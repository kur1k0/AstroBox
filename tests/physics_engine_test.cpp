#include "orbit/body_colors.h"
#include "orbit/body_manager.h"
#include "orbit/body_size.h"
#include "orbit/force_solver.h"
#include "orbit/physics_engine.h"
#include "orbit/presets.h"

#include <cassert>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using orbit::Body;
using orbit::BodyManager;
using orbit::PairwiseForceSolver;
using orbit::PhysicsConfig;
using orbit::PhysicsEngine;
using orbit::PresetId;
using orbit::Vector2D;

struct Snapshot {
    int id;
    Vector2D pos;
};

std::vector<Snapshot> positions(const BodyManager &manager)
{
    std::vector<Snapshot> result;
    for (const Body &body : manager.bodies()) {
        if (body.active) result.push_back({body.id, body.pos});
    }
    return result;
}

double maximum_position_error(const BodyManager &manager,
                              const std::vector<Snapshot> &initial)
{
    double error = 0.0;
    for (const Snapshot &snapshot : initial) {
        const Body *body = manager.find(snapshot.id);
        assert(body);
        error = std::max(error, std::sqrt((body->pos - snapshot.pos).length_squared()));
    }
    return error;
}

void integrate_for(PhysicsEngine &engine, double duration, double dt)
{
    const int steps = static_cast<int>(std::lround(duration / dt));
    for (int i = 0; i < steps; ++i) engine.step(dt);
}

const Body *find_by_name(const BodyManager &manager, const char *name)
{
    for (const Body &body : manager.bodies()) {
        if (body.active && std::strcmp(body.name, name) == 0) return &body;
    }
    return nullptr;
}

bool is_allowed_color(std::uint32_t color)
{
    for (const orbit::BodyColorOption &option : orbit::kBodyColors) {
        if (option.rgb == color) return true;
    }
    return false;
}

void test_two_body_orbit()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    const double speed = std::sqrt(0.5);
    assert(bodies.create({"Aster", {-0.5, 0.0}, {0.0, -speed},
                          1.0, 0.035, 0xFF8C42, orbit::BodyType::Star}));
    assert(bodies.create({"Boreal", {0.5, 0.0}, {0.0, speed},
                          1.0, 0.035, 0x2D8CFF, orbit::BodyType::Star}));
    PhysicsConfig config;
    config.gravity = 1.0;
    config.softening = 0.001;
    config.fixed_dt = 0.001;
    config.collisions_enabled = false;
    engine.set_config(config);
    engine.initialize_accelerations();

    const auto initial = positions(bodies);
    const double initial_energy = engine.total_energy();
    const double period = 2.0 * std::acos(-1.0) / std::sqrt(2.0);
    integrate_for(engine, period, config.fixed_dt);

    const double relative_energy_error =
        std::fabs((engine.total_energy() - initial_energy) / initial_energy);
    const double orbit_error = maximum_position_error(bodies, initial);
    assert(relative_energy_error < 2.0e-6);
    assert(orbit_error < 0.003);
    assert(engine.total_momentum().length_squared() < 1.0e-20);

    std::cout << "two-body: position_error=" << orbit_error
              << " energy_error=" << relative_energy_error << '\n';
}

void test_figure_eight_and_write_csv(const std::string &csv_path)
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    orbit::load_preset(PresetId::FigureEight, bodies, engine);
    assert(find_by_name(bodies, "Astra"));
    assert(find_by_name(bodies, "Vega"));
    assert(find_by_name(bodies, "Nova"));
    PhysicsConfig config = engine.config();
    config.fixed_dt = 0.001;
    config.collisions_enabled = false;
    engine.set_config(config);
    engine.initialize_accelerations();

    const auto initial = positions(bodies);
    const double initial_energy = engine.total_energy();
    std::ofstream csv(csv_path);
    assert(csv.good());
    csv << "time,id,name,x,y,vx,vy\n";
    csv << std::setprecision(12);

    constexpr double period = 6.32591398;
    const int steps = static_cast<int>(std::lround(period / config.fixed_dt));
    for (int step = 0; step < steps; ++step) {
        engine.step(config.fixed_dt);
        if (step % 20 != 0) continue;
        for (const Body &body : bodies.bodies()) {
            if (!body.active) continue;
            csv << engine.simulation_time() << ',' << body.id << ',' << body.name << ','
                << body.pos.x << ',' << body.pos.y << ',' << body.vel.x << ','
                << body.vel.y << '\n';
        }
    }

    const double relative_energy_error =
        std::fabs((engine.total_energy() - initial_energy) / initial_energy);
    const double orbit_error = maximum_position_error(bodies, initial);
    assert(relative_energy_error < 5.0e-6);
    assert(orbit_error < 0.01);
    assert(engine.total_momentum().length_squared() < 1.0e-20);

    std::cout << "figure-eight: position_error=" << orbit_error
              << " energy_error=" << relative_energy_error << '\n';
}

void test_collision_merge()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    PhysicsConfig config;
    config.gravity = 0.0;
    config.softening = 0.001;
    config.collisions_enabled = true;
    engine.set_config(config);

    bodies.create({"Heavy", {-0.05, 0.0}, {1.0, 0.0}, 2.0, 0.06,
                   0xFFFFFF, orbit::BodyType::Star});
    bodies.create({"Light", {0.05, 0.0}, {-1.0, 0.0}, 1.0, 0.06,
                   0xFFFFFF, orbit::BodyType::Planet});
    const Vector2D momentum_before = engine.total_momentum();
    engine.step(0.01);

    assert(bodies.active_count() == 1);
    assert(engine.merge_count() == 1);
    const Body *merged = nullptr;
    for (const Body &body : bodies.bodies()) {
        if (body.active) merged = &body;
    }
    assert(merged);
    assert(std::fabs(merged->mass - 3.0) < 1.0e-12);
    assert(std::fabs(merged->radius - orbit::radius_for_mass(3.0)) < 1.0e-12);
    assert(std::fabs(merged->vel.x - (1.0 / 3.0)) < 1.0e-12);
    assert((engine.total_momentum() - momentum_before).length_squared() < 1.0e-20);
    std::cout << "collision: mass=" << merged->mass << " vx=" << merged->vel.x << '\n';
}

void test_destroy_clears_body()
{
    BodyManager bodies;
    Body *body = bodies.create({"DeleteMe", {1.0, 2.0}, {3.0, 4.0}, 5.0, 0.1,
                                0xFFFFFF, orbit::BodyType::Asteroid});
    assert(body);
    body->push_trail();
    const int id = body->id;
    assert(bodies.destroy(id));
    assert(!body->active);
    assert(body->id == -1);
    assert(body->trail_count == 0);
    assert(body->name[0] == '\0');
    assert(bodies.active_count() == 0);
    std::cout << "destroy: state and trail cleared\n";
}

void test_preset_trail_sampling_rate()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    orbit::load_preset(PresetId::FigureEight, bodies, engine);
    const PhysicsConfig config = engine.config();
    assert(config.trail_sample_steps == 40);

    Body *body = bodies.find(1);
    assert(body && body->trail_count == 1);
    for (int step = 0; step < config.trail_sample_steps - 1; ++step)
        engine.step(config.fixed_dt);
    assert(body->trail_count == 1);
    engine.step(config.fixed_dt);
    assert(body->trail_count == 2);
    std::cout << "trail: sample_steps=" << config.trail_sample_steps << '\n';
}

void test_solar_system_catalog()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    orbit::load_preset(PresetId::SolarSystem, bodies, engine);

    struct ExpectedBody {
        const char *name;
        std::uint32_t color;
        orbit::BodyType type;
    };
    static constexpr std::array<ExpectedBody, 9> expected = {{
        {"Sun", 0xFFE66D, orbit::BodyType::Star},
        {"Mercury", 0xFFFFFF, orbit::BodyType::Planet},
        {"Venus", 0xFF8C42, orbit::BodyType::Planet},
        {"Earth", 0x2D8CFF, orbit::BodyType::Planet},
        {"Mars", 0xFF4D4D, orbit::BodyType::Planet},
        {"Jupiter", 0xFF8C42, orbit::BodyType::Planet},
        {"Saturn", 0xFFE66D, orbit::BodyType::Planet},
        {"Uranus", 0x4ECDC4, orbit::BodyType::Planet},
        {"Neptune", 0x2D8CFF, orbit::BodyType::Planet},
    }};

    assert(bodies.active_count() == expected.size());
    assert(std::fabs(engine.config().gravity - 0.0001) < 1.0e-12);
    const Body *sun = find_by_name(bodies, "Sun");
    assert(sun && std::fabs(sun->mass - 1000.0) < 1.0e-12);
    for (const ExpectedBody &item : expected) {
        const Body *body = find_by_name(bodies, item.name);
        assert(body && body->color == item.color && body->type == item.type);
        assert(is_allowed_color(body->color));
    }
    assert(engine.total_momentum().length_squared() < 1.0e-20);
    integrate_for(engine, 120.0, engine.config().fixed_dt);
    assert(bodies.active_count() == expected.size());
    assert(engine.merge_count() == 0);
    std::cout << "solar-system: nine bodies stable for 120 simulated seconds\n";
}

void test_hierarchical_three_body_longevity()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    const orbit::PresetView view =
        orbit::load_preset(PresetId::ThreeBody, bodies, engine);

    assert(bodies.active_count() == 3);
    assert(std::strcmp(view.name, "3-BODY PROBLEM") == 0);
    for (const Body &body : bodies.bodies()) {
        if (!body.active) continue;
        assert(body.type == orbit::BodyType::Star);
        assert(is_allowed_color(body.color));
    }
    assert(engine.total_momentum().length_squared() < 1.0e-20);
    integrate_for(engine, 1500.0, engine.config().fixed_dt);
    assert(bodies.active_count() == 3);
    assert(engine.merge_count() == 0);
    for (const Body &body : bodies.bodies()) {
        if (!body.active) continue;
        assert(std::isfinite(body.pos.x) && std::isfinite(body.pos.y));
        assert(body.pos.length_squared() < 100.0);
    }
    std::cout << "three-body: hierarchical system survived 1500 simulated seconds\n";
}

void test_empty_universe()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    orbit::load_preset(PresetId::Empty, bodies, engine);
    assert(bodies.active_count() == 0);
    assert(engine.step_count() == 0);
}

void test_milky_way_capacity()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    const orbit::PresetView view =
        orbit::load_preset(PresetId::MilkyWay, bodies, engine);

    assert(bodies.active_count() == BodyManager::kMaxBodies);
    const Body *core = find_by_name(bodies, "Sagittarius A");
    assert(core && core->type == orbit::BodyType::Star);
    double maximum_x = 0.0;
    double maximum_y = 0.0;
    for (const Body &body : bodies.bodies()) {
        assert(body.active);
        assert(body.type == orbit::BodyType::Star);
        assert(is_allowed_color(body.color));
        maximum_x = std::max(maximum_x, std::fabs(body.pos.x));
        maximum_y = std::max(maximum_y, std::fabs(body.pos.y));
    }
    const double minimum_zoom = view.base_zoom * 0.5;
    std::cout << "milky-way: minimum-view half-extents="
              << maximum_x * minimum_zoom << 'x'
              << maximum_y * minimum_zoom << '\n';
    assert(maximum_x * minimum_zoom <= 119.0);
    assert(maximum_y * minimum_zoom <= 69.0);
    std::array<bool, orbit::kBodyColors.size()> colors_seen = {};
    for (const Body &body : bodies.bodies()) {
        if (body.type != orbit::BodyType::Star) continue;
        for (std::size_t i = 0; i < orbit::kBodyColors.size(); ++i) {
            if (body.color == orbit::kBodyColors[i].rgb) colors_seen[i] = true;
        }
    }
    assert(std::count(colors_seen.begin(), colors_seen.end(), true) >= 4);
    for (std::size_t ring = 0; ring < 7; ++ring) {
        const std::size_t first = 1 + ring * 9;
        bool varied = false;
        for (std::size_t slot = 1; slot < 9; ++slot) {
            if (bodies.bodies()[first + slot].color != bodies.bodies()[first].color) {
                varied = true;
                break;
            }
        }
        assert(varied);
    }
    integrate_for(engine, 30.0, engine.config().fixed_dt);
    assert(bodies.active_count() == BodyManager::kMaxBodies);
    assert(engine.merge_count() == 0);
    for (const Body &body : bodies.bodies()) {
        assert(std::isfinite(body.pos.x) && std::isfinite(body.pos.y));
        assert(body.pos.length_squared() < 40000.0);
    }
    std::cout << "milky-way: 64 bodies stable for 30 simulated seconds\n";
}

void test_preset_body_sizes_follow_mass()
{
    constexpr double maximum_body_zoom = 4.5 * 16.0;
    const int figure_eight_radius = static_cast<int>(std::lround(
        orbit::visual_radius_for_mass(1.0) * maximum_body_zoom));
    const int maximum_core_radius = static_cast<int>(std::lround(
        orbit::visual_radius_for_mass(1000.0) * maximum_body_zoom));
    assert(figure_eight_radius == 2);
    assert(maximum_core_radius == 5);
    assert(maximum_core_radius > figure_eight_radius);

    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    static constexpr std::array<PresetId, 4> presets = {
        PresetId::SolarSystem, PresetId::ThreeBody,
        PresetId::MilkyWay, PresetId::FigureEight,
    };
    for (PresetId preset : presets) {
        orbit::load_preset(preset, bodies, engine);
        for (const Body &body : bodies.bodies()) {
            if (!body.active) continue;
            assert(std::fabs(body.radius - orbit::radius_for_mass(body.mass)) < 1.0e-12);
            const long long displayed_mass = std::llround(body.mass * 100.0);
            assert(displayed_mass >= 10);
            assert(displayed_mass <= 100000);
            assert(displayed_mass != 1);
            assert(displayed_mass != 10);
            assert(body.type != orbit::BodyType::BlackHole);
        }
    }
}

void test_all_preset_colors_are_allowed()
{
    BodyManager bodies;
    PairwiseForceSolver solver;
    PhysicsEngine engine(bodies, solver);
    static constexpr std::array<PresetId, 4> presets = {
        PresetId::SolarSystem, PresetId::ThreeBody,
        PresetId::MilkyWay, PresetId::FigureEight,
    };
    for (PresetId preset : presets) {
        orbit::load_preset(preset, bodies, engine);
        for (const Body &body : bodies.bodies()) {
            if (body.active) assert(is_allowed_color(body.color));
        }
    }
}

} 

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_two_body_orbit();
    test_figure_eight_and_write_csv(argv[1]);
    test_collision_merge();
    test_destroy_clears_body();
    test_preset_trail_sampling_rate();
    test_solar_system_catalog();
    test_hierarchical_three_body_longevity();
    test_empty_universe();
    test_milky_way_capacity();
    test_preset_body_sizes_follow_mass();
    test_all_preset_colors_are_allowed();
    std::cout << "physics engine tests passed\n";
    return 0;
}
