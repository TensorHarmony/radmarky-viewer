#include "core/Annotation.h"
#include "core/Volume.h"
#include "io/NiftiReader.h"
#include "io/NiftiWriter.h"

#include <itkImageIOFactory.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-5)
{
    if(std::abs(actual - expected) <= tolerance)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

itk::IOComponentEnum componentType(const std::filesystem::path& filePath)
{
    const auto utf8 = filePath.u8string();
    const std::string fileName(utf8.begin(), utf8.end());
    auto imageIo = itk::ImageIOFactory::CreateImageIO(
        fileName.c_str(), itk::ImageIOFactory::IOFileModeEnum::ReadMode);
    if(!imageIo)
    {
        throw std::runtime_error("Unable to inspect saved NIfTI component type");
    }
    imageIo->SetFileName(fileName);
    imageIo->ReadImageInformation();
    return imageIo->GetComponentType();
}

} // namespace

int main()
{
    using radmarky::core::Annotation;
    using radmarky::core::AnnotationKind;
    using radmarky::core::Volume;
    using Image = Volume::ImageType;

    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const auto filePath = std::filesystem::temp_directory_path()
        / ("radmarky-annotation-roundtrip-" + std::to_string(unique)
           + ".nii.gz");
    const auto compactFilePath = std::filesystem::temp_directory_path()
        / ("radmarky-annotation-uint8-" + std::to_string(unique)
           + ".nii.gz");
    try
    {
        auto image = Image::New();
        Image::SizeType size{{5, 4, 3}};
        Image::RegionType region;
        region.SetSize(size);
        image->SetRegions(region);
        Image::SpacingType spacing;
        spacing[0] = 0.8;
        spacing[1] = 1.25;
        spacing[2] = 2.5;
        image->SetSpacing(spacing);
        Image::PointType origin;
        origin[0] = 12.0;
        origin[1] = -30.0;
        origin[2] = 4.5;
        image->SetOrigin(origin);
        Image::DirectionType direction;
        direction.SetIdentity();
        direction[0][0] = 0.0;
        direction[0][1] = -1.0;
        direction[1][0] = 1.0;
        direction[1][1] = 0.0;
        image->SetDirection(direction);
        image->Allocate();
        image->FillBuffer(0.0F);
        const Image::IndexType firstMarker{{0, 1, 1}};
        const Image::IndexType secondMarker{{4, 2, 2}};
        image->SetPixel(firstMarker, 1.0F);
        image->SetPixel(secondMarker, 65535.0F);

        Annotation annotation(
            "roundtrip.nii.gz",
            {},
            std::make_shared<Volume>(image),
            AnnotationKind::LabelMap);
        radmarky::io::NiftiWriter::writeLabelMap(annotation, filePath);

        bool passed = true;
        passed &= componentType(filePath) == itk::IOComponentEnum::USHORT;
        passed &= radmarky::io::NiftiReader::componentKind(filePath)
            == radmarky::io::NiftiReader::ComponentKind::Integer;
        const auto reloaded = radmarky::io::NiftiReader::read(filePath);
        const auto& geometry = reloaded->geometry();
        passed &= geometry.dimensions()
            == radmarky::core::ImageGeometry::Dimensions{{5, 4, 3}};
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            passed &= expectNear(
                geometry.spacing()[axis], spacing[axis], "spacing");
            passed &= expectNear(
                geometry.origin()[axis], origin[axis], "origin");
            for(std::size_t column = 0; column < 3; ++column)
            {
                passed &= expectNear(
                    geometry.direction()[axis][column],
                    direction[axis][column],
                    "direction");
            }
        }
        passed &= reloaded->image().GetPixel(firstMarker) == 1.0F;
        passed &= reloaded->image().GetPixel(secondMarker) == 65535.0F;

        image->SetPixel(secondMarker, 255.0F);
        Annotation compactAnnotation(
            "compact.nii.gz",
            {},
            std::make_shared<Volume>(image),
            AnnotationKind::LabelMap);
        radmarky::io::NiftiWriter::writeLabelMap(
            compactAnnotation, compactFilePath);
        passed &= componentType(compactFilePath) == itk::IOComponentEnum::UCHAR;
        const auto compactReloaded =
            radmarky::io::NiftiReader::read(compactFilePath);
        passed &= compactReloaded->image().GetPixel(secondMarker) == 255.0F;

        bool rejectedScalar = false;
        try
        {
            Annotation scalar(
                "scalar.nii", {}, std::make_shared<Volume>(image),
                AnnotationKind::ScalarMap);
            radmarky::io::NiftiWriter::writeLabelMap(scalar, filePath);
        }
        catch(const std::invalid_argument&)
        {
            rejectedScalar = true;
        }
        passed &= rejectedScalar;

        std::filesystem::remove(filePath);
        std::filesystem::remove(compactFilePath);
        return passed ? 0 : 1;
    }
    catch(const std::exception& exception)
    {
        std::filesystem::remove(filePath);
        std::filesystem::remove(compactFilePath);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
