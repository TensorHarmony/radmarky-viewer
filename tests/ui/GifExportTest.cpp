#include "io/AnimatedGifWriter.h"
#include "io/Mp4Writer.h"
#include "ui/GifExportDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gif_lib.h>

#include <array>
#include <iostream>
#include <string_view>

namespace
{

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

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    using radmarky::core::SliceOrientation;
    const std::array<radmarky::ui::GifSliceRange, 3> ranges{{
        {SliceOrientation::Axial, 2.0, 5, 4},
        {SliceOrientation::Sagittal, 1.0, 3, 8},
        {SliceOrientation::Coronal, 2.5, 2, 2},
    }};
    radmarky::ui::GifExportDialog dialog(
        ranges, SliceOrientation::Axial);
    auto* const range =
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("gifRangeMm"));
    auto* const playbackSpeed =
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("animationSpeedFps"));
    auto* const pingPong =
        dialog.findChild<QCheckBox*>(QStringLiteral("gifPingPong"));
    auto* const respectZoom =
        dialog.findChild<QCheckBox*>(QStringLiteral("gifRespectZoom"));
    auto* const showCrosshair =
        dialog.findChild<QCheckBox*>(QStringLiteral("gifShowCrosshair"));

    bool passed = expectTrue(range != nullptr, "range control")
        && expectTrue(playbackSpeed != nullptr, "playback speed control")
        && expectTrue(pingPong != nullptr, "ping-pong control")
        && expectTrue(respectZoom != nullptr, "zoom control")
        && expectTrue(showCrosshair != nullptr, "crosshair control");
    if(!passed)
    {
        return 1;
    }
    range->setValue(5.0);
    passed &= expectTrue(dialog.slicesBefore() == 2, "slices before")
        && expectTrue(dialog.slicesAfter() == 2, "slices after")
        && expectTrue(
            dialog.frameDelayMilliseconds() == 250,
            "default playback speed")
        && expectTrue(dialog.pingPong(), "ping-pong default")
        && expectTrue(
            dialog.format() == radmarky::ui::AnimationFormat::Mp4,
            "MP4 default")
        && expectTrue(dialog.respectZoom(), "respect zoom default")
        && expectTrue(!dialog.showCrosshair(), "crosshair default");
    playbackSpeed->setValue(2.5);
    passed &= expectTrue(
        dialog.frameDelayMilliseconds() == 400,
        "custom playback speed");

    QTemporaryDir temporary;
    passed &= expectTrue(temporary.isValid(), "temporary directory");
    const QString fileName = temporary.filePath(QStringLiteral("animation.gif"));
    radmarky::io::AnimatedGifWriter writer(fileName, 8, 6);
    passed &= expectTrue(writer.isOpen(), "GIF writer open");

    QImage first(8, 6, QImage::Format_RGB32);
    first.fill(QColor(32, 32, 32));
    QImage second(8, 6, QImage::Format_RGB32);
    second.fill(QColor(220, 30, 30));
    passed &= expectTrue(writer.writeFrame(first, 100), "first frame")
        && expectTrue(writer.writeFrame(second, 100), "second frame")
        && expectTrue(writer.finish(), "finish GIF");

    int gifError = 0;
    const QByteArray encodedName = QFile::encodeName(fileName);
    GifFileType* const decoded =
        DGifOpenFileName(encodedName.constData(), &gifError);
    passed &= expectTrue(decoded != nullptr, "open encoded GIF");
    if(decoded != nullptr)
    {
        passed &= expectTrue(DGifSlurp(decoded) == GIF_OK, "decode GIF")
            && expectTrue(decoded->ImageCount == 2, "GIF frame count");
        DGifCloseFile(decoded, &gifError);
    }

    QFile encoded(fileName);
    passed &= expectTrue(encoded.open(QIODevice::ReadOnly), "open GIF bytes");
    const QByteArray bytes = encoded.readAll();
    passed &= expectTrue(
        bytes.startsWith("GIF89a"), "GIF89a signature")
        && expectTrue(
            bytes.contains("NETSCAPE2.0"), "infinite-loop extension");

    const QString mp4Name = temporary.filePath(QStringLiteral("animation.mp4"));
    radmarky::io::Mp4Writer mp4(mp4Name, 8, 6, 100);
    passed &= expectTrue(mp4.isOpen(), "MP4 writer open")
        && expectTrue(mp4.writeFrame(first, 100), "first MP4 frame")
        && expectTrue(mp4.writeFrame(second, 100), "second MP4 frame")
        && expectTrue(mp4.finish(), "finish MP4");
    QFile mp4Encoded(mp4Name);
    passed &= expectTrue(
        mp4Encoded.open(QIODevice::ReadOnly), "open MP4 bytes");
    const QByteArray mp4Header = mp4Encoded.read(12);
    passed &= expectTrue(
        mp4Header.mid(4, 4) == QByteArrayLiteral("ftyp"),
        "MP4 file signature");
    return passed ? 0 : 1;
}
