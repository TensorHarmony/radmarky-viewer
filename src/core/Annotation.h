#pragma once

#include "core/OrthogonalSliceGeometry.h"
#include "core/Volume.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace radmarky::core
{

class AnnotationEditor;

enum class AnnotationKind
{
    LabelMap,
    ScalarMap,
};

class Annotation
{
public:
    Annotation(
        std::string name,
        std::filesystem::path sourcePath,
        std::shared_ptr<Volume> volume,
        AnnotationKind kind);

    [[nodiscard]] static std::shared_ptr<Annotation> createBlankLabelMap(
        std::string name, const Volume& primaryVolume);

    [[nodiscard]] const std::string& name() const noexcept;
    void setName(std::string name);
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept;
    void setSourcePath(std::filesystem::path sourcePath);
    [[nodiscard]] bool isModified() const noexcept;
    void markSaved() noexcept;
    [[nodiscard]] const Volume& volume() const noexcept;
    [[nodiscard]] AnnotationKind kind() const noexcept;
    [[nodiscard]] std::vector<std::uint16_t> labelValues() const;
    [[nodiscard]] std::optional<ImageGeometry::Vector>
    nearestAxialSlicePointContainingLabel(
        std::uint16_t label,
        const ImageGeometry::Vector& currentPhysical,
        SliceAlignment alignment = SliceAlignment::Patient) const;
    [[nodiscard]] double opacity() const noexcept;
    void setOpacity(double opacity);
    [[nodiscard]] bool isVisible() const noexcept;
    void setVisible(bool visible) noexcept;

    // Annotation storage remains in the source image grid. Patient-coordinate
    // display and editing must not resample or rewrite the stored label map.
    void conformGeometry(const ImageGeometry& primaryGeometry) const;
    // Explicit recovery for a voxel-indexed mask whose physical header is known
    // to be absent or incorrect. Dimensions must already match the primary.
    void assumePrimaryGeometry(const ImageGeometry& primaryGeometry);
    void verifyGeometry(const ImageGeometry& primaryGeometry) const;

private:
    friend class AnnotationEditor;

    [[nodiscard]] Volume& editableVolume() noexcept;
    void validateLabelValuesAndBuildLedger();
    void updateLabelLedger(std::uint16_t before, std::uint16_t after);
    void markModified() noexcept;

    std::string name_;
    std::filesystem::path sourcePath_;
    std::shared_ptr<Volume> volume_;
    AnnotationKind kind_ = AnnotationKind::LabelMap;
    std::unordered_map<std::uint16_t, std::size_t> labelVoxelCounts_;
    double opacity_ = 0.5;
    bool visible_ = true;
    bool modified_ = false;
};

} // namespace radmarky::core
