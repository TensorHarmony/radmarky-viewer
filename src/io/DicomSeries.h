#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace radmarky::io
{

struct DicomFileRecord
{
    using PatientPosition = std::array<double, 3>;
    using PatientOrientation = std::array<double, 6>;
    using PixelSpacing = std::array<double, 2>;

    std::filesystem::path filePath;
    std::string seriesInstanceUid;
    bool readable = false;
    std::string issue;
    std::optional<long long> instanceNumber;
    std::optional<PatientPosition> imagePositionPatient;
    std::optional<PatientOrientation> imageOrientationPatient;
    std::optional<PixelSpacing> pixelSpacing;
    std::optional<std::size_t> rows;
    std::optional<std::size_t> columns;
    std::optional<double> spacingBetweenSlices;
    std::string frameOfReferenceUid;
    std::string sopInstanceUid;
    std::optional<long long> acquisitionNumber;
    std::string seriesDescription;
    std::optional<std::size_t> numberOfFrames;
};

struct DicomSeriesCandidate
{
    std::string seriesInstanceUid;
    std::string seriesDescription;
    std::vector<std::size_t> recordIndices;
    std::optional<std::size_t> rows;
    std::optional<std::size_t> columns;
    std::size_t sliceCount = 0;
    std::optional<double> sliceSpacingMillimetres;
    std::vector<std::string> consistencyIssues;
    std::string detectionNote;
    std::size_t partNumber = 1;
    std::size_t partCount = 1;
    bool gantryTilt = false;
    bool nonUniformSpacingOverrideAllowed = false;
    bool spacingMetadataMismatchOverrideAllowed = false;

    [[nodiscard]] bool consistent() const noexcept
    {
        return consistencyIssues.empty();
    }

    [[nodiscard]] bool importable() const noexcept
    {
        return consistent() || nonUniformSpacingOverrideAllowed
            || spacingMetadataMismatchOverrideAllowed;
    }
};

struct DicomSeriesAnalysis
{
    std::vector<DicomSeriesCandidate> series;
    std::vector<std::size_t> ignoredIndices;
    std::optional<std::size_t> proposedSeriesIndex;

    // Retained for non-UI callers that want the uniquely largest consistent
    // candidate. The import UI presents every candidate and always asks the
    // user to make the final choice.
    std::optional<std::string> proposedSeriesUid;
    std::vector<std::size_t> defaultSelection;

    [[nodiscard]] bool canAutomaticallyImport() const noexcept
    {
        return series.size() == 1 && series.front().consistent();
    }
};

[[nodiscard]] DicomSeriesAnalysis analyzeDicomSeries(
    const std::vector<DicomFileRecord>& files);

[[nodiscard]] bool isValidSingleSeriesSelection(
    const std::vector<DicomFileRecord>& files,
    const std::vector<std::size_t>& selectedIndices);

} // namespace radmarky::io
