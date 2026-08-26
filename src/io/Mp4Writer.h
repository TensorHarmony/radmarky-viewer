#pragma once

#include "io/AnimationWriter.h"

#include <memory>

namespace radmarky::io
{

class Mp4Writer final : public AnimationWriter
{
public:
    Mp4Writer(
        const QString& fileName,
        int width,
        int height,
        int frameDelayMilliseconds);
    ~Mp4Writer() override;

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] const QString& errorString() const noexcept override;
    bool writeFrame(const QImage& image, int delayMilliseconds) override;
    bool finish() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace radmarky::io
