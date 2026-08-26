#include "app/ApplicationInfo.h"

#ifndef RADMARKY_VERSION
#define RADMARKY_VERSION "0.0.0"
#endif

#ifndef RADMARKY_RELEASE_DATE
#define RADMARKY_RELEASE_DATE "Unreleased"
#endif

namespace radmarky::app
{

std::string_view applicationName() noexcept
{
    return "RadMarky Viewer";
}

std::string_view copyrightHolder() noexcept
{
    return "TensorHarmony Technologies Inc.";
}

std::string_view copyrightNotice() noexcept
{
    return "Copyright © 2026 TensorHarmony Technologies Inc.";
}

std::string_view applicationVersion() noexcept
{
    return RADMARKY_VERSION;
}

std::string_view applicationReleaseDate() noexcept
{
    return RADMARKY_RELEASE_DATE;
}

} // namespace radmarky::app
