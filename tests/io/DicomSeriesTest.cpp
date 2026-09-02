#include "io/DicomSeries.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

bool expectFalse(const bool condition, const std::string_view field)
{
    if(!condition)
    {
        return true;
    }
    std::cerr << field << ": expected false\n";
    return false;
}

bool expectNear(
    const std::optional<double>& actual,
    const double expected,
    const std::string_view field)
{
    if(actual && std::abs(*actual - expected) <= 1.0e-9)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << '\n';
    return false;
}

radmarky::io::DicomFileRecord recordAt(
    const char* const name,
    const char* const uid,
    const double z,
    const std::size_t identifier,
    const std::optional<long long> acquisition = std::nullopt,
    const std::optional<double> declaredSpacing = std::nullopt)
{
    radmarky::io::DicomFileRecord record;
    record.filePath = std::filesystem::path(name);
    record.seriesInstanceUid = uid;
    record.readable = true;
    record.instanceNumber = static_cast<long long>(identifier);
    record.imagePositionPatient = {{0.0, 0.0, z}};
    record.imageOrientationPatient = {{1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    record.pixelSpacing = {{0.8, 0.7}};
    record.rows = 32;
    record.columns = 64;
    record.spacingBetweenSlices = declaredSpacing;
    record.frameOfReferenceUid = "1.2.3.4";
    record.sopInstanceUid = "1.2.3.4." + std::to_string(identifier);
    record.acquisitionNumber = acquisition;
    record.seriesDescription = "Axial chest";
    return record;
}

} // namespace

int main()
{
    using radmarky::io::analyzeDicomSeries;
    using radmarky::io::isValidSingleSeriesSelection;

    bool passed = true;
    const std::vector oneSeries{
        recordAt("one", "1.2.3", 0.0, 1),
        recordAt("two.dcm", "1.2.3", 1.0, 2),
        recordAt("three.dicom", "1.2.3", 2.0, 3),
    };
    const auto oneAnalysis = analyzeDicomSeries(oneSeries);
    passed &= expectTrue(oneAnalysis.series.size() == 1, "one detected series");
    passed &= expectTrue(
        oneAnalysis.series.front().consistent(), "one series is consistent");
    passed &= expectTrue(
        oneAnalysis.series.front().seriesDescription == "Axial chest",
        "series description retained");
    passed &= expectTrue(
        oneAnalysis.series.front().columns == std::optional<std::size_t>{64}
            && oneAnalysis.series.front().rows == std::optional<std::size_t>{32}
            && oneAnalysis.series.front().sliceCount == 3,
        "series resolution retained");
    passed &= expectNear(
        oneAnalysis.series.front().sliceSpacingMillimetres,
        1.0,
        "measured series spacing");
    passed &= expectTrue(
        oneAnalysis.proposedSeriesIndex == std::optional<std::size_t>{0},
        "one series proposed");
    passed &= expectTrue(
        oneAnalysis.defaultSelection == std::vector<std::size_t>({0, 1, 2}),
        "one series default selection");
    passed &= expectTrue(
        oneAnalysis.canAutomaticallyImport(),
        "one consistent series can be imported without review");
    passed &= expectTrue(
        isValidSingleSeriesSelection(oneSeries, {0, 2}),
        "subset of one series valid");

    const std::vector shuffledSeries{
        recordAt("slice-three", "2.3.4", 2.0, 6),
        recordAt("slice-one", "2.3.4", 0.0, 7),
        recordAt("slice-two", "2.3.4", 1.0, 8),
    };
    const auto shuffledAnalysis = analyzeDicomSeries(shuffledSeries);
    passed &= expectTrue(
        shuffledAnalysis.series.size() == 1
            && shuffledAnalysis.series.front().recordIndices
                == std::vector<std::size_t>({1, 2, 0}),
        "candidate files stored in geometric slice order");
    passed &= expectTrue(
        shuffledAnalysis.defaultSelection
            == std::vector<std::size_t>({1, 2, 0}),
        "default selection uses geometric slice order");

    auto secondUid = recordAt("other", "9.8.7", 0.0, 4);
    auto missingUid = recordAt("missing", "", 0.0, 5);
    missingUid.issue = "Missing Series Instance UID";
    radmarky::io::DicomFileRecord unreadable;
    unreadable.filePath = "notes.txt";
    unreadable.issue = "Not a readable DICOM file";
    const std::vector mixed{
        oneSeries[0], oneSeries[1], oneSeries[2], secondUid, missingUid, unreadable};
    const auto mixedAnalysis = analyzeDicomSeries(mixed);
    passed &= expectTrue(mixedAnalysis.series.size() == 2, "UID groups detected");
    passed &= expectFalse(
        mixedAnalysis.canAutomaticallyImport(),
        "multiple series require review");
    passed &= expectTrue(
        mixedAnalysis.ignoredIndices == std::vector<std::size_t>({4, 5}),
        "unassigned inputs reported");
    passed &= expectTrue(
        mixedAnalysis.proposedSeriesUid == std::optional<std::string>("1.2.3"),
        "largest consistent series proposed");
    passed &= expectFalse(
        isValidSingleSeriesSelection(mixed, {0, 3}),
        "mixed UID selection invalid");
    passed &= expectFalse(
        isValidSingleSeriesSelection(mixed, {4}),
        "missing UID selection invalid");
    passed &= expectFalse(
        isValidSingleSeriesSelection(mixed, {}),
        "empty selection invalid");

    const std::vector fused{
        recordAt("a0", "5.5.5", 0.0, 10, 1, 1.0),
        recordAt("a1", "5.5.5", 1.0, 11, 1, 1.0),
        recordAt("a2", "5.5.5", 2.0, 12, 1, 1.0),
        recordAt("b0", "5.5.5", 0.5, 20, 2, 2.0),
        recordAt("b1", "5.5.5", 2.5, 21, 2, 2.0),
        recordAt("b2", "5.5.5", 4.5, 22, 2, 2.0),
    };
    const auto fusedAnalysis = analyzeDicomSeries(fused);
    passed &= expectTrue(
        fusedAnalysis.series.size() == 2,
        "fused acquisitions with one UID split into candidates");
    if(fusedAnalysis.series.size() == 2)
    {
        passed &= expectTrue(
            fusedAnalysis.series[0].consistent()
                && fusedAnalysis.series[1].consistent(),
            "fused candidates are independently consistent");
        passed &= expectNear(
            fusedAnalysis.series[0].sliceSpacingMillimetres,
            1.0,
            "first fused spacing");
        passed &= expectNear(
            fusedAnalysis.series[1].sliceSpacingMillimetres,
            2.0,
            "second fused spacing");
        passed &= expectTrue(
            fusedAnalysis.series[0].partCount == 2
                && fusedAnalysis.series[1].partNumber == 2,
            "shared UID parts numbered");
    }

    const std::vector positionSplit{
        recordAt("p0", "6.6.6", 0.0, 30),
        recordAt("p1", "6.6.6", 1.0, 31),
        recordAt("p2", "6.6.6", 2.0, 32),
        recordAt("p3", "6.6.6", 10.0, 33),
        recordAt("p4", "6.6.6", 12.0, 34),
        recordAt("p5", "6.6.6", 14.0, 35),
    };
    const auto positionAnalysis = analyzeDicomSeries(positionSplit);
    passed &= expectTrue(
        positionAnalysis.series.size() == 2,
        "measured spacing transition split without filename assumptions");

    const std::vector missingSliceGrid{
        recordAt("m0", "7.7.7", 0.0, 60),
        recordAt("m1", "7.7.7", 1.0, 61),
        recordAt("m2", "7.7.7", 2.0, 62),
        recordAt("m3", "7.7.7", 4.0, 63),
        recordAt("m4", "7.7.7", 5.0, 64),
        recordAt("m5", "7.7.7", 6.0, 65),
    };
    const auto missingSliceAnalysis = analyzeDicomSeries(missingSliceGrid);
    passed &= expectTrue(
        missingSliceAnalysis.series.size() == 1
            && !missingSliceAnalysis.series.front().consistent(),
        "missing slice is reported instead of being split into healthy fragments");
    passed &= expectTrue(
        missingSliceAnalysis.series.front().missingSlicesOverrideAllowed
            && missingSliceAnalysis.series.front().importable()
            && missingSliceAnalysis.series.front().consistencyIssueCodes
                == std::vector<std::string>{"DICOM_GEOMETRY_MISSING_SLICES"},
        "missing slice candidate exposes error code and manual override");
    passed &= expectFalse(
        missingSliceAnalysis.proposedSeriesIndex.has_value()
            || missingSliceAnalysis.canAutomaticallyImport(),
        "missing slice override requires review and explicit selection");

    const std::vector irregularSpacing{
        recordAt("i0", "8.8.8", 0.0, 70),
        recordAt("i1", "8.8.8", 1.0, 71),
        recordAt("i2", "8.8.8", 2.25, 72),
        recordAt("i3", "8.8.8", 3.25, 73),
    };
    const auto irregularSpacingAnalysis = analyzeDicomSeries(irregularSpacing);
    passed &= expectTrue(
        irregularSpacingAnalysis.series.size() == 1
            && !irregularSpacingAnalysis.series.front().consistent()
            && irregularSpacingAnalysis.series.front()
                   .nonUniformSpacingOverrideAllowed
            && irregularSpacingAnalysis.series.front().importable(),
        "non-uniform spacing candidate allows manual override");
    passed &= expectFalse(
        irregularSpacingAnalysis.proposedSeriesIndex.has_value(),
        "non-uniform spacing candidate is not selected by default");
    passed &= expectFalse(
        irregularSpacingAnalysis.canAutomaticallyImport(),
        "overrideable non-uniform spacing requires review");

    const std::vector spacingMetadataMismatch{
        recordAt("d0", "9.9.9", 0.0, 80, std::nullopt, 2.0),
        recordAt("d1", "9.9.9", 1.0, 81, std::nullopt, 2.0),
        recordAt("d2", "9.9.9", 2.0, 82, std::nullopt, 2.0),
    };
    const auto spacingMetadataMismatchAnalysis =
        analyzeDicomSeries(spacingMetadataMismatch);
    passed &= expectTrue(
        spacingMetadataMismatchAnalysis.series.size() == 1
            && !spacingMetadataMismatchAnalysis.series.front().consistent()
            && spacingMetadataMismatchAnalysis.series.front()
                   .spacingMetadataMismatchOverrideAllowed
            && spacingMetadataMismatchAnalysis.series.front().importable(),
        "declared and position-derived spacing disagreement allows manual override");
    passed &= expectFalse(
        spacingMetadataMismatchAnalysis.proposedSeriesIndex.has_value(),
        "spacing metadata disagreement is not selected by default");
    passed &= expectFalse(
        spacingMetadataMismatchAnalysis.canAutomaticallyImport(),
        "overrideable spacing metadata mismatch requires review");

    auto inconsistent = oneSeries;
    inconsistent.push_back(recordAt("four", "1.2.3", 3.5, 40));
    inconsistent[1].columns = 65;
    inconsistent[2].pixelSpacing = {{0.9, 0.7}};
    inconsistent[3].imageOrientationPatient =
        {{0.0, 1.0, 0.0, -1.0, 0.0, 0.0}};
    inconsistent[3].frameOfReferenceUid = "different-frame";
    const auto inconsistentAnalysis = analyzeDicomSeries(inconsistent);
    passed &= expectTrue(
        inconsistentAnalysis.series.size() == 1,
        "inconsistent UID group remains visible");
    passed &= expectFalse(
        inconsistentAnalysis.series.front().consistent(),
        "inconsistent candidate is not importable");
    passed &= expectTrue(
        inconsistentAnalysis.series.front().consistencyIssues.size() >= 4,
        "multiple inconsistencies reported together");

    const std::vector tied{
        recordAt("tie-a", "1.2.3", 0.0, 50),
        recordAt("tie-b", "9.8.7", 0.0, 51),
    };
    const auto tiedAnalysis = analyzeDicomSeries(tied);
    passed &= expectFalse(
        tiedAnalysis.proposedSeriesIndex.has_value(),
        "equal candidates require an explicit choice");

    return passed ? 0 : 1;
}
