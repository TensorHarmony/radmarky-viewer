#pragma once

#include "core/ImageGeometry.h"

namespace radmarky::core
{

[[nodiscard]] double physicalDistanceMillimetres(
    const ImageGeometry::Vector& first,
    const ImageGeometry::Vector& second);

} // namespace radmarky::core
