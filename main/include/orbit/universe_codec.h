#pragma once

#include "orbit/body_manager.h"
#include "orbit/physics_engine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace orbit {

struct UniverseEncodeResult {
    std::string text;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

struct DecodedBody {
    std::array<char, 16> name = {};
    Vector2D pos;
    Vector2D vel;
    double mass = 1.0;
    double radius = 0.02;
    std::uint32_t color = 0xFFFFFF;
    BodyType type = BodyType::Planet;
};

struct DecodedUniverse {
    PhysicsConfig config;
    std::array<DecodedBody, BodyManager::kMaxBodies> bodies = {};
    std::size_t body_count = 0;
};

struct UniverseDecodeResult {
    DecodedUniverse universe;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

UniverseEncodeResult encode_universe(const BodyManager &bodies,
                                     const PhysicsConfig &config);
UniverseDecodeResult decode_universe(const std::string &text);

} 
