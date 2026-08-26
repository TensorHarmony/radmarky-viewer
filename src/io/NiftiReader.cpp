#include "io/NiftiReader.h"

#include "core/Volume.h"

#include <itkCommand.h>
#include <itkImageFileReader.h>
#include <itkMacro.h>
#include <itkMatrix.h>
#include <itkMetaDataDictionary.h>
#include <itkMetaDataObject.h>
#include <itkNiftiImageIO.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace radmarky::io
{
namespace
{

std::string pathForItk(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::string safeDisplayValue(std::string value)
{
    for(char& character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if(character == '\0' || character == '\r' || character == '\n'
           || (byte < 0x20 && character != '\t'))
        {
            character = ' ';
        }
    }
    while(!value.empty() && value.back() == ' ')
    {
        value.pop_back();
    }
    return value;
}

template<typename T>
std::string streamValue(const T& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string matrix44Value(const itk::Matrix<float, 4, 4>& matrix)
{
    std::ostringstream stream;
    for(unsigned row = 0; row < 4; ++row)
    {
        for(unsigned column = 0; column < 4; ++column)
        {
            if(row != 0 || column != 0)
            {
                stream << ' ';
            }
            stream << matrix(row, column);
        }
    }
    return stream.str();
}

std::string metadataObjectValue(const itk::MetaDataObjectBase* const object)
{
    if(object == nullptr)
    {
        return {};
    }
    if(const auto* const value =
           dynamic_cast<const itk::MetaDataObject<std::string>*>(object))
    {
        return value->GetMetaDataObjectValue();
    }
    if(const auto* const value =
           dynamic_cast<const itk::MetaDataObject<float>*>(object))
    {
        return streamValue(value->GetMetaDataObjectValue());
    }
    if(const auto* const value =
           dynamic_cast<const itk::MetaDataObject<double>*>(object))
    {
        return streamValue(value->GetMetaDataObjectValue());
    }
    if(const auto* const value =
           dynamic_cast<const itk::MetaDataObject<itk::Matrix<float, 4, 4>>*>(
               object))
    {
        return matrix44Value(value->GetMetaDataObjectValue());
    }
    return {};
}

std::vector<core::Volume::DicomMetadataEntry> niftiMetadataFromDictionary(
    const itk::MetaDataDictionary& dictionary)
{
    std::vector<core::Volume::DicomMetadataEntry> metadata;
    for(auto iterator = dictionary.Begin(); iterator != dictionary.End();
        ++iterator)
    {
        if(iterator->first.empty())
        {
            continue;
        }
        metadata.push_back({
            iterator->first,
            safeDisplayValue(metadataObjectValue(iterator->second.GetPointer()))});
    }
    std::sort(
        metadata.begin(),
        metadata.end(),
        [](const auto& left, const auto& right) { return left.key < right.key; });
    return metadata;
}

} // namespace

NiftiReader::ComponentKind NiftiReader::componentKind(
    const std::filesystem::path& filePath)
{
    if(filePath.empty() || !std::filesystem::is_regular_file(filePath))
    {
        throw std::runtime_error("NIfTI file does not exist: " + filePath.string());
    }

    auto imageIo = itk::NiftiImageIO::New();
    imageIo->SetFileName(pathForItk(filePath));
    try
    {
        imageIo->ReadImageInformation();
    }
    catch(const itk::ExceptionObject& exception)
    {
        throw std::runtime_error(
            "Unable to inspect NIfTI file '" + filePath.string() + "': "
            + exception.GetDescription());
    }

    switch(imageIo->GetComponentType())
    {
    case itk::IOComponentEnum::FLOAT:
    case itk::IOComponentEnum::DOUBLE:
        return ComponentKind::FloatingPoint;
    case itk::IOComponentEnum::UCHAR:
    case itk::IOComponentEnum::CHAR:
    case itk::IOComponentEnum::USHORT:
    case itk::IOComponentEnum::SHORT:
    case itk::IOComponentEnum::UINT:
    case itk::IOComponentEnum::INT:
    case itk::IOComponentEnum::ULONG:
    case itk::IOComponentEnum::LONG:
    case itk::IOComponentEnum::ULONGLONG:
    case itk::IOComponentEnum::LONGLONG:
        return ComponentKind::Integer;
    default:
        throw std::runtime_error(
            "Unsupported NIfTI component type: "
            + imageIo->GetComponentTypeAsString(imageIo->GetComponentType()));
    }
}

std::shared_ptr<core::Volume> NiftiReader::read(
    const std::filesystem::path& filePath,
    const std::function<void(double)>& progress)
{
    if(filePath.empty())
    {
        throw std::invalid_argument("NIfTI path cannot be empty");
    }
    if(!std::filesystem::is_regular_file(filePath))
    {
        throw std::runtime_error("NIfTI file does not exist: " + filePath.string());
    }

    using Reader = itk::ImageFileReader<core::Volume::ImageType>;
    auto reader = Reader::New();
    auto imageIo = itk::NiftiImageIO::New();
    reader->SetImageIO(imageIo);
    reader->SetFileName(pathForItk(filePath));
    if(progress)
    {
        auto command = itk::CStyleCommand::New();
        command->SetClientData(const_cast<std::function<void(double)>*>(&progress));
        command->SetCallback([](
                                 itk::Object* caller,
                                 const itk::EventObject&,
                                 void* clientData) {
            auto* const process = dynamic_cast<itk::ProcessObject*>(caller);
            auto* const callback =
                static_cast<std::function<void(double)>*>(clientData);
            if(process != nullptr && callback != nullptr && *callback)
            {
                (*callback)(process->GetProgress());
            }
        });
        reader->AddObserver(itk::ProgressEvent(), command);
    }

    try
    {
        reader->Update();
    }
    catch(const itk::ExceptionObject& exception)
    {
        throw std::runtime_error(
            "Unable to read NIfTI file '" + filePath.string() + "': "
            + exception.GetDescription());
    }

    core::Volume::ImageType::Pointer image = reader->GetOutput();
    image->DisconnectPipeline();
    auto volume = std::make_shared<core::Volume>(std::move(image));
    volume->setDicomMetadata(
        niftiMetadataFromDictionary(imageIo->GetMetaDataDictionary()));
    return volume;
}

} // namespace radmarky::io
