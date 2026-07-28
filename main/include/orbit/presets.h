#pragma once

#include "orbit/physics_engine.h"

namespace orbit {

inline constexpr double kFigureEightBaseZoom = 4.5;

enum class PresetId {
    Empty,
    SolarSystem,
    ThreeBody,
    MilkyWay,
    FigureEight,
};

struct PresetView {
    const char *name;
    double base_zoom;
};

PresetView load_preset(PresetId id, BodyManager &bodies, PhysicsEngine &engine);

} 
