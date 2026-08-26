#pragma once

#include <string_view>

namespace radmarky::app
{

[[nodiscard]] std::string_view applicationName() noexcept;
[[nodiscard]] std::string_view copyrightHolder() noexcept;
[[nodiscard]] std::string_view copyrightNotice() noexcept;
[[nodiscard]] std::string_view applicationVersion() noexcept;
[[nodiscard]] std::string_view applicationReleaseDate() noexcept;

} // namespace radmarky::app
