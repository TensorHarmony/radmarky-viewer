#pragma once

#include "io/AnimationWriter.h"

#include <memory>

class QImage;

namespace radmarky::io
{

class AnimatedGifWriter final : public AnimationWriter
{
public:
    AnimatedGifWriter(const QString& fileName, int width, int height);
    ~AnimatedGifWriter() override;

    AnimatedGifWriter(const AnimatedGifWriter&) = delete;
    AnimatedGifWriter& operator=(const AnimatedGifWriter&) = delete;

    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] const QString& errorString() const noexcept override;
    bool writeFrame(const QImage& image, int delayMilliseconds) override;
    bool finish() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace radmarky::io
