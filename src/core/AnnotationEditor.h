#pragma once

#include "core/BrushGeometry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace radmarky::core
{

class Annotation;

enum class PaintOverMode
{
    AllLabels,
    OneLabel,
};

// Applies axial brush strokes to one label-map annotation. A stroke may
// contain many stamps but is committed as one undo command.
class AnnotationEditor
{
public:
    void setAnnotation(const std::shared_ptr<Annotation>& annotation);
    void clearAnnotation() noexcept;
    [[nodiscard]] const std::shared_ptr<Annotation>& annotation() const noexcept;

    void setActiveLabel(std::uint16_t label) noexcept;
    [[nodiscard]] std::uint16_t activeLabel() const noexcept;
    void setBrushRadius(int radius);
    [[nodiscard]] int brushRadius() const noexcept;
    void setBrushShape(BrushShape shape) noexcept;
    [[nodiscard]] BrushShape brushShape() const noexcept;
    void setPaintOver(PaintOverMode mode, std::uint16_t label = 0) noexcept;

    void beginStroke(bool erase);
    [[nodiscard]] bool stamp(const ImageGeometry::Vector& physicalPoint);
    [[nodiscard]] bool endStroke();
    void cancelStroke();
    [[nodiscard]] bool eraseConnectedComponentOnSlice(
        const ImageGeometry::Vector& physicalPoint);

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

private:
    struct StampCenter
    {
        BrushGridIndex grid{};
        double normalCoordinate = 0.0;
    };

    struct Change
    {
        std::size_t offset = 0;
        float before = 0.0F;
        float after = 0.0F;
    };
    using Stroke = std::vector<Change>;

    [[nodiscard]] bool apply(const Stroke& stroke, bool forward);
    std::shared_ptr<Annotation> annotation_;
    std::optional<OrthogonalSliceGeometry> sliceGeometry_;
    std::uint16_t activeLabel_ = 1;
    int brushRadius_ = 1;
    BrushShape brushShape_ = BrushShape::Square;
    PaintOverMode paintOverMode_ = PaintOverMode::AllLabels;
    std::uint16_t paintOverLabel_ = 0;
    bool strokeActive_ = false;
    bool erase_ = false;
    std::optional<StampCenter> previousStampCenter_;
    Stroke pending_;
    std::unordered_map<std::size_t, std::size_t> pendingLookup_;
    std::vector<Stroke> undoStack_;
    std::vector<Stroke> redoStack_;
};

} // namespace radmarky::core
