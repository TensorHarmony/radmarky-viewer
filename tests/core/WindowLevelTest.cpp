#include "core/Volume.h"
#include "core/WindowLevel.h"

#include <itkImage.h>

#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{

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

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

bool expectThrows(const auto& action, const std::string_view field)
{
    try
    {
        action();
        std::cerr << field << ": expected exception\n";
        return false;
    }
    catch(const std::exception&)
    {
        return true;
    }
}

} // namespace

int main()
{
    using radmarky::core::Volume;
    using radmarky::core::WindowLevel;
    using radmarky::core::WindowLevelPreset;

    bool passed = true;

    const auto fullRange = WindowLevel::fromIntensityRange(-1024.0, 3071.0);
    passed &= expectNear(fullRange.window(), 4095.0, "full-range window");
    passed &= expectNear(fullRange.level(), 1023.5, "full-range level");
    passed &= expectNear(fullRange.displayMinimum(), -1024.0, "display minimum");
    passed &= expectNear(fullRange.displayMaximum(), 3071.0, "display maximum");
    passed &= expectNear(fullRange.intensityMinimum(), -1024.0, "stored intensity min");
    passed &= expectNear(fullRange.intensityMaximum(), 3071.0, "stored intensity max");

    auto edited = fullRange;
    edited.set(400.0, 40.0);
    passed &= expectNear(edited.window(), 400.0, "edited window");
    passed &= expectNear(edited.level(), 40.0, "edited level");
    passed &= expectNear(edited.displayMinimum(), -160.0, "edited display min");
    passed &= expectNear(edited.displayMaximum(), 240.0, "edited display max");
    passed &= expectNear(
        edited.intensityMinimum(), -1024.0, "intensity min unchanged by set");
    edited.reset();
    passed &= expectNear(edited.window(), 4095.0, "reset window");
    passed &= expectNear(edited.level(), 1023.5, "reset level");

    const auto constantRange = WindowLevel::fromIntensityRange(50.0, 50.0);
    passed &= expectNear(constantRange.window(), 1.0, "constant image window");
    passed &= expectNear(constantRange.level(), 50.0, "constant image level");

    const auto lung = WindowLevel::fromPreset(
        WindowLevelPreset::CtLung, -1024.0, 3071.0);
    passed &= expectNear(lung.window(), 1500.0, "lung window");
    passed &= expectNear(lung.level(), -600.0, "lung level");
    passed &= expectNear(
        WindowLevel::fromPreset(WindowLevelPreset::CtSoftTissue, 0.0, 1.0).window(),
        400.0,
        "soft-tissue window");
    passed &= expectNear(
        WindowLevel::fromPreset(WindowLevelPreset::CtBone, 0.0, 1.0).level(),
        480.0,
        "bone level");
    passed &= expectNear(
        WindowLevel::fromPreset(WindowLevelPreset::CtBrain, 0.0, 1.0).window(),
        80.0,
        "brain window");
    passed &= expectNear(
        WindowLevel::fromPreset(WindowLevelPreset::FullRange, -10.0, 10.0).window(),
        20.0,
        "full-range preset window");

    const auto start = WindowLevel::fromIntensityRange(0.0, 100.0);
    const auto draggedLevel = start.dragged(0.1, 0.0);
    passed &= expectNear(draggedLevel.window(), 100.0, "horizontal drag keeps window");
    passed &= expectNear(draggedLevel.level(), 60.0, "horizontal drag changes level");
    const auto draggedWindow = start.dragged(0.0, 0.2);
    passed &= expectNear(draggedWindow.window(), 120.0, "vertical drag changes window");
    passed &= expectNear(draggedWindow.level(), 50.0, "vertical drag keeps level");
    const auto clampedWindow = start.dragged(0.0, -5.0);
    passed &= expectNear(
        clampedWindow.window(),
        WindowLevel::minimumWindow,
        "negative window drag clamps");

    passed &= expectThrows(
        [] { (void)WindowLevel::fromIntensityRange(1.0, 0.0); }, "inverted range");
    passed &= expectThrows(
        [] {
            (void)WindowLevel::fromIntensityRange(
                std::numeric_limits<double>::quiet_NaN(), 1.0);
        },
        "non-finite range");
    passed &= expectThrows(
        [&fullRange] {
            auto invalid = fullRange;
            invalid.set(0.0, 0.0);
        },
        "non-positive window");
    passed &= expectThrows(
        [&fullRange] {
            auto invalid = fullRange;
            invalid.set(10.0, std::numeric_limits<double>::infinity());
        },
        "non-finite level");
    passed &= expectThrows(
        [&start] {
            (void)start.dragged(std::numeric_limits<double>::quiet_NaN(), 0.0);
        },
        "non-finite drag");

    using Image = Volume::ImageType;
    auto image = Image::New();
    Image::RegionType region;
    region.SetSize({{3, 2, 1}});
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(-1024.0F);
    Image::IndexType bright{{1, 0, 0}};
    image->SetPixel(bright, 321.0F);
    const float* const buffer = image->GetBufferPointer();
    const float originalFirst = buffer[0];
    const float originalBright = image->GetPixel(bright);

    const Volume volume(image);
    passed &= expectNear(volume.scalarRange().minimum, -1024.0, "volume scalar min");
    passed &= expectNear(volume.scalarRange().maximum, 321.0, "volume scalar max");

    auto robustImage = Image::New();
    Image::RegionType robustRegion;
    robustRegion.SetSize({{1004, 1, 1}});
    robustImage->SetRegions(robustRegion);
    robustImage->Allocate();
    auto* const robustBuffer = robustImage->GetBufferPointer();
    robustBuffer[0] = -1.0e9F;
    robustBuffer[1] = std::numeric_limits<float>::quiet_NaN();
    for(std::size_t index = 2; index < 502; ++index)
    {
        robustBuffer[index] = 10.0F;
    }
    for(std::size_t index = 502; index < 1002; ++index)
    {
        robustBuffer[index] = 20.0F;
    }
    robustBuffer[1002] = std::numeric_limits<float>::infinity();
    robustBuffer[1003] = 1.0e9F;
    const Volume robustVolume(robustImage);
    const auto robustRange = robustVolume.robustScalarRange();
    passed &= expectNear(robustRange.minimum, 10.0, "robust scalar min");
    passed &= expectNear(robustRange.maximum, 20.0, "robust scalar max");

    auto display = WindowLevel::fromIntensityRange(
        volume.scalarRange().minimum, volume.scalarRange().maximum);
    display.set(80.0, 40.0);
    passed &= expectNear(buffer[0], originalFirst, "volume voxel unchanged after W/L");
    passed &= expectNear(
        image->GetPixel(bright),
        originalBright,
        "volume bright voxel unchanged after W/L");
    passed &= expectNear(volume.image().GetPixel(bright), 321.0, "owned image unchanged");

    return passed ? 0 : 1;
}
