#include "io/DicomSeries.h"

#include "io/DicomGeometry.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <utility>

namespace radmarky::io
{
namespace
{

constexpr double spacingAbsoluteTolerance = 1.0e-3;
constexpr double spacingRelativeTolerance = 1.0e-3;

bool spacingNear(const double left, const double right)
{
    return std::abs(left - right)
        <= std::max(
            spacingAbsoluteTolerance,
            spacingRelativeTolerance * std::max(std::abs(left), std::abs(right)));
}

DicomSeriesCandidate makeCandidate(
    const std::vector<DicomFileRecord>& files,
    std::vector<std::size_t> indices)
{
    DicomSeriesCandidate candidate;
    candidate.recordIndices = std::move(indices);
    if(candidate.recordIndices.empty())
    {
        return candidate;
    }

    std::vector<DicomFileRecord> records;
    records.reserve(candidate.recordIndices.size());
    for(const auto index : candidate.recordIndices)
    {
        records.push_back(files[index]);
    }

    const auto& first = records.front();
    candidate.seriesInstanceUid = first.seriesInstanceUid;
    candidate.rows = first.rows;
    candidate.columns = first.columns;
    candidate.sliceCount = records.size() == 1 && first.numberOfFrames
        ? *first.numberOfFrames
        : records.size();
    for(const auto& record : records)
    {
        if(candidate.seriesDescription.empty() && !record.seriesDescription.empty())
        {
            candidate.seriesDescription = record.seriesDescription;
        }
    }

    const auto geometry = analyzeDicomGeometry(records);
    std::vector<std::size_t> orderedRecordIndices;
    orderedRecordIndices.reserve(candidate.recordIndices.size());
    for(const auto orderedIndex : geometry.orderedIndices)
    {
        if(orderedIndex < candidate.recordIndices.size())
        {
            orderedRecordIndices.push_back(candidate.recordIndices[orderedIndex]);
        }
    }
    if(orderedRecordIndices.size() == candidate.recordIndices.size())
    {
        candidate.recordIndices = std::move(orderedRecordIndices);
    }
    candidate.sliceSpacingMillimetres = geometry.sliceSpacingMillimetres;
    if(!candidate.sliceSpacingMillimetres && records.size() == 1
       && first.spacingBetweenSlices
       && std::isfinite(*first.spacingBetweenSlices)
       && std::abs(*first.spacingBetweenSlices) > spacingAbsoluteTolerance)
    {
        candidate.sliceSpacingMillimetres =
            std::abs(*first.spacingBetweenSlices);
    }
    candidate.gantryTilt = geometry.gantryTilt;
    candidate.nonUniformSpacingOverrideAllowed =
        geometry.canOverrideNonUniformSpacing();
    candidate.spacingMetadataMismatchOverrideAllowed =
        geometry.canOverrideSpacingMetadataMismatch();
    candidate.consistencyIssues.reserve(geometry.diagnostics.size());
    for(const auto& diagnostic : geometry.diagnostics)
    {
        candidate.consistencyIssues.push_back(diagnostic.message);
    }
    return candidate;
}

std::vector<DicomSeriesCandidate> splitByAcquisitionNumber(
    const std::vector<DicomFileRecord>& files,
    const std::vector<std::size_t>& indices)
{
    std::map<long long, std::vector<std::size_t>> groups;
    for(const auto index : indices)
    {
        if(!files[index].acquisitionNumber)
        {
            return {};
        }
        groups[*files[index].acquisitionNumber].push_back(index);
    }
    if(groups.size() < 2
       || std::any_of(groups.begin(), groups.end(), [](const auto& entry) {
              return entry.second.size() < 2;
          }))
    {
        return {};
    }

    std::vector<DicomSeriesCandidate> candidates;
    candidates.reserve(groups.size());
    std::size_t consistentCount = 0;
    for(auto& [acquisition, group] : groups)
    {
        auto candidate = makeCandidate(files, std::move(group));
        candidate.detectionNote =
            "Separated acquisition " + std::to_string(acquisition)
            + " from a shared Series Instance UID";
        consistentCount += candidate.consistent() ? 1U : 0U;
        candidates.push_back(std::move(candidate));
    }
    return consistentCount >= 2 ? candidates : std::vector<DicomSeriesCandidate>{};
}

std::vector<DicomSeriesCandidate> splitByDeclaredSpacing(
    const std::vector<DicomFileRecord>& files,
    const std::vector<std::size_t>& indices)
{
    struct SpacingGroup
    {
        double spacing = 0.0;
        std::vector<std::size_t> indices;
    };
    std::vector<SpacingGroup> groups;
    for(const auto index : indices)
    {
        const auto spacing = files[index].spacingBetweenSlices;
        if(!spacing || !std::isfinite(*spacing)
           || std::abs(*spacing) <= spacingAbsoluteTolerance)
        {
            return {};
        }
        const double absoluteSpacing = std::abs(*spacing);
        const auto match = std::find_if(
            groups.begin(), groups.end(), [absoluteSpacing](const auto& group) {
                return spacingNear(group.spacing, absoluteSpacing);
            });
        if(match == groups.end())
        {
            groups.push_back({absoluteSpacing, {index}});
        }
        else
        {
            match->indices.push_back(index);
        }
    }
    if(groups.size() < 2
       || std::any_of(groups.begin(), groups.end(), [](const auto& group) {
              return group.indices.size() < 2;
          }))
    {
        return {};
    }

    std::vector<DicomSeriesCandidate> candidates;
    candidates.reserve(groups.size());
    std::size_t consistentCount = 0;
    for(auto& group : groups)
    {
        auto candidate = makeCandidate(files, std::move(group.indices));
        candidate.detectionNote =
            "Separated by distinct slice-spacing metadata ("
            + std::to_string(group.spacing) + " mm) within a shared Series Instance UID";
        consistentCount += candidate.consistent() ? 1U : 0U;
        candidates.push_back(std::move(candidate));
    }
    return consistentCount >= 2 ? candidates : std::vector<DicomSeriesCandidate>{};
}

std::vector<DicomSeriesCandidate> splitByPositionSpacing(
    const std::vector<DicomFileRecord>& files,
    const std::vector<std::size_t>& indices)
{
    if(indices.size() < 6)
    {
        return {};
    }

    struct PositionedRecord
    {
        double position = 0.0;
        std::size_t index = 0;
    };
    const auto& first = files[indices.front()];
    if(!first.imageOrientationPatient)
    {
        return {};
    }
    const auto& orientation = *first.imageOrientationPatient;
    const std::array<double, 3> row{
        orientation[0], orientation[1], orientation[2]};
    const std::array<double, 3> column{
        orientation[3], orientation[4], orientation[5]};
    std::array<double, 3> normal{
        row[1] * column[2] - row[2] * column[1],
        row[2] * column[0] - row[0] * column[2],
        row[0] * column[1] - row[1] * column[0]};
    const double normalLength = std::sqrt(
        normal[0] * normal[0] + normal[1] * normal[1]
        + normal[2] * normal[2]);
    if(!std::isfinite(normalLength) || normalLength <= spacingAbsoluteTolerance)
    {
        return {};
    }
    for(auto& value : normal)
    {
        value /= normalLength;
    }

    std::vector<PositionedRecord> positioned;
    positioned.reserve(indices.size());
    for(const auto index : indices)
    {
        const auto& record = files[index];
        if(!record.imagePositionPatient || !record.imageOrientationPatient
           || *record.imageOrientationPatient != orientation)
        {
            return {};
        }
        const auto& position = *record.imagePositionPatient;
        positioned.push_back({
            position[0] * normal[0] + position[1] * normal[1]
                + position[2] * normal[2],
            index});
    }
    std::sort(
        positioned.begin(),
        positioned.end(),
        [](const auto& left, const auto& right) {
            return left.position < right.position;
        });

    std::vector<double> gaps;
    gaps.reserve(positioned.size() - 1);
    for(std::size_t index = 1; index < positioned.size(); ++index)
    {
        const double gap = positioned[index].position - positioned[index - 1].position;
        if(!std::isfinite(gap) || gap <= spacingAbsoluteTolerance)
        {
            return {};
        }
        gaps.push_back(gap);
    }
    std::vector<bool> uniformPrefix(gaps.size(), true);
    for(std::size_t index = 1; index < gaps.size(); ++index)
    {
        uniformPrefix[index] = uniformPrefix[index - 1]
            && spacingNear(gaps[index], gaps.front());
    }
    std::vector<bool> uniformSuffix(gaps.size(), true);
    for(std::size_t index = gaps.size() - 1; index > 0; --index)
    {
        uniformSuffix[index - 1] = uniformSuffix[index]
            && spacingNear(gaps[index - 1], gaps.back());
    }

    // A position-only split is intentionally conservative. Both sides must be
    // complete, uniform stacks with different spacings, and the separating gap
    // must belong to neither grid. This avoids turning a single stack with a
    // missing slice into two apparently healthy series.
    for(std::size_t boundary = 3; boundary + 3 <= positioned.size(); ++boundary)
    {
        const double boundaryGap = gaps[boundary - 1];
        if(!uniformPrefix[boundary - 2] || !uniformSuffix[boundary]
           || spacingNear(gaps.front(), gaps.back())
           || spacingNear(boundaryGap, gaps.front())
           || spacingNear(boundaryGap, gaps.back()))
        {
            continue;
        }
        std::vector<std::size_t> leftIndices;
        std::vector<std::size_t> rightIndices;
        leftIndices.reserve(boundary);
        rightIndices.reserve(positioned.size() - boundary);
        for(std::size_t index = 0; index < boundary; ++index)
        {
            leftIndices.push_back(positioned[index].index);
        }
        for(std::size_t index = boundary; index < positioned.size(); ++index)
        {
            rightIndices.push_back(positioned[index].index);
        }
        auto left = makeCandidate(files, std::move(leftIndices));
        auto right = makeCandidate(files, std::move(rightIndices));
        if(!left.consistent() || !right.consistent()
           || !left.sliceSpacingMillimetres || !right.sliceSpacingMillimetres
           || spacingNear(
               *left.sliceSpacingMillimetres, *right.sliceSpacingMillimetres))
        {
            continue;
        }
        left.detectionNote =
            "Separated by a change in measured slice spacing within a shared Series Instance UID";
        right.detectionNote = left.detectionNote;
        return {std::move(left), std::move(right)};
    }
    return {};
}

void numberCandidateParts(std::vector<DicomSeriesCandidate>& candidates)
{
    for(std::size_t index = 0; index < candidates.size(); ++index)
    {
        candidates[index].partNumber = index + 1;
        candidates[index].partCount = candidates.size();
    }
}

} // namespace

DicomSeriesAnalysis analyzeDicomSeries(
    const std::vector<DicomFileRecord>& files)
{
    DicomSeriesAnalysis analysis;
    std::map<std::string, std::vector<std::size_t>> groups;
    for(std::size_t index = 0; index < files.size(); ++index)
    {
        const auto& file = files[index];
        if(file.readable && !file.seriesInstanceUid.empty())
        {
            groups[file.seriesInstanceUid].push_back(index);
        }
        else
        {
            analysis.ignoredIndices.push_back(index);
        }
    }

    for(const auto& [uid, indices] : groups)
    {
        static_cast<void>(uid);
        auto whole = makeCandidate(files, indices);
        std::vector<DicomSeriesCandidate> candidates;
        if(!whole.consistent())
        {
            candidates = splitByAcquisitionNumber(files, indices);
            if(candidates.empty())
            {
                candidates = splitByDeclaredSpacing(files, indices);
            }
            if(candidates.empty())
            {
                candidates = splitByPositionSpacing(files, indices);
            }
        }
        if(candidates.empty())
        {
            candidates.push_back(std::move(whole));
        }
        numberCandidateParts(candidates);
        analysis.series.insert(
            analysis.series.end(),
            std::make_move_iterator(candidates.begin()),
            std::make_move_iterator(candidates.end()));
    }

    std::size_t greatestCount = 0;
    bool tied = false;
    for(std::size_t index = 0; index < analysis.series.size(); ++index)
    {
        const auto& candidate = analysis.series[index];
        if(!candidate.consistent())
        {
            continue;
        }
        if(candidate.sliceCount > greatestCount)
        {
            greatestCount = candidate.sliceCount;
            analysis.proposedSeriesIndex = index;
            tied = false;
        }
        else if(candidate.sliceCount == greatestCount)
        {
            tied = true;
        }
    }
    if(tied)
    {
        analysis.proposedSeriesIndex.reset();
    }
    if(analysis.proposedSeriesIndex)
    {
        const auto& proposed = analysis.series[*analysis.proposedSeriesIndex];
        analysis.proposedSeriesUid = proposed.seriesInstanceUid;
        analysis.defaultSelection = proposed.recordIndices;
    }
    return analysis;
}

bool isValidSingleSeriesSelection(
    const std::vector<DicomFileRecord>& files,
    const std::vector<std::size_t>& selectedIndices)
{
    if(selectedIndices.empty())
    {
        return false;
    }

    std::optional<std::string> selectedUid;
    for(const auto index : selectedIndices)
    {
        if(index >= files.size())
        {
            return false;
        }
        const auto& file = files[index];
        if(!file.readable || file.seriesInstanceUid.empty())
        {
            return false;
        }
        if(!selectedUid)
        {
            selectedUid = file.seriesInstanceUid;
        }
        else if(file.seriesInstanceUid != *selectedUid)
        {
            return false;
        }
    }
    return true;
}

} // namespace radmarky::io
