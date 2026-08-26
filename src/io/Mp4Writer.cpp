#include "io/Mp4Writer.h"

#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSaveFile>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace radmarky::io
{
namespace
{

QString ffmpegError(const int code)
{
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, message, sizeof(message));
    return QString::fromUtf8(message);
}

int writeOutput(void* const opaque, const std::uint8_t* const data, const int size)
{
    auto* const file = static_cast<QSaveFile*>(opaque);
    const qint64 written = file->write(
        reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    return written < 0 ? AVERROR(EIO) : static_cast<int>(written);
}

std::int64_t seekOutput(
    void* const opaque, const std::int64_t offset, const int whence)
{
    auto* const file = static_cast<QSaveFile*>(opaque);
    if(whence == AVSEEK_SIZE)
    {
        return file->size();
    }

    const int origin = whence & ~AVSEEK_FORCE;
    std::int64_t position = offset;
    if(origin == SEEK_CUR)
    {
        position += file->pos();
    }
    else if(origin == SEEK_END)
    {
        position += file->size();
    }
    else if(origin != SEEK_SET)
    {
        return AVERROR(EINVAL);
    }
    if(position < 0 || !file->seek(position))
    {
        return AVERROR(EIO);
    }
    return position;
}

} // namespace

struct Mp4Writer::Impl
{
    ~Impl()
    {
        sws_freeContext(scaler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avio_context_free(&ioContext);
        avformat_free_context(formatContext);
        if(output && !finished)
        {
            output->cancelWriting();
        }
    }

    void setError(const QString& message)
    {
        if(error.isEmpty())
        {
            error = message;
        }
    }

    void setFfmpegError(const QString& operation, const int code)
    {
        setError(QObject::tr("%1: %2").arg(operation, ffmpegError(code)));
    }

    bool writePackets()
    {
        while(true)
        {
            const int result = avcodec_receive_packet(codecContext, packet);
            if(result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            {
                return true;
            }
            if(result < 0)
            {
                setFfmpegError(QObject::tr("Could not encode the MP4 frame"), result);
                return false;
            }

            av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int writeResult =
                av_interleaved_write_frame(formatContext, packet);
            av_packet_unref(packet);
            if(writeResult < 0)
            {
                setFfmpegError(QObject::tr("Could not write the MP4 frame"), writeResult);
                return false;
            }
        }
    }

    std::unique_ptr<QSaveFile> output;
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    AVIOContext* ioContext = nullptr;
    SwsContext* scaler = nullptr;
    QString error;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int encodedWidth = 0;
    int encodedHeight = 0;
    std::int64_t frameIndex = 0;
    bool headerWritten = false;
    bool finished = false;
};

Mp4Writer::Mp4Writer(
    const QString& fileName,
    const int width,
    const int height,
    const int frameDelayMilliseconds)
    : impl_(std::make_unique<Impl>())
{
    if(width <= 0 || height <= 0 || frameDelayMilliseconds <= 0)
    {
        impl_->setError(QObject::tr("MP4 frame settings are invalid."));
        return;
    }

    impl_->sourceWidth = width;
    impl_->sourceHeight = height;
    impl_->encodedWidth = width + width % 2;
    impl_->encodedHeight = height + height % 2;
    impl_->output = std::make_unique<QSaveFile>(fileName);
    if(!impl_->output->open(QIODevice::WriteOnly))
    {
        impl_->setError(impl_->output->errorString());
        return;
    }

    int result = avformat_alloc_output_context2(
        &impl_->formatContext, nullptr, "mp4", nullptr);
    if(result < 0 || impl_->formatContext == nullptr)
    {
        impl_->setFfmpegError(
            QObject::tr("Could not initialize the MP4 container"), result);
        return;
    }

    constexpr int ioBufferSize = 64 * 1024;
    auto* const ioBuffer = static_cast<unsigned char*>(av_malloc(ioBufferSize));
    if(ioBuffer == nullptr)
    {
        impl_->setError(QObject::tr("Could not allocate the MP4 output buffer."));
        return;
    }
    impl_->ioContext = avio_alloc_context(
        ioBuffer,
        ioBufferSize,
        1,
        impl_->output.get(),
        nullptr,
        &writeOutput,
        &seekOutput);
    if(impl_->ioContext == nullptr)
    {
        av_free(ioBuffer);
        impl_->setError(QObject::tr("Could not initialize MP4 file output."));
        return;
    }
    impl_->formatContext->pb = impl_->ioContext;
    impl_->formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

    const AVCodec* const codec = avcodec_find_encoder_by_name("libx264");
    if(codec == nullptr)
    {
        impl_->setError(QObject::tr(
            "The integrated FFmpeg build does not contain the H.264 encoder."));
        return;
    }
    impl_->stream = avformat_new_stream(impl_->formatContext, nullptr);
    impl_->codecContext = avcodec_alloc_context3(codec);
    if(impl_->stream == nullptr || impl_->codecContext == nullptr)
    {
        impl_->setError(QObject::tr("Could not initialize the H.264 encoder."));
        return;
    }

    int timeBaseNumerator = 0;
    int timeBaseDenominator = 0;
    av_reduce(
        &timeBaseNumerator,
        &timeBaseDenominator,
        frameDelayMilliseconds,
        1000,
        1000);
    impl_->codecContext->codec_id = AV_CODEC_ID_H264;
    impl_->codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    impl_->codecContext->width = impl_->encodedWidth;
    impl_->codecContext->height = impl_->encodedHeight;
    impl_->codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->codecContext->time_base = {timeBaseNumerator, timeBaseDenominator};
    impl_->codecContext->framerate = av_inv_q(impl_->codecContext->time_base);
    impl_->codecContext->gop_size = 30;
    impl_->codecContext->max_b_frames = 2;
    if((impl_->formatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0)
    {
        impl_->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    av_opt_set(impl_->codecContext->priv_data, "preset", "medium", 0);
    av_opt_set(impl_->codecContext->priv_data, "crf", "18", 0);
    av_opt_set(impl_->codecContext->priv_data, "tune", "animation", 0);

    result = avcodec_open2(impl_->codecContext, codec, nullptr);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not open the H.264 encoder"), result);
        return;
    }
    result = avcodec_parameters_from_context(
        impl_->stream->codecpar, impl_->codecContext);
    if(result < 0)
    {
        impl_->setFfmpegError(
            QObject::tr("Could not configure the MP4 video stream"), result);
        return;
    }
    impl_->stream->time_base = impl_->codecContext->time_base;

    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if(impl_->frame == nullptr || impl_->packet == nullptr)
    {
        impl_->setError(QObject::tr("Could not allocate an MP4 video frame."));
        return;
    }
    impl_->frame->format = impl_->codecContext->pix_fmt;
    impl_->frame->width = impl_->encodedWidth;
    impl_->frame->height = impl_->encodedHeight;
    result = av_frame_get_buffer(impl_->frame, 32);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not allocate MP4 image data"), result);
        return;
    }
    impl_->scaler = sws_getContext(
        impl_->encodedWidth,
        impl_->encodedHeight,
        AV_PIX_FMT_RGBA,
        impl_->encodedWidth,
        impl_->encodedHeight,
        impl_->codecContext->pix_fmt,
        SWS_BICUBIC,
        nullptr,
        nullptr,
        nullptr);
    if(impl_->scaler == nullptr)
    {
        impl_->setError(QObject::tr("Could not initialize MP4 color conversion."));
        return;
    }

    result = avformat_write_header(impl_->formatContext, nullptr);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not write the MP4 header"), result);
        return;
    }
    impl_->headerWritten = true;
}

Mp4Writer::~Mp4Writer() = default;

bool Mp4Writer::isOpen() const noexcept
{
    return impl_->error.isEmpty() && impl_->headerWritten && !impl_->finished;
}

const QString& Mp4Writer::errorString() const noexcept
{
    return impl_->error;
}

bool Mp4Writer::writeFrame(const QImage& image, const int)
{
    if(!isOpen())
    {
        return false;
    }
    if(image.width() != impl_->sourceWidth || image.height() != impl_->sourceHeight)
    {
        impl_->setError(QObject::tr(
            "Every MP4 frame must have the same dimensions."));
        return false;
    }

    QImage source = image.convertToFormat(QImage::Format_RGBA8888);
    if(source.width() != impl_->encodedWidth
       || source.height() != impl_->encodedHeight)
    {
        QImage padded(
            impl_->encodedWidth, impl_->encodedHeight, QImage::Format_RGBA8888);
        padded.fill(Qt::black);
        QPainter painter(&padded);
        painter.drawImage(0, 0, source);
        painter.end();
        source = std::move(padded);
    }

    int result = av_frame_make_writable(impl_->frame);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not prepare the MP4 frame"), result);
        return false;
    }
    const std::uint8_t* sourceData[] = {source.constBits(), nullptr, nullptr, nullptr};
    const int sourceLines[] = {
        static_cast<int>(source.bytesPerLine()), 0, 0, 0};
    const int converted = sws_scale(
        impl_->scaler,
        sourceData,
        sourceLines,
        0,
        impl_->encodedHeight,
        impl_->frame->data,
        impl_->frame->linesize);
    if(converted != impl_->encodedHeight)
    {
        impl_->setError(QObject::tr("Could not convert the MP4 frame colors."));
        return false;
    }
    impl_->frame->pts = impl_->frameIndex++;
    result = avcodec_send_frame(impl_->codecContext, impl_->frame);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not submit the MP4 frame"), result);
        return false;
    }
    return impl_->writePackets();
}

bool Mp4Writer::finish()
{
    if(!isOpen())
    {
        return false;
    }
    const int result = avcodec_send_frame(impl_->codecContext, nullptr);
    if(result < 0)
    {
        impl_->setFfmpegError(QObject::tr("Could not finish H.264 encoding"), result);
        return false;
    }
    if(!impl_->writePackets())
    {
        return false;
    }
    const int trailerResult = av_write_trailer(impl_->formatContext);
    if(trailerResult < 0)
    {
        impl_->setFfmpegError(
            QObject::tr("Could not finalize the MP4 file"), trailerResult);
        return false;
    }
    avio_flush(impl_->ioContext);
    if(!impl_->output->commit())
    {
        impl_->setError(impl_->output->errorString());
        return false;
    }
    impl_->finished = true;
    return true;
}

} // namespace radmarky::io
