#pragma once

#include <QCoreApplication>
#include <QVariant>

namespace radmarky::rendering
{

inline constexpr auto renderProfilingProperty = "radmarky.renderProfiling";

[[nodiscard]] inline bool renderProfilingEnabled() noexcept
{
    const auto* const application = QCoreApplication::instance();
    return application != nullptr
        && application->property(renderProfilingProperty).toBool();
}

} // namespace radmarky::rendering
