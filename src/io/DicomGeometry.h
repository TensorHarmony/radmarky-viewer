#pragma once

#include "io/DicomSeries.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace radmarky::io
{

enum class DicomGeometryIssueKind
{
    MissingSpatialMetadata,
    InvalidOrientation,
    InconsistentOrientation,
    InconsistentDimensions,
    InconsistentPixelSpacing,
    InconsistentSpacingMetadata,
    SpacingMetadataMismatch,
    InconsistentFrameOfReference,
    DuplicateSlice,
    MissingSlices,
    NonUniformSpacing,
    InconsistentStackDirection,
};

struct DicomGeometryDiagnostic
{
    DicomGeometryIssueKind kind;
    std::string message;
};

struct DicomGeometryAnalysis
{
    std::vector<std::size_t> orderedIndices;
    std::vector<DicomGeometryDiagnostic> diagnostics;
    std::optional<double> sliceSpacingMillimetres;
    bool completeSpatialGeometry = false;
    bool gantryTilt = false;

    [[nodiscard]] bool valid() const noexcept
    {
        return diagnostics.empty();
    }

    [[nodiscard]] bool canOverrideNonUniformSpacing() const noexcept
    {
        return diagnostics.size() == 1
            && diagnostics.front().kind
                == DicomGeometryIssueKind::NonUniformSpacing;
    }

    [[nodiscard]] bool canOverrideMissingSlices() const noexcept
    {
        return diagnostics.size() == 1
            && diagnostics.front().kind == DicomGeometryIssueKind::MissingSlices;
    }

    [[nodiscard]] bool canOverrideSpacingMetadataMismatch() const noexcept
    {
        return diagnostics.size() == 1
            && diagnostics.front().kind
                == DicomGeometryIssueKind::SpacingMetadataMismatch;
    }

    [[nodiscard]] bool canOverrideSliceSpacing() const noexcept
    {
        return canOverrideMissingSlices() || canOverrideNonUniformSpacing()
            || canOverrideSpacingMetadataMismatch();
    }
};

[[nodiscard]] std::string_view dicomGeometryIssueCode(
    DicomGeometryIssueKind kind) noexcept;

[[nodiscard]] DicomGeometryAnalysis analyzeDicomGeometry(
    const std::vector<DicomFileRecord>& records);

[[nodiscard]] std::string formatDicomGeometryDiagnostics(
    const DicomGeometryAnalysis& analysis);

} // namespace radmarky::io
