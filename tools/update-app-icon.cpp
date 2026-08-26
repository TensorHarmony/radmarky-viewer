#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QList>
#include <QPainter>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>

#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace
{
constexpr int kIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

QByteArray icoDibBytes(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();
    const int xorStride = width * 4;
    const int andStride = ((width + 31) / 32) * 4;
    const quint32 xorSize = static_cast<quint32>(xorStride * height);
    const quint32 andSize = static_cast<quint32>(andStride * height);

    QByteArray bytes;
    bytes.reserve(40 + static_cast<int>(xorSize + andSize));
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint32(40);
    out << quint32(width);
    out << quint32(height * 2);
    out << quint16(1);
    out << quint16(32);
    out << quint32(0);
    out << quint32(xorSize + andSize);
    out << quint32(0) << quint32(0) << quint32(0) << quint32(0);

    for(int y = height - 1; y >= 0; --y)
    {
        const QRgb* const line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for(int x = 0; x < width; ++x)
        {
            const QRgb pixel = line[x];
            out << quint8(qBlue(pixel)) << quint8(qGreen(pixel))
                << quint8(qRed(pixel)) << quint8(qAlpha(pixel));
        }
    }

    for(quint32 i = 0; i < andSize; ++i)
    {
        out << quint8(0);
    }

    return bytes;
}

QImage renderSvg(QSvgRenderer& renderer, const int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter);
    painter.end();
    return image.convertToFormat(QImage::Format_ARGB32);
}

bool writeIco(const QString& outputPath, const QList<QByteArray>& images)
{
    if(images.size() != static_cast<int>(std::size(kIconSizes)))
    {
        return false;
    }

    QSaveFile file(outputPath);
    if(!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint16 count = static_cast<quint16>(images.size());
    out << quint16(0) << quint16(1) << count;

    quint32 offset = 6 + 16 * count;
    for(int i = 0; i < images.size(); ++i)
    {
        const int size = kIconSizes[i];
        const quint8 stored = size >= 256 ? 0 : static_cast<quint8>(size);
        out << stored << stored << quint8(0) << quint8(0);
        out << quint16(1) << quint16(32);
        out << quint32(images[i].size()) << offset;
        offset += static_cast<quint32>(images[i].size());
    }

    for(const QByteArray& image : images)
    {
        if(file.write(image) != image.size())
        {
            return false;
        }
    }

    return file.commit();
}

void printUsage()
{
    std::fprintf(
        stderr,
        "Usage: update-app-icon <app-icon.svg> <radmarky.ico>\n");
}
} // namespace

int main(int argc, char* argv[])
{
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "minimal");
    }

    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("update-app-icon"));

    const QStringList args = QCoreApplication::arguments();
    if(args.size() != 3)
    {
        printUsage();
        return EXIT_FAILURE;
    }

    const QString svgPath = args.at(1);
    const QString icoPath = args.at(2);
    if(!QFileInfo::exists(svgPath))
    {
        std::fprintf(stderr, "SVG not found: %s\n", qUtf8Printable(svgPath));
        return EXIT_FAILURE;
    }

    QSvgRenderer renderer(svgPath);
    if(!renderer.isValid())
    {
        std::fprintf(stderr, "Invalid SVG: %s\n", qUtf8Printable(svgPath));
        return EXIT_FAILURE;
    }

    QList<QByteArray> images;
    images.reserve(static_cast<int>(std::size(kIconSizes)));
    for(const int size : kIconSizes)
    {
        const QByteArray dib = icoDibBytes(renderSvg(renderer, size));
        if(dib.isEmpty())
        {
            std::fprintf(stderr, "Failed to encode %dx%d icon bitmap.\n", size, size);
            return EXIT_FAILURE;
        }
        images.append(dib);
    }

    if(!writeIco(icoPath, images))
    {
        std::fprintf(stderr, "Failed to write ICO: %s\n", qUtf8Printable(icoPath));
        return EXIT_FAILURE;
    }

    std::fprintf(stdout, "Wrote %s\n", qUtf8Printable(icoPath));
    return EXIT_SUCCESS;
}
