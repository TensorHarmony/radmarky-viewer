#include "io/DicomGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <set>
#include <sstream>

namespace radmarky::io
{
namespace
{

using Vector = DicomFileRecord::PatientPosition;

constexpr double positionAbsoluteTolerance = 1.0e-3;
constexpr double spacingRelativeTolerance = 1.0e-3;
constexpr double orientationTolerance = 1.0e-4;

double dot(const Vector& left, const Vector& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

Vector subtract(const Vector& left, const Vector& right)
{
    return {{left[0] - right[0], left[1] - right[1], left[2] - right[2]}};
}

Vector divide(const Vector& value, const double divisor)
{
    return {{value[0] / divisor, value[1] / divisor, value[2] / divisor}};
}

Vector cross(const Vector& left, const Vector& right)
{
    return {{
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    }};
}

double norm(const Vector& value)
{
    return std::sqrt(dot(value, value));
}

std::optional<Vector> normalized(const Vector& value)
{
    const double length = norm(value);
    if(!std::isfinite(length) || length <= orientationTolerance)
    {
        return std::nullopt;
    }
    return divide(value, length);
}

bool nearlyEqual(
    const double left,
    const double right,
    const double absoluteTolerance,
    const double relativeTolerance)
{
    return std::abs(left - right)
        <= std::max(
            absoluteTolerance,
            relativeTolerance * std::max(std::abs(left), std::abs(right)));
}

bool vectorsNear(const Vector& left, const Vector& right, const double tolerance)
{
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(std::abs(left[axis] - right[axis]) > tolerance)
        {
            return false;
        }
    }
    return true;
}

bool finiteVector(const Vector& value)
{
    return std::all_of(value.begin(), value.end(), [](const double component) {
        return std::isfinite(component);
    });
}

std::optional<std::array<Vector, 2>> normalizedInPlaneOrientation(
    const DicomFileRecord::PatientOrientation& orientation)
{
    const Vector row{{orientation[0], orientation[1], orientation[2]}};
    const Vector column{{orientation[3], orientation[4], orientation[5]}};
    const auto normalizedRow = normalized(row);
    const auto normalizedColumn = normalized(column);
    if(!normalizedRow || !normalizedColumn
       || std::abs(norm(row) - 1.0) > orientationTolerance
       || std::abs(norm(column) - 1.0) > orientationTolerance
       || std::abs(dot(*normalizedRow, *normalizedColumn))
           > orientationTolerance)
    {
        return std::nullopt;
    }
    return std::array<Vector, 2>{{*normalizedRow, *normalizedColumn}};
}

void addDiagnostic(
    DicomGeometryAnalysis& analysis,
    const DicomGeometryIssueKind kind,
    std::string message)
{
    const bool alreadyPresent = std::any_of(
        analysis.diagnostics.begin(),
        analysis.diagnostics.end(),
        [kind](const auto& diagnostic) { return diagnostic.kind == kind; });
    if(!alreadyPresent)
    {
        analysis.diagnostics.push_back({kind, std::move(message)});
    }
}

double lowerMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) / 2];
}

bool hasMissingSlicePattern(
    const std::vector<double>& projectedGaps,
    const double expectedGap)
{
    bool hasMultiple = false;
    for(const double gap : projectedGaps)
    {
        const double ratio = gap / expectedGap;
        const double nearestInteger = std::round(ratio);
        if(nearestInteger < 1.0 || std::abs(ratio - nearestInteger) > 0.05)
        {
            return false;
        }
        hasMultiple |= nearestInteger >= 2.0;
    }
    return hasMultiple;
}

} // namespace

DicomGeometryAnalysis analyzeDicomGeometry(
    const std::vector<DicomFileRecord>& records)
{
    DicomGeometryAnalysis analysis;
    analysis.orderedIndices.resize(records.size());
    std::iota(
        analysis.orderedIndices.begin(), analysis.orderedIndices.end(), std::size_t{0});

    if(records.size() <= 1)
    {
        // A single file may be a valid multi-frame object. Its frame geometry is
        // interpreted by GDCM/ITK rather than by this classic slice-stack check.
        return analysis;
    }

    const bool hasCompleteSpatialMetadata = std::all_of(
        records.begin(), records.end(), [](const DicomFileRecord& record) {
            return record.imagePositionPatient.has_value()
                && record.imageOrientationPatient.has_value()
                && record.pixelSpacing.has_value() && record.rows.has_value()
                && record.columns.has_value();
        });
    if(!hasCompleteSpatialMetadata)
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::MissingSpatialMetadata,
            "Every slice must contain Image Position (Patient), Image Orientation "
            "(Patient), Pixel Spacing, Rows, and Columns");
        return analysis;
    }
    analysis.completeSpatialGeometry = true;

    if(std::any_of(records.begin(), records.end(), [](const auto& record) {
           return !finiteVector(*record.imagePositionPatient);
       }))
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::MissingSpatialMetadata,
            "Image Position (Patient) contains a non-finite coordinate");
        return analysis;
    }

    std::array<Vector, 2> referenceOrientation{};
    bool haveReferenceOrientation = false;
    for(const auto& record : records)
    {
        const auto orientation =
            normalizedInPlaneOrientation(*record.imageOrientationPatient);
        if(!orientation)
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::InvalidOrientation,
                "Image Orientation (Patient) must contain orthogonal unit direction "
                "cosines");
            continue;
        }
        if(!haveReferenceOrientation)
        {
            referenceOrientation = *orientation;
            haveReferenceOrientation = true;
        }
        else if(!vectorsNear(
                    (*orientation)[0],
                    referenceOrientation[0],
                    orientationTolerance)
                || !vectorsNear(
                    (*orientation)[1],
                    referenceOrientation[1],
                    orientationTolerance))
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::InconsistentOrientation,
                "Image Orientation (Patient) changes within the selected series");
        }
    }
    if(!haveReferenceOrientation)
    {
        return analysis;
    }

    const auto referenceRows = *records.front().rows;
    const auto referenceColumns = *records.front().columns;
    const auto referencePixelSpacing = *records.front().pixelSpacing;
    for(const auto& record : records)
    {
        if(*record.rows == 0 || *record.columns == 0
           || *record.rows != referenceRows
           || *record.columns != referenceColumns)
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::InconsistentDimensions,
                "Rows or Columns change within the selected series");
        }
        for(std::size_t axis = 0; axis < 2; ++axis)
        {
            if(!std::isfinite((*record.pixelSpacing)[axis])
               || (*record.pixelSpacing)[axis] <= 0.0
               || !nearlyEqual(
                   (*record.pixelSpacing)[axis],
                   referencePixelSpacing[axis],
                   1.0e-6,
                   1.0e-4))
            {
                addDiagnostic(
                    analysis,
                    DicomGeometryIssueKind::InconsistentPixelSpacing,
                    "Pixel Spacing changes or is invalid within the selected series");
            }
        }
    }


    const bool hasAnySpacingBetweenSlices = std::any_of(
        records.begin(), records.end(), [](const DicomFileRecord& record) {
            return record.spacingBetweenSlices.has_value();
        });
    const bool hasAllSpacingBetweenSlices = std::all_of(
        records.begin(), records.end(), [](const DicomFileRecord& record) {
            return record.spacingBetweenSlices.has_value();
        });
    std::optional<double> declaredSliceSpacing;
    if(hasAnySpacingBetweenSlices && !hasAllSpacingBetweenSlices)
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::InconsistentSpacingMetadata,
            "Spacing Between Slices is present on only some selected slices");
    }
    else if(hasAllSpacingBetweenSlices)
    {
        declaredSliceSpacing = std::abs(*records.front().spacingBetweenSlices);
        for(const auto& record : records)
        {
            const double spacing = std::abs(*record.spacingBetweenSlices);
            if(!std::isfinite(spacing) || spacing <= positionAbsoluteTolerance
               || !nearlyEqual(
                   spacing,
                   *declaredSliceSpacing,
                   positionAbsoluteTolerance,
                   spacingRelativeTolerance))
            {
                addDiagnostic(
                    analysis,
                    DicomGeometryIssueKind::InconsistentSpacingMetadata,
                    "Spacing Between Slices changes or is invalid within the "
                    "selected series");
            }
        }
    }

    std::optional<std::string> frameOfReference;
    bool missingFrameOfReference = false;
    for(const auto& record : records)
    {
        if(record.frameOfReferenceUid.empty())
        {
            missingFrameOfReference |= frameOfReference.has_value();
            continue;
        }
        if(!frameOfReference)
        {
            frameOfReference = record.frameOfReferenceUid;
        }
        else if(record.frameOfReferenceUid != *frameOfReference)
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::InconsistentFrameOfReference,
                "Frame of Reference UID changes within the selected series");
        }
    }
    if(frameOfReference)
    {
        missingFrameOfReference |= std::any_of(
            records.begin(), records.end(), [](const DicomFileRecord& record) {
                return record.frameOfReferenceUid.empty();
            });
    }
    if(missingFrameOfReference)
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::InconsistentFrameOfReference,
            "Frame of Reference UID is present on only some selected slices");
    }

    std::set<std::string> sopInstanceUids;
    for(const auto& record : records)
    {
        if(!record.sopInstanceUid.empty()
           && !sopInstanceUids.insert(record.sopInstanceUid).second)
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::DuplicateSlice,
                "The selected series repeats a SOP Instance UID");
        }
    }

    const auto normal = normalized(
        cross(referenceOrientation[0], referenceOrientation[1]));
    if(!normal)
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::InvalidOrientation,
            "Image Orientation (Patient) cannot define a slice normal");
        return analysis;
    }

    std::stable_sort(
        analysis.orderedIndices.begin(),
        analysis.orderedIndices.end(),
        [&records, &normal](const std::size_t left, const std::size_t right) {
            const double leftPosition =
                dot(*records[left].imagePositionPatient, *normal);
            const double rightPosition =
                dot(*records[right].imagePositionPatient, *normal);
            if(leftPosition != rightPosition)
            {
                return leftPosition < rightPosition;
            }
            return records[left].filePath < records[right].filePath;
        });

    std::vector<double> projectedGaps;
    std::vector<Vector> positionSteps;
    projectedGaps.reserve(records.size() - 1);
    positionSteps.reserve(records.size() - 1);
    for(std::size_t ordered = 1; ordered < analysis.orderedIndices.size(); ++ordered)
    {
        const auto& previous = records[analysis.orderedIndices[ordered - 1]];
        const auto& current = records[analysis.orderedIndices[ordered]];
        const Vector step = subtract(
            *current.imagePositionPatient, *previous.imagePositionPatient);
        const double projectedGap = dot(step, *normal);
        if(!std::isfinite(projectedGap)
           || projectedGap <= positionAbsoluteTolerance)
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::DuplicateSlice,
                "Two or more slices have the same position along the slice normal");
        }
        projectedGaps.push_back(projectedGap);
        positionSteps.push_back(step);
    }
    if(std::any_of(
           projectedGaps.begin(), projectedGaps.end(), [](const double gap) {
               return !std::isfinite(gap) || gap <= positionAbsoluteTolerance;
           }))
    {
        return analysis;
    }

    const double expectedProjectedGap = lowerMedian(projectedGaps);
    const double spacingTolerance = std::max(
        positionAbsoluteTolerance,
        spacingRelativeTolerance * expectedProjectedGap);
    const bool nonUniformSpacing = std::any_of(
        projectedGaps.begin(), projectedGaps.end(), [&](const double gap) {
            return std::abs(gap - expectedProjectedGap) > spacingTolerance;
        });
    if(nonUniformSpacing)
    {
        const double minimumGap =
            *std::min_element(projectedGaps.begin(), projectedGaps.end());
        if(hasMissingSlicePattern(projectedGaps, minimumGap))
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::MissingSlices,
                "Slice positions contain one or more gaps consistent with missing "
                "slices");
        }
        else
        {
            addDiagnostic(
                analysis,
                DicomGeometryIssueKind::NonUniformSpacing,
                "Slice spacing is not uniform within the selected series");
        }
    }
    else if(declaredSliceSpacing
            && !nearlyEqual(
                expectedProjectedGap,
                *declaredSliceSpacing,
                positionAbsoluteTolerance,
                spacingRelativeTolerance))
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::SpacingMetadataMismatch,
            "Spacing Between Slices disagrees with Image Position (Patient)");
    }

    const Vector referenceStepPerMillimetre =
        divide(positionSteps.front(), projectedGaps.front());
    bool inconsistentStackDirection = false;
    for(std::size_t index = 1; index < positionSteps.size(); ++index)
    {
        const Vector stepPerMillimetre =
            divide(positionSteps[index], projectedGaps[index]);
        if(!vectorsNear(
               stepPerMillimetre,
               referenceStepPerMillimetre,
               spacingRelativeTolerance))
        {
            inconsistentStackDirection = true;
            break;
        }
    }
    if(inconsistentStackDirection)
    {
        addDiagnostic(
            analysis,
            DicomGeometryIssueKind::InconsistentStackDirection,
            "Slice positions do not follow one consistent stack direction");
    }

    Vector inPlaneStep = referenceStepPerMillimetre;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        inPlaneStep[axis] -= (*normal)[axis];
    }
    analysis.gantryTilt = norm(inPlaneStep) > orientationTolerance;
    analysis.sliceSpacingMillimetres =
        expectedProjectedGap * norm(referenceStepPerMillimetre);
    return analysis;
}

std::string formatDicomGeometryDiagnostics(
    const DicomGeometryAnalysis& analysis)
{
    std::ostringstream message;
    for(std::size_t index = 0; index < analysis.diagnostics.size(); ++index)
    {
        if(index != 0)
        {
            message << "; ";
        }
        message << analysis.diagnostics[index].message;
    }
    return message.str();
}

} // namespace radmarky::io
