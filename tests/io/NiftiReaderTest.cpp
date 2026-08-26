#include "core/Volume.h"
#include "io/NiftiReader.h"

#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
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

std::string pathForItk(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

} // namespace

int main()
{
    using Image = radmarky::core::Volume::ImageType;
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const auto filePath = std::filesystem::temp_directory_path()
        / ("radmarky-nifti-reader-" + std::to_string(unique) + ".nii");
    const auto integerFilePath = std::filesystem::temp_directory_path()
        / ("radmarky-nifti-integer-" + std::to_string(unique) + ".nii");

    try
    {
        auto image = Image::New();
        Image::SizeType size{{5, 4, 3}};
        Image::RegionType region;
        region.SetSize(size);
        image->SetRegions(region);

        Image::SpacingType spacing;
        spacing[0] = 0.8;
        spacing[1] = 1.1;
        spacing[2] = 2.4;
        image->SetSpacing(spacing);

        Image::PointType origin;
        origin[0] = 12.5;
        origin[1] = -30.0;
        origin[2] = 4.0;
        image->SetOrigin(origin);

        Image::DirectionType direction;
        direction.SetIdentity();
        direction[0][0] = 0.0;
        direction[0][1] = -1.0;
        direction[1][0] = 1.0;
        direction[1][1] = 0.0;
        image->SetDirection(direction);
        image->Allocate();
        image->FillBuffer(-100.0F);

        Image::IndexType rightMarker{{0, 1, 1}};
        Image::IndexType leftMarker{{4, 1, 1}};
        image->SetPixel(rightMarker, 100.0F);
        image->SetPixel(leftMarker, 200.0F);

        using Writer = itk::ImageFileWriter<Image>;
        auto writer = Writer::New();
        writer->SetImageIO(itk::NiftiImageIO::New());
        writer->SetFileName(pathForItk(filePath));
        writer->SetInput(image);
        writer->Update();

        bool passed = true;
        const auto volume = radmarky::io::NiftiReader::read(filePath);
        passed &= radmarky::io::NiftiReader::componentKind(filePath)
            == radmarky::io::NiftiReader::ComponentKind::FloatingPoint;
        const auto& geometry = volume->geometry();
        passed &= geometry.dimensions()
            == radmarky::core::ImageGeometry::Dimensions{{5, 4, 3}};
        passed &= expectNear(geometry.spacing()[0], 0.8, "spacing x");
        passed &= expectNear(geometry.spacing()[1], 1.1, "spacing y");
        passed &= expectNear(geometry.spacing()[2], 2.4, "spacing z");
        passed &= expectNear(geometry.origin()[0], 12.5, "origin x");
        passed &= expectNear(geometry.origin()[1], -30.0, "origin y");
        passed &= expectNear(geometry.origin()[2], 4.0, "origin z");
        passed &= expectNear(geometry.direction()[0][1], -1.0, "direction 0,1");
        passed &= expectNear(geometry.direction()[1][0], 1.0, "direction 1,0");
        passed &= expectNear(volume->image().GetPixel(rightMarker), 100.0, "right marker");
        passed &= expectNear(volume->image().GetPixel(leftMarker), 200.0, "left marker");

        const auto& metadata = volume->dicomMetadata();
        passed &= !metadata.empty();
        if(metadata.empty())
        {
            std::cerr << "NIfTI protocol metadata: expected extracted header fields\n";
        }
        const auto metadataValue = [&metadata](const std::string_view key)
            -> std::string {
            const auto found = std::find_if(
                metadata.begin(), metadata.end(), [&](const auto& entry) {
                    return entry.key == key;
                });
            return found == metadata.end() ? std::string{} : found->value;
        };
        const auto hasMetadataKey = [&metadata](const std::string_view key) {
            return std::any_of(
                metadata.begin(), metadata.end(), [&](const auto& entry) {
                    return entry.key == key;
                });
        };
        passed &= metadataValue("dim[1]") == "5";
        passed &= metadataValue("dim[2]") == "4";
        passed &= metadataValue("dim[3]") == "3";
        if(metadataValue("dim[1]") != "5" || metadataValue("dim[2]") != "4"
           || metadataValue("dim[3]") != "3")
        {
            std::cerr << "NIfTI dim[] metadata: expected 5 4 3, got "
                      << metadataValue("dim[1]") << ' '
                      << metadataValue("dim[2]") << ' '
                      << metadataValue("dim[3]") << '\n';
        }
        passed &= hasMetadataKey("pixdim[1]");
        passed &= hasMetadataKey("pixdim[2]");
        passed &= hasMetadataKey("pixdim[3]");
        passed &= hasMetadataKey("datatype");
        passed &= hasMetadataKey("qform_code");
        passed &= hasMetadataKey("sform_code");
        passed &= hasMetadataKey("srow_x");
        passed &= hasMetadataKey("srow_y");
        passed &= hasMetadataKey("srow_z");
        passed &= hasMetadataKey("descrip");
        if(!hasMetadataKey("datatype") || !hasMetadataKey("qform_code")
           || !hasMetadataKey("sform_code") || !hasMetadataKey("srow_x"))
        {
            std::cerr << "NIfTI protocol metadata: missing required header keys\n";
        }

        using IntegerImage = itk::Image<unsigned short, 3>;
        auto integerImage = IntegerImage::New();
        IntegerImage::SizeType integerSize{{2, 2, 2}};
        IntegerImage::RegionType integerRegion;
        integerRegion.SetSize(integerSize);
        integerImage->SetRegions(integerRegion);
        integerImage->Allocate();
        integerImage->FillBuffer(3);
        using IntegerWriter = itk::ImageFileWriter<IntegerImage>;
        auto integerWriter = IntegerWriter::New();
        integerWriter->SetImageIO(itk::NiftiImageIO::New());
        integerWriter->SetFileName(pathForItk(integerFilePath));
        integerWriter->SetInput(integerImage);
        integerWriter->Update();
        passed &= radmarky::io::NiftiReader::componentKind(integerFilePath)
            == radmarky::io::NiftiReader::ComponentKind::Integer;

        std::filesystem::remove(filePath);
        std::filesystem::remove(integerFilePath);
        return passed ? 0 : 1;
    }
    catch(const std::exception& exception)
    {
        std::filesystem::remove(filePath);
        std::filesystem::remove(integerFilePath);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
