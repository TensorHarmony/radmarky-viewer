#include "app/ApplicationInfo.h"

#include <iostream>
#include <string_view>

namespace
{

bool expectEqual(
    const std::string_view actual,
    const std::string_view expected,
    const std::string_view field)
{
    if(actual == expected)
    {
        return true;
    }

    std::cerr << field << ": expected '" << expected << "', got '" << actual
              << "'\n";
    return false;
}

} // namespace

int main()
{
    const bool nameIsCorrect = expectEqual(
        radmarky::app::applicationName(), "RadMarky Viewer", "application name");
    const bool copyrightHolderIsCorrect = expectEqual(
        radmarky::app::copyrightHolder(),
        "TensorHarmony Technologies Inc.",
        "copyright holder");
    const bool copyrightNoticeIsCorrect = expectEqual(
        radmarky::app::copyrightNotice(),
        "Copyright © 2026 TensorHarmony Technologies Inc.",
        "copyright notice");
    const bool versionIsCorrect = expectEqual(
        radmarky::app::applicationVersion(), "1.0.0-rc.2", "application version");
    const bool releaseDateIsCorrect = expectEqual(
        radmarky::app::applicationReleaseDate(),
        "August 29, 2026",
        "application release date");

    return nameIsCorrect && copyrightHolderIsCorrect && copyrightNoticeIsCorrect
            && versionIsCorrect && releaseDateIsCorrect
        ? 0
        : 1;
}
