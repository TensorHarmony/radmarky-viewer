#include "io/AnimatedGifWriter.h"

#include <QImage>
#include <QObject>
#include <QSaveFile>

#include <gif_lib.h>

#include <algorithm>
#include <array>
#include <vector>

namespace radmarky::io
{
namespace
{

QString gifError(const int code)
{
    const char* const message = GifErrorString(code);
    return message != nullptr
        ? QString::fromLocal8Bit(message)
        : QObject::tr("Unknown GIF encoding error");
}

int writeGifBytes(
    GifFileType* const gif,
    const GifByteType* const bytes,
    const int byteCount)
{
    auto* const file = static_cast<QSaveFile*>(gif->UserData);
    const qint64 written = file->write(
        reinterpret_cast<const char*>(bytes), static_cast<qint64>(byteCount));
    return written == byteCount ? byteCount : 0;
}

} // namespace

struct AnimatedGifWriter::Impl
{
    QSaveFile file;
    GifFileType* gif = nullptr;
    QString error;
    int width = 0;
    int height = 0;
    bool finished = false;
};

AnimatedGifWriter::AnimatedGifWriter(
    const QString& fileName,
    const int width,
    const int height)
    : impl_(std::make_unique<Impl>())
{
    impl_->width = width;
    impl_->height = height;
    if(width <= 0 || height <= 0 || width > 65535 || height > 65535)
    {
        impl_->error = QObject::tr("GIF frame dimensions are invalid or too large.");
        return;
    }

    impl_->file.setFileName(fileName);
    if(!impl_->file.open(QIODevice::WriteOnly))
    {
        impl_->error = impl_->file.errorString();
        return;
    }

    int errorCode = 0;
    impl_->gif = EGifOpen(&impl_->file, writeGifBytes, &errorCode);
    if(impl_->gif == nullptr)
    {
        impl_->error = gifError(errorCode);
        impl_->file.cancelWriting();
        return;
    }

    EGifSetGifVersion(impl_->gif, true);
    if(EGifPutScreenDesc(impl_->gif, width, height, 8, 0, nullptr) == GIF_ERROR)
    {
        impl_->error = gifError(impl_->gif->Error);
        return;
    }

    constexpr std::array<GifByteType, 11> applicationId{
        'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
    constexpr std::array<GifByteType, 3> loopForever{{1, 0, 0}};
    if(EGifPutExtensionLeader(impl_->gif, APPLICATION_EXT_FUNC_CODE) == GIF_ERROR
       || EGifPutExtensionBlock(
              impl_->gif,
              static_cast<int>(applicationId.size()),
              applicationId.data()) == GIF_ERROR
       || EGifPutExtensionBlock(
              impl_->gif,
              static_cast<int>(loopForever.size()),
              loopForever.data()) == GIF_ERROR
       || EGifPutExtensionTrailer(impl_->gif) == GIF_ERROR)
    {
        impl_->error = gifError(impl_->gif->Error);
    }
}

AnimatedGifWriter::~AnimatedGifWriter()
{
    if(impl_->gif != nullptr)
    {
        int errorCode = 0;
        EGifCloseFile(impl_->gif, &errorCode);
        impl_->gif = nullptr;
    }
    if(!impl_->finished)
    {
        impl_->file.cancelWriting();
    }
}

bool AnimatedGifWriter::isOpen() const noexcept
{
    return impl_->gif != nullptr && impl_->error.isEmpty() && !impl_->finished;
}

const QString& AnimatedGifWriter::errorString() const noexcept
{
    return impl_->error;
}

bool AnimatedGifWriter::writeFrame(
    const QImage& image,
    const int delayMilliseconds)
{
    if(!isOpen())
    {
        return false;
    }
    if(image.width() != impl_->width || image.height() != impl_->height)
    {
        impl_->error = QObject::tr("Every GIF frame must have the same dimensions.");
        return false;
    }

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    const qsizetype pixelCount =
        static_cast<qsizetype>(impl_->width) * impl_->height;
    std::vector<GifByteType> indexed(static_cast<std::size_t>(pixelCount));

    constexpr int colorLevels = 6;
    constexpr int colorCubeSize = colorLevels * colorLevels * colorLevels;
    constexpr int grayscaleLevels = 256 - colorCubeSize;
    std::array<GifColorType, 256> palette{};
    for(int redLevel = 0; redLevel < colorLevels; ++redLevel)
    {
        for(int greenLevel = 0; greenLevel < colorLevels; ++greenLevel)
        {
            for(int blueLevel = 0; blueLevel < colorLevels; ++blueLevel)
            {
                const int index =
                    (redLevel * colorLevels + greenLevel) * colorLevels
                    + blueLevel;
                palette[static_cast<std::size_t>(index)] = {
                    static_cast<GifByteType>(redLevel * 255 / (colorLevels - 1)),
                    static_cast<GifByteType>(greenLevel * 255 / (colorLevels - 1)),
                    static_cast<GifByteType>(blueLevel * 255 / (colorLevels - 1)),
                };
            }
        }
    }
    for(int level = 0; level < grayscaleLevels; ++level)
    {
        const auto value = static_cast<GifByteType>(
            level * 255 / (grayscaleLevels - 1));
        palette[static_cast<std::size_t>(colorCubeSize + level)] = {
            value, value, value};
    }

    for(int y = 0; y < impl_->height; ++y)
    {
        const auto* const scanLine = rgb.constScanLine(y);
        for(int x = 0; x < impl_->width; ++x)
        {
            const auto source = static_cast<std::size_t>(x * 3);
            const auto target = static_cast<std::size_t>(y) * impl_->width + x;
            const int red = scanLine[source];
            const int green = scanLine[source + 1];
            const int blue = scanLine[source + 2];
            const int minimum = std::min({red, green, blue});
            const int maximum = std::max({red, green, blue});
            if(maximum - minimum < 18)
            {
                const int luminance = (red * 54 + green * 183 + blue * 19) / 256;
                indexed[target] = static_cast<GifByteType>(
                    colorCubeSize
                    + (luminance * (grayscaleLevels - 1) + 127) / 255);
            }
            else
            {
                const int redLevel = (red * (colorLevels - 1) + 127) / 255;
                const int greenLevel = (green * (colorLevels - 1) + 127) / 255;
                const int blueLevel = (blue * (colorLevels - 1) + 127) / 255;
                indexed[target] = static_cast<GifByteType>(
                    (redLevel * colorLevels + greenLevel) * colorLevels
                    + blueLevel);
            }
        }
    }

    ColorMapObject* const colorMap = GifMakeMapObject(256, palette.data());
    if(colorMap == nullptr)
    {
        impl_->error = QObject::tr("The GIF color table could not be allocated.");
        return false;
    }

    const int delayCentiseconds = std::clamp(
        (delayMilliseconds + 5) / 10, 1, 65535);
    const std::array<GifByteType, 4> control{{
        0,
        static_cast<GifByteType>(delayCentiseconds & 0xff),
        static_cast<GifByteType>((delayCentiseconds >> 8) & 0xff),
        0,
    }};

    bool success = EGifPutExtension(
                       impl_->gif,
                       GRAPHICS_EXT_FUNC_CODE,
                       static_cast<int>(control.size()),
                       control.data()) != GIF_ERROR
        && EGifPutImageDesc(
               impl_->gif,
               0,
               0,
               impl_->width,
               impl_->height,
               false,
               colorMap) != GIF_ERROR;
    for(int y = 0; success && y < impl_->height; ++y)
    {
        success = EGifPutLine(
                      impl_->gif,
                      indexed.data() + static_cast<std::size_t>(y) * impl_->width,
                      impl_->width) != GIF_ERROR;
    }
    GifFreeMapObject(colorMap);

    if(!success)
    {
        impl_->error = gifError(impl_->gif->Error);
    }
    return success;
}

bool AnimatedGifWriter::finish()
{
    if(!isOpen())
    {
        return false;
    }

    int errorCode = 0;
    if(EGifCloseFile(impl_->gif, &errorCode) == GIF_ERROR)
    {
        impl_->gif = nullptr;
        impl_->error = gifError(errorCode);
        impl_->file.cancelWriting();
        return false;
    }
    impl_->gif = nullptr;
    if(!impl_->file.commit())
    {
        impl_->error = impl_->file.errorString();
        return false;
    }
    impl_->finished = true;
    return true;
}

} // namespace radmarky::io
