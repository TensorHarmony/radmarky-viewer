#include "core/OrthogonalSliceGeometry.h"
#include "core/Volume.h"

#include <itkImage.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-6)
{
    if(std::abs(actual - expected) <= tolerance)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

} // namespace

int main()
{
    using radmarky::core::OrthogonalSliceGeometry;
    using radmarky::core::SliceOrientation;
    using Volume = radmarky::core::Volume;
    using Image = Volume::ImageType;

    auto image = Image::New();
    Image::SizeType size{{33, 33, 33}};
    Image::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    Image::SpacingType spacing;
    spacing[0] = 0.5;
    spacing[1] = 1.5;
    spacing[2] = 5.0;
    image->SetSpacing(spacing);
    image->Allocate();
    image->FillBuffer(0.0F);
    const Image::IndexType centerIndex{{16, 16, 16}};
    image->SetPixel(centerIndex, 2304.0F);

    const Volume volume(image);
    const auto center = volume.geometry().indexToPhysical({{16.0, 16.0, 16.0}});
    const std::array<std::size_t, 5> expectedSideLengths{{1, 3, 5, 9, 17}};
    const std::array<std::size_t, 5> expectedPixelCounts{{1, 9, 25, 81, 289}};
    bool passed = true;

    for(const auto orientation : {
            SliceOrientation::Axial,
            SliceOrientation::Sagittal,
            SliceOrientation::Coronal})
    {
        const auto slice = OrthogonalSliceGeometry::fromImageGeometry(
            volume.geometry(), orientation);
        for(int radius = 1; radius <= 5; ++radius)
        {
            const auto expectedCount =
                expectedPixelCounts[static_cast<std::size_t>(radius - 1)];
            passed &= expectTrue(
                Volume::samplingSideLength(radius)
                    == expectedSideLengths[static_cast<std::size_t>(radius - 1)],
                "sampling side length");
            passed &= expectTrue(
                Volume::samplingPixelCount(radius) == expectedCount,
                "sampling pixel count");
            const auto sample = volume.sampleMeanPhysical(center, slice, radius);
            passed &= expectTrue(sample.has_value(), "center mean sample exists");
            if(sample)
            {
                const double expectedSampleMean = static_cast<float>(
                    2304.0 / static_cast<double>(expectedCount));
                passed &= expectNear(
                    sample->value,
                    expectedSampleMean,
                    "anisotropic mean uses distinct footprint voxels");
                passed &= expectTrue(
                    sample->index
                        == std::array<std::size_t, 3>{{16, 16, 16}},
                    "mean preserves cursor index");
            }
            const auto statistics =
                volume.sampleStatisticsPhysical(center, slice, radius);
            passed &= expectTrue(
                statistics.has_value(), "center statistics sample exists");
            if(statistics)
            {
                passed &= expectNear(
                    statistics->maximum, 2304.0, "statistics maximum");
                passed &= expectNear(
                    statistics->mean,
                    2304.0 / static_cast<double>(expectedCount),
                    "statistics mean");
                const double expectedMedian = radius == 1 ? 2304.0 : 0.0;
                passed &= expectNear(
                    statistics->median, expectedMedian, "statistics median");
                passed &= expectNear(
                    statistics->minimum, expectedMedian, "statistics minimum");
            }
        }
    }

    const auto axial = OrthogonalSliceGeometry::fromImageGeometry(
        volume.geometry(), SliceOrientation::Axial);
    const auto edge = volume.geometry().indexToPhysical({{0.0, 0.0, 16.0}});
    image->SetPixel(centerIndex, 0.0F);
    const Image::IndexType edgeIndex{{0, 0, 16}};
    image->SetPixel(edgeIndex, 40.0F);
    const auto edgeSample = volume.sampleMeanPhysical(edge, axial, 2);
    passed &= expectTrue(edgeSample.has_value(), "edge mean sample exists");
    if(edgeSample)
    {
        passed &= expectNear(
            edgeSample->value, 10.0, "edge mean clips to four valid pixels");
    }
    const auto edgeStatistics = volume.sampleStatisticsPhysical(edge, axial, 2);
    passed &= expectTrue(
        edgeStatistics.has_value(), "edge statistics sample exists");
    if(edgeStatistics)
    {
        passed &= expectNear(edgeStatistics->maximum, 40.0, "edge sample maximum");
        passed &= expectNear(edgeStatistics->mean, 10.0, "edge statistics mean");
        passed &= expectNear(
            edgeStatistics->median, 0.0, "edge sample median");
        passed &= expectNear(edgeStatistics->minimum, 0.0, "edge sample minimum");
    }

    bool invalidRadiusRejected = false;
    try
    {
        static_cast<void>(volume.sampleMeanPhysical(center, axial, 0));
    }
    catch(const std::invalid_argument&)
    {
        invalidRadiusRejected = true;
    }
    passed &= expectTrue(invalidRadiusRejected, "invalid radius rejected");

    return passed ? 0 : 1;
}
