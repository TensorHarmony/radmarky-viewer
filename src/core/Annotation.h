#pragma once

#include "core/Volume.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
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
    [[nodiscard]] double opacity() const noexcept;
    void setOpacity(double opacity);
    [[nodiscard]] bool isVisible() const noexcept;
    void setVisible(bool visible) noexcept;

    // Label maps are voxel-indexed annotations. If their dimensions match,
    // use the anatomical image header. Scalar maps retain their physical header
    // and must already match the anatomical image.
    void conformGeometry(const ImageGeometry& primaryGeometry);
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
