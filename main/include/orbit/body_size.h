#pragma once

#include <algorithm>
#include <cmath>

namespace orbit {




inline double radius_for_mass(double mass)
{
    if (!std::isfinite(mass) || mass <= 0.0) return 0.006;
    return std::clamp(0.026 * std::cbrt(mass), 0.006, 0.26);
}




inline double visual_radius_for_mass(double mass)
{
    if (!std::isfinite(mass) || mass <= 0.0) return 0.006;
    return std::clamp(0.026 * std::pow(mass, 0.14), 0.006, 0.07);
}

} 
