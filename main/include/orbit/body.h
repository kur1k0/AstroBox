#pragma once

#include "orbit/vector2d.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orbit {

enum class BodyType : std::uint8_t {
    Star,
    Planet,
    Asteroid,
    BlackHole,
};

struct Body {
    static constexpr std::size_t kTrailSize = 16;

    int id = -1;
    char name[16] = {};
    bool active = false;
    Vector2D pos;
    Vector2D vel;
    Vector2D acc;
    double mass = 1.0;
    double radius = 0.02;
    std::uint32_t color = 0xFFFFFF;
    BodyType type = BodyType::Planet;
    std::array<Vector2D, kTrailSize> trail = {};
    std::size_t trail_head = 0;
    std::size_t trail_count = 0;

    void clear_trail();
    void push_trail();
    const Vector2D &trail_at(std::size_t chronological_index) const;
};

struct BodySpec {
    const char *name = "Body";
    Vector2D pos;
    Vector2D vel;
    double mass = 1.0;
    double radius = 0.02;
    std::uint32_t color = 0xFFFFFF;
    BodyType type = BodyType::Planet;
};

} 
