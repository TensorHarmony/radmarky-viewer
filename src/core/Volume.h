#pragma once

#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"

#include <itkImage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radmarky::core
{

class Volume
{
public:
    using ImageType = itk::Image<float, 3>;

    struct VoxelSample
    {
        std::array<std::size_t, 3> index{};
        float value = 0.0F;
    };

    struct IntensityStatistics
    {
        double maximum = 0.0;
        double mean = 0.0;
        double median = 0.0;
        double minimum = 0.0;
    };

    struct ScalarRange
    {
        double minimum = 0.0;
        double maximum = 0.0;
    };

    struct DicomMetadataEntry
    {
        std::string key;
        std::string value;

        [[nodiscard]] bool operator==(const DicomMetadataEntry&) const = default;
    };

    explicit Volume(ImageType::Pointer image);
    Volume(ImageType::Pointer image, ScalarRange scalarRange);

    [[nodiscard]] ImageType& image() noexcept;
    [[nodiscard]] const ImageType& image() const noexcept;
    [[nodiscard]] const ImageGeometry& geometry() const noexcept;
    void setGeometry(const ImageGeometry& geometry);
    [[nodiscard]] ScalarRange scalarRange() const noexcept;
    [[nodiscard]] ScalarRange robustScalarRange() const;
    [[nodiscard]] bool hasDisplayRgb() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>& displayRgb() const noexcept;
    void setDisplayRgb(std::vector<std::uint8_t> rgb);
    [[nodiscard]] const std::vector<DicomMetadataEntry>& dicomMetadata() const
        noexcept;
    void setDicomMetadata(std::vector<DicomMetadataEntry> metadata);
    [[nodiscard]] std::optional<VoxelSample> sampleNearestPhysical(
        const ImageGeometry::Vector& point) const;
    [[nodiscard]] std::optional<VoxelSample> sampleMeanPhysical(
        const ImageGeometry::Vector& point,
        const OrthogonalSliceGeometry& slice,
        int samplingRadius) const;
    [[nodiscard]] std::optional<IntensityStatistics> sampleStatisticsPhysical(
        const ImageGeometry::Vector& point,
        const OrthogonalSliceGeometry& slice,
        int samplingRadius) const;
    [[nodiscard]] static std::size_t samplingSideLength(int samplingRadius);
    [[nodiscard]] static std::size_t samplingPixelCount(int samplingRadius);

private:
    [[nodiscard]] static ImageGeometry geometryFor(const ImageType::Pointer& image);
    [[nodiscard]] static ScalarRange scalarRangeFor(const ImageType::Pointer& image);

    ImageType::Pointer image_;
    ImageGeometry geometry_;
    ScalarRange scalarRange_{};
    std::vector<std::uint8_t> displayRgb_;
    std::vector<DicomMetadataEntry> dicomMetadata_;
};

} // namespace radmarky::core
