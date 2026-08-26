#include "core/AnnotationEditor.h"

#include "core/Annotation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace radmarky::core
{

void AnnotationEditor::setAnnotation(
    const std::shared_ptr<Annotation>& annotation)
{
    if(annotation && annotation->kind() != AnnotationKind::LabelMap)
    {
        throw std::invalid_argument("Only label-map annotations can be edited");
    }
    annotation_ = annotation;
    pending_.clear();
    pendingLookup_.clear();
    undoStack_.clear();
    redoStack_.clear();
    strokeActive_ = false;
    previousStampCenter_.reset();
}

void AnnotationEditor::clearAnnotation() noexcept
{
    annotation_.reset();
    pending_.clear();
    pendingLookup_.clear();
    undoStack_.clear();
    redoStack_.clear();
    strokeActive_ = false;
    previousStampCenter_.reset();
}

const std::shared_ptr<Annotation>& AnnotationEditor::annotation() const noexcept
{
    return annotation_;
}

void AnnotationEditor::setActiveLabel(const std::uint16_t label) noexcept
{
    activeLabel_ = label;
}

std::uint16_t AnnotationEditor::activeLabel() const noexcept
{
    return activeLabel_;
}

void AnnotationEditor::setBrushRadius(const int radius)
{
    if(radius < 1 || radius > 100)
    {
        throw std::invalid_argument("Brush size must be from 1 to 100 voxels");
    }
    brushRadius_ = radius;
}

int AnnotationEditor::brushRadius() const noexcept
{
    return brushRadius_;
}

void AnnotationEditor::setBrushShape(const BrushShape shape) noexcept
{
    brushShape_ = shape;
}

BrushShape AnnotationEditor::brushShape() const noexcept
{
    return brushShape_;
}

void AnnotationEditor::setPaintOver(
    const PaintOverMode mode, const std::uint16_t label) noexcept
{
    paintOverMode_ = mode;
    paintOverLabel_ = label;
}

void AnnotationEditor::beginStroke(const bool erase)
{
    if(!annotation_)
    {
        throw std::logic_error("A label-map annotation must be selected for editing");
    }
    pending_.clear();
    pendingLookup_.clear();
    strokeActive_ = true;
    erase_ = erase;
    previousStampCenter_.reset();
}

bool AnnotationEditor::stamp(const ImageGeometry::Vector& physicalPoint)
{
    if(!strokeActive_ || !annotation_)
    {
        return false;
    }
    auto& volume = annotation_->editableVolume();
    auto& image = volume.image();
    const auto continuous = volume.geometry().physicalToContinuousIndex(physicalPoint);
    for(const double component : continuous)
    {
        if(!std::isfinite(component))
        {
            return false;
        }
    }

    const auto& dimensions = volume.geometry().dimensions();
    const std::array<long, 3> center{{
        std::lround(continuous[0]),
        std::lround(continuous[1]),
        std::lround(continuous[2]),
    }};
    const long centerX = center[0];
    const long centerY = center[1];
    const long axial = center[2];
    if(axial < 0 || axial >= static_cast<long>(dimensions[2]))
    {
        return false;
    }

    const float replacement = erase_ ? 0.0F : static_cast<float>(activeLabel_);
    bool changed = false;
    // Odd sizes are centered on the selected voxel. Even sizes include the
    // selected voxel and extend one extra voxel in the positive direction.
    const BrushFootprint footprint(brushRadius_, brushShape_);
    const int firstOffset = footprint.firstOffset();
    const int lastOffset = footprint.lastOffset();
    const auto sizeX = dimensions[0];
    const auto sizeY = dimensions[1];
    float* const values = image.GetBufferPointer();

    auto lineStart = previousStampCenter_.value_or(center);
    if(lineStart[2] != axial)
    {
        lineStart = center;
    }
    const long lineDx = centerX - lineStart[0];
    const long lineDy = centerY - lineStart[1];
    const long lineSteps = std::max(std::abs(lineDx), std::abs(lineDy));
    previousStampCenter_ = center;

    for(long step = 0; step <= lineSteps; ++step)
    {
        const long stampX = lineSteps == 0
            ? centerX
            : lineStart[0] + std::lround(
                static_cast<double>(lineDx) * static_cast<double>(step)
                / static_cast<double>(lineSteps));
        const long stampY = lineSteps == 0
            ? centerY
            : lineStart[1] + std::lround(
                static_cast<double>(lineDy) * static_cast<double>(step)
                / static_cast<double>(lineSteps));
        for(int dy = firstOffset; dy <= lastOffset; ++dy)
        {
            for(int dx = firstOffset; dx <= lastOffset; ++dx)
            {
                if(!footprint.contains(dx, dy))
                {
                    continue;
                }
                const long x = stampX + dx;
                const long y = stampY + dy;
                if(x < 0 || y < 0 || x >= static_cast<long>(sizeX)
                   || y >= static_cast<long>(sizeY))
                {
                    continue;
                }
                const std::size_t offset = static_cast<std::size_t>(x)
                    + sizeX * (static_cast<std::size_t>(y)
                               + sizeY * static_cast<std::size_t>(axial));
                const float current = values[offset];
                const bool canPaint = paintOverMode_ == PaintOverMode::AllLabels
                    || (paintOverMode_ == PaintOverMode::OneLabel
                        && current == static_cast<float>(paintOverLabel_));
                if(!canPaint)
                {
                    continue;
                }
                if(values[offset] == replacement)
                {
                    continue;
                }
                const auto existing = pendingLookup_.find(offset);
                if(existing == pendingLookup_.end())
                {
                    pending_.push_back({offset, values[offset], replacement});
                    pendingLookup_.emplace(offset, pending_.size() - 1);
                }
                else
                {
                    pending_[existing->second].after = replacement;
                }
                annotation_->updateLabelLedger(
                    static_cast<std::uint16_t>(std::lround(values[offset])),
                    static_cast<std::uint16_t>(std::lround(replacement)));
                values[offset] = replacement;
                changed = true;
            }
        }
    }
    if(changed)
    {
        image.Modified();
    }
    return changed;
}

bool AnnotationEditor::endStroke()
{
    if(!strokeActive_)
    {
        return false;
    }
    strokeActive_ = false;
    previousStampCenter_.reset();
    pending_.erase(
        std::remove_if(
            pending_.begin(), pending_.end(), [](const Change& change) {
                return change.before == change.after;
            }),
        pending_.end());
    if(pending_.empty())
    {
        pendingLookup_.clear();
        return false;
    }
    undoStack_.push_back(std::move(pending_));
    annotation_->markModified();
    pending_.clear();
    pendingLookup_.clear();
    redoStack_.clear();
    return true;
}

void AnnotationEditor::cancelStroke()
{
    if(annotation_)
    {
        static_cast<void>(apply(pending_, false));
    }
    pending_.clear();
    pendingLookup_.clear();
    strokeActive_ = false;
    previousStampCenter_.reset();
}

bool AnnotationEditor::eraseConnectedComponentOnSlice(
    const ImageGeometry::Vector& physicalPoint)
{
    if(!annotation_ || strokeActive_)
    {
        return false;
    }

    auto& volume = annotation_->editableVolume();
    auto& image = volume.image();
    const auto continuous =
        volume.geometry().physicalToContinuousIndex(physicalPoint);
    for(const double component : continuous)
    {
        if(!std::isfinite(component))
        {
            return false;
        }
    }

    const auto& dimensions = volume.geometry().dimensions();
    const std::array<long, 3> seed{{
        std::lround(continuous[0]),
        std::lround(continuous[1]),
        std::lround(continuous[2]),
    }};
    for(std::size_t axis = 0; axis < seed.size(); ++axis)
    {
        if(seed[axis] < 0
           || seed[axis] >= static_cast<long>(dimensions[axis]))
        {
            return false;
        }
    }

    const std::size_t sizeX = dimensions[0];
    const std::size_t sizeY = dimensions[1];
    const std::size_t planeSize = sizeX * sizeY;
    const std::size_t seedOffset = static_cast<std::size_t>(seed[0])
        + sizeX * static_cast<std::size_t>(seed[1])
        + planeSize * static_cast<std::size_t>(seed[2]);
    float* const values = image.GetBufferPointer();
    const float label = values[seedOffset];
    if(label == 0.0F || !std::isfinite(label))
    {
        return false;
    }

    Stroke erasedComponent;
    std::vector<std::size_t> pendingOffsets;
    const auto eraseOffset = [&](const std::size_t offset) {
        if(values[offset] != label)
        {
            return;
        }
        erasedComponent.push_back({offset, label, 0.0F});
        pendingOffsets.push_back(offset);
        annotation_->updateLabelLedger(
            static_cast<std::uint16_t>(std::lround(label)), 0);
        // Clearing on discovery also serves as the flood fill's visited mark.
        values[offset] = 0.0F;
    };
    eraseOffset(seedOffset);

    for(std::size_t next = 0; next < pendingOffsets.size(); ++next)
    {
        const std::size_t offset = pendingOffsets[next];
        const std::size_t inPlane = offset % planeSize;
        const std::size_t y = inPlane / sizeX;
        const std::size_t x = inPlane % sizeX;
        if(x > 0) eraseOffset(offset - 1);
        if(x + 1 < sizeX) eraseOffset(offset + 1);
        if(y > 0) eraseOffset(offset - sizeX);
        if(y + 1 < sizeY) eraseOffset(offset + sizeX);
        if(x > 0 && y > 0) eraseOffset(offset - sizeX - 1);
        if(x + 1 < sizeX && y > 0) eraseOffset(offset - sizeX + 1);
        if(x > 0 && y + 1 < sizeY) eraseOffset(offset + sizeX - 1);
        if(x + 1 < sizeX && y + 1 < sizeY)
        {
            eraseOffset(offset + sizeX + 1);
        }
    }

    image.Modified();
    undoStack_.push_back(std::move(erasedComponent));
    redoStack_.clear();
    annotation_->markModified();
    return true;
}

bool AnnotationEditor::canUndo() const noexcept { return !undoStack_.empty(); }
bool AnnotationEditor::canRedo() const noexcept { return !redoStack_.empty(); }

bool AnnotationEditor::apply(const Stroke& stroke, const bool forward)
{
    if(!annotation_ || stroke.empty())
    {
        return false;
    }
    auto& image = annotation_->editableVolume().image();
    float* const values = image.GetBufferPointer();
    for(const auto& change : stroke)
    {
        const float replacement = forward ? change.after : change.before;
        annotation_->updateLabelLedger(
            static_cast<std::uint16_t>(std::lround(values[change.offset])),
            static_cast<std::uint16_t>(std::lround(replacement)));
        values[change.offset] = replacement;
    }
    image.Modified();
    return true;
}

bool AnnotationEditor::undo()
{
    if(!canUndo()) return false;
    Stroke stroke = std::move(undoStack_.back());
    undoStack_.pop_back();
    static_cast<void>(apply(stroke, false));
    annotation_->markModified();
    redoStack_.push_back(std::move(stroke));
    return true;
}

bool AnnotationEditor::redo()
{
    if(!canRedo()) return false;
    Stroke stroke = std::move(redoStack_.back());
    redoStack_.pop_back();
    static_cast<void>(apply(stroke, true));
    annotation_->markModified();
    undoStack_.push_back(std::move(stroke));
    return true;
}

} // namespace radmarky::core
