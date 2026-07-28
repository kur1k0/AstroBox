#pragma once

#include "orbit/body_manager.h"
#include "orbit/force_solver.h"

#include <cstdint>

namespace orbit {

struct PhysicsConfig {
    double gravity = 1.0;
    double softening = 0.001;
    double fixed_dt = 1.0 / 240.0;
    double max_frame_time = 0.10;
    int max_steps_per_frame = 32;
    int trail_sample_steps = 8;
    bool collisions_enabled = true;
};

class PhysicsEngine {
public:
    PhysicsEngine(BodyManager &bodies, ForceSolver &solver);

    void set_config(const PhysicsConfig &config);
    const PhysicsConfig &config() const { return config_; }
    void reset_clock();
    void initialize_accelerations();
    void step(double dt);
    int advance(double elapsed_seconds);

    double simulation_time() const { return simulation_time_; }
    std::uint64_t step_count() const { return step_count_; }
    std::uint64_t merge_count() const { return merge_count_; }

    Vector2D total_momentum() const;
    double total_energy() const;

private:
    void merge_collisions();

    BodyManager &bodies_;
    ForceSolver &solver_;
    PhysicsConfig config_;
    double accumulator_ = 0.0;
    double simulation_time_ = 0.0;
    std::uint64_t step_count_ = 0;
    std::uint64_t merge_count_ = 0;
    bool accelerations_ready_ = false;
};

} 
