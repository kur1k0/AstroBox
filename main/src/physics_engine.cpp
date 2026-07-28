#include "orbit/physics_engine.h"

#include <algorithm>
#include <cmath>

namespace orbit {

PhysicsEngine::PhysicsEngine(BodyManager &bodies, ForceSolver &solver)
    : bodies_(bodies), solver_(solver)
{
}

void PhysicsEngine::set_config(const PhysicsConfig &config)
{
    config_ = config;
    accelerations_ready_ = false;
    accumulator_ = 0.0;
}

void PhysicsEngine::reset_clock()
{
    accumulator_ = 0.0;
    simulation_time_ = 0.0;
    step_count_ = 0;
    merge_count_ = 0;
    accelerations_ready_ = false;
}

void PhysicsEngine::initialize_accelerations()
{
    solver_.compute_accelerations(bodies_, config_.gravity, config_.softening);
    accelerations_ready_ = true;
}

void PhysicsEngine::merge_collisions()
{
    auto &bodies = bodies_.bodies();
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < bodies.size() && !merged; ++i) {
            if (!bodies[i].active) continue;
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                if (!bodies[j].active) continue;
                const Vector2D delta = bodies[j].pos - bodies[i].pos;
                const double contact_radius = bodies[i].radius + bodies[j].radius;
                if (delta.length_squared() >= contact_radius * contact_radius) continue;

                Body *survivor = &bodies[i];
                Body *absorbed = &bodies[j];
                if (absorbed->mass > survivor->mass ||
                    (absorbed->mass == survivor->mass && absorbed->id < survivor->id)) {
                    std::swap(survivor, absorbed);
                }
                bodies_.merge(*survivor, *absorbed);
                ++merge_count_;
                merged = true;
                break;
            }
        }
    }
}

void PhysicsEngine::step(double dt)
{
    if (dt <= 0.0) return;
    if (!accelerations_ready_) initialize_accelerations();

    
    
    for (Body &body : bodies_.bodies()) {
        if (!body.active) continue;
        body.vel += body.acc * (0.5 * dt);
        body.pos += body.vel * dt;
    }

    if (config_.collisions_enabled) merge_collisions();
    solver_.compute_accelerations(bodies_, config_.gravity, config_.softening);

    for (Body &body : bodies_.bodies()) {
        if (!body.active) continue;
        body.vel += body.acc * (0.5 * dt);
    }

    simulation_time_ += dt;
    ++step_count_;
    if (config_.trail_sample_steps > 0 &&
        step_count_ % static_cast<std::uint64_t>(config_.trail_sample_steps) == 0) {
        for (Body &body : bodies_.bodies()) {
            if (body.active) body.push_trail();
        }
    }
}

int PhysicsEngine::advance(double elapsed_seconds)
{
    accumulator_ += std::min(std::max(elapsed_seconds, 0.0), config_.max_frame_time);
    int steps = 0;
    while (accumulator_ >= config_.fixed_dt && steps < config_.max_steps_per_frame) {
        step(config_.fixed_dt);
        accumulator_ -= config_.fixed_dt;
        ++steps;
    }
    if (steps == config_.max_steps_per_frame) accumulator_ = 0.0;
    return steps;
}

Vector2D PhysicsEngine::total_momentum() const
{
    Vector2D momentum;
    for (const Body &body : bodies_.bodies()) {
        if (body.active) momentum += body.vel * body.mass;
    }
    return momentum;
}

double PhysicsEngine::total_energy() const
{
    double energy = 0.0;
    const auto &bodies = bodies_.bodies();
    for (const Body &body : bodies) {
        if (body.active) energy += 0.5 * body.mass * body.vel.length_squared();
    }
    const double epsilon_squared = config_.softening * config_.softening;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].active) continue;
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            if (!bodies[j].active) continue;
            const double distance = std::sqrt((bodies[j].pos - bodies[i].pos).length_squared() +
                                              epsilon_squared);
            energy -= config_.gravity * bodies[i].mass * bodies[j].mass / distance;
        }
    }
    return energy;
}

} 
