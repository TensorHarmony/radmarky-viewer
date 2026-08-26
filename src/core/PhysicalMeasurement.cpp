#include "core/PhysicalMeasurement.h"

#include <cmath>
#include <stdexcept>

namespace radmarky::core
{

double physicalDistanceMillimetres(
    const ImageGeometry::Vector& first,
    const ImageGeometry::Vector& second)
{
    double squaredDistance = 0.0;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(!std::isfinite(first[axis]) || !std::isfinite(second[axis]))
        {
            throw std::invalid_argument("Measurement points must be finite");
        }
        const double difference = second[axis] - first[axis];
        squaredDistance += difference * difference;
    }
    return std::sqrt(squaredDistance);
}

} // namespace radmarky::core
