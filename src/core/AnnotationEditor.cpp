#include "core/AnnotationEditor.h"

#include "core/Annotation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace radmarky::core
{
namespace
{

std::optional<std::size_t> nearestVoxelOffset(
    const ImageGeometry& geometry,
    const ImageGeometry::Vector& physicalPoint)
{
    const auto continuous = geometry.physicalToContinuousIndex(physicalPoint);
    std::array<long, 3> index{};
    const auto& dimensions = geometry.dimensions();
    for(std::size_t axis = 0; axis < index.size(); ++axis)
    {
        if(!std::isfinite(continuous[axis]))
        {
            return std::nullopt;
        }
        index[axis] = std::lround(continuous[axis]);
        if(index[axis] < 0
           || index[axis] >= static_cast<long>(dimensions[axis]))
        {
            return std::nullopt;
        }
    }
    return static_cast<std::size_t>(index[0])
        + dimensions[0] * (static_cast<std::size_t>(index[1])
                           + dimensions[1] * static_cast<std::size_t>(index[2]));
}

bool belongsOnlyToCurrentAxialSlice(
    const ImageGeometry& geometry,
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& samplePoint,
    const std::size_t offset)
{
    // With an oblique native grid, a voxel near a patient-axial slice boundary
    // can be the nearest-neighbor sample on two displayed slices. Editing that
    // voxel would make a 2-D brush appear to affect an adjacent slice. Leave
    // such ambiguous boundary voxels untouched instead.
    for(const double sign : {-1.0, 1.0})
    {
        auto adjacentPoint = samplePoint;
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            adjacentPoint[axis] += sign * slice.sliceStep()
                * slice.normalDirectionLps()[axis];
        }
        if(nearestVoxelOffset(geometry, adjacentPoint) == offset)
        {
            return false;
        }
    }
    return true;
}

} // namespace

void AnnotationEditor::setAnnotation(
    const std::shared_ptr<Annotation>& annotation,
    const SliceAlignment alignment)
{
    if(annotation && annotation->kind() != AnnotationKind::LabelMap)
    {
        throw std::invalid_argument("Only label-map annotations can be edited");
    }
    annotation_ = annotation;
    if(annotation_)
    {
        sliceGeometry_ = OrthogonalSliceGeometry::fromImageGeometry(
            annotation_->volume().geometry(), SliceOrientation::Axial, alignment);
    }
    else
    {
        sliceGeometry_.reset();
    }
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
    sliceGeometry_.reset();
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
    if(!strokeActive_ || !annotation_ || !sliceGeometry_)
    {
        return false;
    }
    auto& volume = annotation_->editableVolume();
    auto& image = volume.image();
    const auto& geometry = volume.geometry();
    const auto& slice = *sliceGeometry_;
    const auto center = brushGridIndex(slice, physicalPoint);
    if(!center)
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
    float* const values = image.GetBufferPointer();

    const double normalCoordinate = slice.normalCoordinate(physicalPoint);
    auto lineStart = previousStampCenter_.value_or(
        StampCenter{*center, normalCoordinate});
    const double planeTolerance =
        1.0e-6 * std::max(1.0, slice.sliceStep());
    if(std::abs(lineStart.normalCoordinate - normalCoordinate) > planeTolerance)
    {
        lineStart = StampCenter{*center, normalCoordinate};
    }
    const long lineDx = (*center)[0] - lineStart.grid[0];
    const long lineDy = (*center)[1] - lineStart.grid[1];
    const long lineSteps = std::max(std::abs(lineDx), std::abs(lineDy));
    previousStampCenter_ = StampCenter{*center, normalCoordinate};

    for(long step = 0; step <= lineSteps; ++step)
    {
        const long stampHorizontal = lineSteps == 0
            ? (*center)[0]
            : lineStart.grid[0] + std::lround(
                static_cast<double>(lineDx) * static_cast<double>(step)
                / static_cast<double>(lineSteps));
        const long stampVertical = lineSteps == 0
            ? (*center)[1]
            : lineStart.grid[1] + std::lround(
                static_cast<double>(lineDy) * static_cast<double>(step)
                / static_cast<double>(lineSteps));
        const BrushGridIndex stampCenter{{stampHorizontal, stampVertical}};
        for(int dy = firstOffset; dy <= lastOffset; ++dy)
        {
            for(int dx = firstOffset; dx <= lastOffset; ++dx)
            {
                if(!footprint.contains(dx, dy))
                {
                    continue;
                }
                const auto samplePoint = brushPointOnSliceGrid(
                    slice, stampCenter, physicalPoint,
                    static_cast<double>(dx), static_cast<double>(dy));
                const auto offset = nearestVoxelOffset(geometry, samplePoint);
                if(!offset
                   || !belongsOnlyToCurrentAxialSlice(
                       geometry, slice, samplePoint, *offset))
                {
                    continue;
                }
                const float current = values[*offset];
                const bool canPaint = paintOverMode_ == PaintOverMode::AllLabels
                    || (paintOverMode_ == PaintOverMode::OneLabel
                        && current == static_cast<float>(paintOverLabel_));
                if(!canPaint)
                {
                    continue;
                }
                if(values[*offset] == replacement)
                {
                    continue;
                }
                const auto existing = pendingLookup_.find(*offset);
                if(existing == pendingLookup_.end())
                {
                    pending_.push_back({*offset, values[*offset], replacement});
                    pendingLookup_.emplace(*offset, pending_.size() - 1);
                }
                else
                {
                    pending_[existing->second].after = replacement;
                }
                annotation_->updateLabelLedger(
                    static_cast<std::uint16_t>(std::lround(values[*offset])),
                    static_cast<std::uint16_t>(std::lround(replacement)));
                values[*offset] = replacement;
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
    if(!annotation_ || !sliceGeometry_ || strokeActive_)
    {
        return false;
    }

    auto& volume = annotation_->editableVolume();
    auto& image = volume.image();
    const auto& geometry = volume.geometry();
    const auto& slice = *sliceGeometry_;
    const auto seed = brushGridIndex(slice, physicalPoint);
    if(!seed)
    {
        return false;
    }
    const auto seedPoint = brushPointOnSliceGrid(
        slice, *seed, physicalPoint, 0.0, 0.0);
    const auto seedOffset = nearestVoxelOffset(geometry, seedPoint);
    if(!seedOffset)
    {
        return false;
    }
    float* const values = image.GetBufferPointer();
    const float label = values[*seedOffset];
    if(label == 0.0F || !std::isfinite(label))
    {
        return false;
    }

    const std::size_t width = slice.width();
    const std::size_t height = slice.height();
    std::vector<std::uint8_t> visited(width * height, 0U);
    std::vector<BrushGridIndex> pendingCells{*seed};
    std::unordered_set<std::size_t> componentOffsets;
    for(std::size_t next = 0; next < pendingCells.size(); ++next)
    {
        const auto cell = pendingCells[next];
        if(cell[0] < 0 || cell[1] < 0
           || cell[0] >= static_cast<long>(width)
           || cell[1] >= static_cast<long>(height))
        {
            continue;
        }
        const std::size_t cellOffset = static_cast<std::size_t>(cell[0])
            + width * static_cast<std::size_t>(cell[1]);
        if(visited[cellOffset] != 0U)
        {
            continue;
        }
        visited[cellOffset] = 1U;
        const auto samplePoint = brushPointOnSliceGrid(
            slice, cell, physicalPoint, 0.0, 0.0);
        const auto offset = nearestVoxelOffset(geometry, samplePoint);
        if(!offset || values[*offset] != label
           || !belongsOnlyToCurrentAxialSlice(
               geometry, slice, samplePoint, *offset))
        {
            continue;
        }
        componentOffsets.insert(*offset);
        for(long dy = -1; dy <= 1; ++dy)
        {
            for(long dx = -1; dx <= 1; ++dx)
            {
                if(dx != 0 || dy != 0)
                {
                    pendingCells.push_back({{cell[0] + dx, cell[1] + dy}});
                }
            }
        }
    }

    Stroke erasedComponent;
    erasedComponent.reserve(componentOffsets.size());
    for(const std::size_t offset : componentOffsets)
    {
        erasedComponent.push_back({offset, label, 0.0F});
        annotation_->updateLabelLedger(
            static_cast<std::uint16_t>(std::lround(label)), 0);
        values[offset] = 0.0F;
    }
    if(erasedComponent.empty())
    {
        return false;
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
