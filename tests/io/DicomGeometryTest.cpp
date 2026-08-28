#include "io/DicomGeometry.h"

#include <cmath>
#include <filesystem>
#include <iostream>
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

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-9)
{
    if(std::abs(actual - expected) <= tolerance)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

bool hasIssue(
    const radmarky::io::DicomGeometryAnalysis& analysis,
    const radmarky::io::DicomGeometryIssueKind kind)
{
    for(const auto& diagnostic : analysis.diagnostics)
    {
        if(diagnostic.kind == kind)
        {
            return true;
        }
    }
    return false;
}

radmarky::io::DicomFileRecord recordAt(
    const double x,
    const double z,
    const std::size_t identifier)
{
    radmarky::io::DicomFileRecord record;
    record.filePath = std::filesystem::path(
        "slice-" + std::to_string(identifier) + ".dcm");
    record.seriesInstanceUid = "1.2.3";
    record.readable = true;
    record.imagePositionPatient = {{x, 0.0, z}};
    record.imageOrientationPatient = {{1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    record.pixelSpacing = {{0.8, 0.7}};
    record.rows = 32;
    record.columns = 64;
    record.frameOfReferenceUid = "1.2.3.4";
    record.sopInstanceUid = "1.2.3.4." + std::to_string(identifier);
    return record;
}

} // namespace

int main()
{
    using radmarky::io::DicomGeometryIssueKind;

    bool passed = true;

    // Order classic DICOM stacks from Image Position (Patient) projected onto
    // the normal formed by the two orientation cosines, independently of input
    // filename or Instance Number order.
    const std::vector shuffled{
        recordAt(0.0, 4.0, 0),
        recordAt(0.0, 0.0, 1),
        recordAt(0.0, 2.0, 2),
    };
    const auto ordered = radmarky::io::analyzeDicomGeometry(shuffled);
    passed &= expectTrue(ordered.valid(), "uniform stack is valid");
    passed &= expectTrue(
        ordered.completeSpatialGeometry, "uniform stack has complete geometry");
    passed &= expectTrue(
        ordered.orderedIndices == std::vector<std::size_t>{1, 2, 0},
        "position-based slice ordering");
    passed &= expectTrue(
        ordered.sliceSpacingMillimetres.has_value(), "slice spacing available");
    if(ordered.sliceSpacingMillimetres)
    {
        passed &= expectNear(
            *ordered.sliceSpacingMillimetres, 2.0, "uniform slice spacing");
    }
    passed &= expectTrue(!ordered.gantryTilt, "orthogonal stack has no tilt");

    const std::vector tilted{
        recordAt(0.0, 0.0, 10),
        recordAt(0.5, 2.0, 11),
        recordAt(1.0, 4.0, 12),
    };
    const auto tilt = radmarky::io::analyzeDicomGeometry(tilted);
    passed &= expectTrue(tilt.valid(), "uniform gantry tilt is valid");
    passed &= expectTrue(tilt.gantryTilt, "gantry tilt detected");
    if(tilt.sliceSpacingMillimetres)
    {
        passed &= expectNear(
            *tilt.sliceSpacingMillimetres,
            std::sqrt(4.25),
            "tilted physical slice spacing");
    }

    auto duplicates = shuffled;
    duplicates[0].imagePositionPatient = duplicates[2].imagePositionPatient;
    const auto duplicateAnalysis =
        radmarky::io::analyzeDicomGeometry(duplicates);
    passed &= expectTrue(
        hasIssue(duplicateAnalysis, DicomGeometryIssueKind::DuplicateSlice),
        "duplicate position rejected");

    std::vector missing{
        recordAt(0.0, 0.0, 20),
        recordAt(0.0, 1.0, 21),
        recordAt(0.0, 3.0, 22),
    };
    const auto missingAnalysis = radmarky::io::analyzeDicomGeometry(missing);
    passed &= expectTrue(
        hasIssue(missingAnalysis, DicomGeometryIssueKind::MissingSlices),
        "missing slice gap rejected");

    std::vector irregular{
        recordAt(0.0, 0.0, 30),
        recordAt(0.0, 1.0, 31),
        recordAt(0.0, 2.25, 32),
        recordAt(0.0, 3.25, 33),
    };
    const auto irregularAnalysis =
        radmarky::io::analyzeDicomGeometry(irregular);
    passed &= expectTrue(
        hasIssue(irregularAnalysis, DicomGeometryIssueKind::NonUniformSpacing),
        "irregular spacing rejected");
    passed &= expectTrue(
        irregularAnalysis.canOverrideNonUniformSpacing(),
        "isolated non-uniform spacing can be overridden");

    auto inconsistentDirection = shuffled;
    inconsistentDirection[2].imagePositionPatient = {{0.5, 0.0, 2.0}};
    const auto directionAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentDirection);
    passed &= expectTrue(
        hasIssue(
            directionAnalysis,
            DicomGeometryIssueKind::InconsistentStackDirection),
        "changing stack direction rejected separately from spacing");
    passed &= expectTrue(
        !directionAnalysis.canOverrideNonUniformSpacing(),
        "changing stack direction cannot use spacing override");

    auto inconsistentOrientation = shuffled;
    inconsistentOrientation[1].imageOrientationPatient =
        {{0.0, 1.0, 0.0, -1.0, 0.0, 0.0}};
    const auto orientationAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentOrientation);
    passed &= expectTrue(
        hasIssue(
            orientationAnalysis,
            DicomGeometryIssueKind::InconsistentOrientation),
        "inconsistent orientation rejected");

    auto invalidOrientation = shuffled;
    invalidOrientation[0].imageOrientationPatient =
        {{1.0, 0.0, 0.0, 1.0, 0.0, 0.0}};
    const auto invalidOrientationAnalysis =
        radmarky::io::analyzeDicomGeometry(invalidOrientation);
    passed &= expectTrue(
        hasIssue(
            invalidOrientationAnalysis, DicomGeometryIssueKind::InvalidOrientation),
        "non-orthogonal in-plane cosines rejected");

    auto inconsistentDimensions = shuffled;
    inconsistentDimensions[1].columns = 65;
    const auto dimensionsAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentDimensions);
    passed &= expectTrue(
        hasIssue(
            dimensionsAnalysis, DicomGeometryIssueKind::InconsistentDimensions),
        "inconsistent dimensions rejected");

    auto inconsistentPixelSpacing = shuffled;
    inconsistentPixelSpacing[1].pixelSpacing = {{0.8, 0.75}};
    const auto pixelSpacingAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentPixelSpacing);
    passed &= expectTrue(
        hasIssue(
            pixelSpacingAnalysis,
            DicomGeometryIssueKind::InconsistentPixelSpacing),
        "inconsistent pixel spacing rejected");

    auto inconsistentSpacingMetadata = shuffled;
    for(auto& record : inconsistentSpacingMetadata)
    {
        record.spacingBetweenSlices = 2.0;
    }
    inconsistentSpacingMetadata[1].spacingBetweenSlices = 3.0;
    const auto spacingMetadataAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentSpacingMetadata);
    passed &= expectTrue(
        hasIssue(
            spacingMetadataAnalysis,
            DicomGeometryIssueKind::InconsistentSpacingMetadata),
        "inconsistent spacing metadata rejected");
    passed &= expectTrue(
        !spacingMetadataAnalysis.canOverrideSliceSpacing(),
        "changing spacing metadata cannot be overridden");

    auto spacingMetadataMismatch = shuffled;
    for(auto& record : spacingMetadataMismatch)
    {
        record.spacingBetweenSlices = 3.0;
    }
    const auto spacingMetadataMismatchAnalysis =
        radmarky::io::analyzeDicomGeometry(spacingMetadataMismatch);
    passed &= expectTrue(
        hasIssue(
            spacingMetadataMismatchAnalysis,
            DicomGeometryIssueKind::SpacingMetadataMismatch),
        "declared and position-derived spacing disagreement detected");
    passed &= expectTrue(
        spacingMetadataMismatchAnalysis.canOverrideSpacingMetadataMismatch()
            && spacingMetadataMismatchAnalysis.canOverrideSliceSpacing(),
        "isolated spacing metadata and position disagreement can be overridden");

    auto inconsistentFrame = shuffled;
    inconsistentFrame[1].frameOfReferenceUid = "9.8.7.6";
    const auto frameAnalysis =
        radmarky::io::analyzeDicomGeometry(inconsistentFrame);
    passed &= expectTrue(
        hasIssue(
            frameAnalysis,
            DicomGeometryIssueKind::InconsistentFrameOfReference),
        "inconsistent frame of reference rejected");

    auto repeatedSop = shuffled;
    repeatedSop[1].sopInstanceUid = repeatedSop[0].sopInstanceUid;
    const auto sopAnalysis = radmarky::io::analyzeDicomGeometry(repeatedSop);
    passed &= expectTrue(
        hasIssue(sopAnalysis, DicomGeometryIssueKind::DuplicateSlice),
        "duplicate SOP instance rejected");

    auto incomplete = shuffled;
    incomplete[1].imagePositionPatient.reset();
    const auto incompleteAnalysis = radmarky::io::analyzeDicomGeometry(incomplete);
    passed &= expectTrue(
        hasIssue(
            incompleteAnalysis, DicomGeometryIssueKind::MissingSpatialMetadata),
        "missing spatial metadata rejected");

    const auto singleFrame = radmarky::io::analyzeDicomGeometry(
        std::vector<radmarky::io::DicomFileRecord>{{recordAt(0.0, 0.0, 40)}});
    passed &= expectTrue(singleFrame.valid(), "single multi-frame candidate allowed");

    return passed ? 0 : 1;
}
