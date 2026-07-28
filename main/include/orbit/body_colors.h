#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orbit {

struct BodyColorOption {
    const char *name;
    std::uint32_t rgb;
};

inline constexpr std::array<BodyColorOption, 8> kBodyColors = {{
    {"RED", 0xFF4D4D},
    {"ORANGE", 0xFF8C42},
    {"YELLOW", 0xFFE66D},
    {"GREEN", 0x56D364},
    {"CYAN", 0x4ECDC4},
    {"BLUE", 0x2D8CFF},
    {"PURPLE", 0xB983FF},
    {"WHITE", 0xFFFFFF},
}};

inline constexpr std::size_t kStarDefaultColor = 2;
inline constexpr std::size_t kPlanetDefaultColor = 5;

std::size_t nearest_body_color(std::uint32_t rgb);

} 
