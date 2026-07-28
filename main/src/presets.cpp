#include "orbit/presets.h"
#include "orbit/body_size.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace orbit {

namespace {

std::uint32_t milky_way_star_color(std::uint32_t star_number)
{
    
    
    
    static constexpr std::array<std::uint32_t, 16> colors = {{
        0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
        0xFFE66D, 0xFFE66D, 0xFFE66D,
        0xFF8C42, 0xFF8C42,
        0x2D8CFF, 0x2D8CFF,
        0x4ECDC4, 0xFFFFFF, 0xFF4D4D,
    }};
    std::uint32_t hash = star_number + 0x9E3779B9U;
    hash ^= hash >> 16U;
    hash *= 0x7FEB352DU;
    hash ^= hash >> 15U;
    hash *= 0x846CA68BU;
    hash ^= hash >> 16U;
    return colors[hash % colors.size()];
}

Body *add(BodyManager &bodies, const char *name, Vector2D pos, Vector2D vel,
          double mass, std::uint32_t color, BodyType type)
{
    return bodies.create({name, pos, vel, mass, radius_for_mass(mass), color, type});
}

void finish_preset(BodyManager &bodies, PhysicsEngine &engine,
                   const PhysicsConfig &config)
{
    engine.set_config(config);
    engine.reset_clock();
    for (Body &body : bodies.bodies()) {
        if (body.active) body.clear_trail();
    }
    engine.initialize_accelerations();
}

} 

PresetView load_preset(PresetId id, BodyManager &bodies, PhysicsEngine &engine)
{
    bodies.clear();
    PhysicsConfig config;
    config.gravity = 1.0;
    config.softening = 0.001;
    config.fixed_dt = 1.0 / 240.0;
    config.max_frame_time = 0.25;
    config.max_steps_per_frame = 64;
    config.trail_sample_steps = 40;
    config.collisions_enabled = true;

    if (id == PresetId::Empty) {
        finish_preset(bodies, engine, config);
        return {"EMPTY UNIVERSE", kFigureEightBaseZoom};
    }

    if (id == PresetId::SolarSystem) {
        constexpr double base_zoom = 1.75;
        config.gravity = 0.0001;
        config.softening = 0.002;
        constexpr double sun_mass = 1000.0;
        add(bodies, "Sun", {}, {}, sun_mass,
            0xFFE66D, BodyType::Star);

        struct PlanetPreset {
            const char *name;
            double orbit;
            double angle_degrees;
            double mass;
            std::uint32_t color;
        };
        static constexpr std::array<PlanetPreset, 8> planets = {{
            {"Mercury", 0.39,   0.0, 0.20, 0xFFFFFF},
            {"Venus",   0.72,  55.0, 0.20, 0xFF8C42},
            {"Earth",   1.00, 115.0, 0.20, 0x2D8CFF},
            {"Mars",    1.52, 180.0, 0.20, 0xFF4D4D},
            {"Jupiter", 5.20, 235.0, 2.00, 0xFF8C42},
            {"Saturn",  9.58, 290.0, 0.60, 0xFFE66D},
            {"Uranus", 19.20,  25.0, 0.20, 0x4ECDC4},
            {"Neptune",30.05, 155.0, 0.20, 0x2D8CFF},
        }};

        constexpr double pi = 3.14159265358979323846;
        Vector2D heliocentric_momentum;
        double total_mass = sun_mass;
        for (const PlanetPreset &planet : planets) {
            const double angle = planet.angle_degrees * pi / 180.0;
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            const double speed = std::sqrt(
                config.gravity * (sun_mass + planet.mass) / planet.orbit);
            const Vector2D velocity = {-sine * speed, cosine * speed};
            add(bodies, planet.name,
                {planet.orbit * cosine, planet.orbit * sine}, velocity,
                planet.mass, planet.color, BodyType::Planet);
            heliocentric_momentum += velocity * planet.mass;
            total_mass += planet.mass;
        }

        
        const Vector2D frame_velocity = -heliocentric_momentum / total_mass;
        Vector2D weighted_position;
        for (Body &body : bodies.bodies()) {
            if (!body.active) continue;
            body.vel += frame_velocity;
            weighted_position += body.pos * body.mass;
        }
        const Vector2D center_of_mass = weighted_position / total_mass;
        for (Body &body : bodies.bodies()) {
            if (body.active) body.pos -= center_of_mass;
        }

        finish_preset(bodies, engine, config);
        return {"SOLAR SYSTEM", base_zoom};
    }

    if (id == PresetId::ThreeBody) {
        constexpr double base_zoom = 1.375;
        
        
        
        config.gravity = 0.2;
        config.softening = 0.003;
        constexpr double inner_mass = 1.0;
        constexpr double outer_mass = 0.5;
        constexpr double inner_separation = 1.0;
        constexpr double outer_separation = 6.0;
        constexpr double total_mass = inner_mass * 2.0 + outer_mass;
        constexpr double inner_center_x =
            -outer_mass * outer_separation / total_mass;
        constexpr double outer_x =
            inner_mass * 2.0 * outer_separation / total_mass;
        const double inner_speed = std::sqrt(
            config.gravity * inner_mass / (2.0 * inner_separation));
        const double outer_angular_speed = std::sqrt(
            config.gravity * total_mass /
            (outer_separation * outer_separation * outer_separation));
        const double inner_center_speed = outer_angular_speed * inner_center_x;
        const double outer_speed = outer_angular_speed * outer_x;

        add(bodies, "Aurelia",
            {inner_center_x - inner_separation * 0.5, 0.0},
            {0.0, inner_center_speed - inner_speed}, inner_mass,
            0xFFE66D, BodyType::Star);
        add(bodies, "Ember",
            {inner_center_x + inner_separation * 0.5, 0.0},
            {0.0, inner_center_speed + inner_speed}, inner_mass,
            0xFF8C42, BodyType::Star);
        add(bodies, "Ivory", {outer_x, 0.0}, {0.0, outer_speed},
            outer_mass, 0xFFFFFF, BodyType::Star);
        finish_preset(bodies, engine, config);
        return {"3-BODY PROBLEM", base_zoom};
    }

    if (id == PresetId::MilkyWay) {
        
        
        
        constexpr double base_zoom = 0.375;
        config.gravity = 0.001;
        config.softening = 0.10;
        config.fixed_dt = 1.0 / 180.0;
        config.max_steps_per_frame = 48;
        config.trail_sample_steps = 24;
        constexpr double core_mass = 1000.0;
        constexpr double orbiter_mass = 1.0;
        constexpr int stars_per_ring = 9;
        constexpr int ring_count = 7;
        constexpr double pi = 3.14159265358979323846;
        Body *core = add(bodies, "Sagittarius A", {}, {}, core_mass,
                         0xFFFFFF, BodyType::Star);
        Vector2D stellar_momentum;
        int star_number = 1;
        for (int ring = 0; ring < ring_count; ++ring) {
            const double semi_major = 13.0 + static_cast<double>(ring) * 12.0;
            const double eccentricity = 0.30 + 0.025 * (ring % 4);
            const double semi_latus =
                semi_major * (1.0 - eccentricity * eccentricity);
            const double enclosed_mass =
                core_mass + orbiter_mass * (ring * stars_per_ring + stars_per_ring * 0.5);
            const double velocity_scale =
                std::sqrt(config.gravity * enclosed_mass / semi_latus);
            const double apsis = static_cast<double>(ring) * 0.08;
            const double apsis_cosine = std::cos(apsis);
            const double apsis_sine = std::sin(apsis);
            const double phase = static_cast<double>(ring) * pi / stars_per_ring;
            for (int slot = 0; slot < stars_per_ring; ++slot) {
                const double anomaly = phase + 2.0 * pi * slot / stars_per_ring;
                const double cosine = std::cos(anomaly);
                const double sine = std::sin(anomaly);
                const double radius = semi_latus / (1.0 + eccentricity * cosine);
                const Vector2D orbital_position = {radius * cosine, radius * sine};
                const Vector2D orbital_velocity = {
                    -velocity_scale * sine,
                    velocity_scale * (eccentricity + cosine),
                };
                const Vector2D position = {
                    orbital_position.x * apsis_cosine - orbital_position.y * apsis_sine,
                    orbital_position.x * apsis_sine + orbital_position.y * apsis_cosine,
                };
                const Vector2D velocity = {
                    orbital_velocity.x * apsis_cosine - orbital_velocity.y * apsis_sine,
                    orbital_velocity.x * apsis_sine + orbital_velocity.y * apsis_cosine,
                };
                char name[16];
                const int current_star = star_number++;
                std::snprintf(name, sizeof(name), "Star %02d", current_star);
                add(bodies, name, position, velocity, orbiter_mass,
                    milky_way_star_color(static_cast<std::uint32_t>(current_star)),
                    BodyType::Star);
                stellar_momentum += velocity * orbiter_mass;
            }
        }
        if (core) core->vel = -stellar_momentum / core_mass;
        finish_preset(bodies, engine, config);
        return {"MILKY WAY", base_zoom};
    }

    
    add(bodies, "Astra", {-0.97000436, 0.24308753}, {0.466203685, 0.432365730},
        1.0, 0xFFE66D, BodyType::Star);
    add(bodies, "Vega", {0.97000436, -0.24308753}, {0.466203685, 0.432365730},
        1.0, 0xFF8C42, BodyType::Star);
    add(bodies, "Nova", {0.0, 0.0}, {-0.93240737, -0.86473146},
        1.0, 0xFFFFFF, BodyType::Star);
    finish_preset(bodies, engine, config);
    return {"FIGURE 8", kFigureEightBaseZoom};
}

} 
