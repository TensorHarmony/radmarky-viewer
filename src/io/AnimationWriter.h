#pragma once

#include <QString>

class QImage;

namespace radmarky::io
{

class AnimationWriter
{
public:
    virtual ~AnimationWriter() = default;

    [[nodiscard]] virtual bool isOpen() const noexcept = 0;
    [[nodiscard]] virtual const QString& errorString() const noexcept = 0;
    virtual bool writeFrame(const QImage& image, int delayMilliseconds) = 0;
    virtual bool finish() = 0;
};

} // namespace radmarky::io
