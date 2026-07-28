#include "orbit/body_colors.h"

#include <limits>

namespace orbit {

std::size_t nearest_body_color(std::uint32_t rgb)
{
    const int red = static_cast<int>((rgb >> 16U) & 0xFFU);
    const int green = static_cast<int>((rgb >> 8U) & 0xFFU);
    const int blue = static_cast<int>(rgb & 0xFFU);
    std::size_t nearest = 0;
    unsigned long best_distance = std::numeric_limits<unsigned long>::max();

    for (std::size_t i = 0; i < kBodyColors.size(); ++i) {
        const std::uint32_t candidate = kBodyColors[i].rgb;
        const long dr = red - static_cast<int>((candidate >> 16U) & 0xFFU);
        const long dg = green - static_cast<int>((candidate >> 8U) & 0xFFU);
        const long db = blue - static_cast<int>(candidate & 0xFFU);
        const unsigned long distance = static_cast<unsigned long>(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            nearest = i;
        }
    }
    return nearest;
}

} 
