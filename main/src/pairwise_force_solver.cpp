#include "orbit/force_solver.h"

#include "orbit/body_manager.h"

#include <cmath>

namespace orbit {

void PairwiseForceSolver::compute_accelerations(BodyManager &manager, double gravity,
                                                double softening)
{
    auto &bodies = manager.bodies();
    for (Body &body : bodies) {
        if (body.active) body.acc = {};
    }

    const double epsilon_squared = softening * softening;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        Body &a = bodies[i];
        if (!a.active) continue;
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            Body &b = bodies[j];
            if (!b.active) continue;

            const Vector2D delta = b.pos - a.pos;
            const double softened_r2 = delta.length_squared() + epsilon_squared;
            const double inverse_r = 1.0 / std::sqrt(softened_r2);
            const double inverse_r3 = inverse_r * inverse_r * inverse_r;
            const Vector2D direction_scale = delta * (gravity * inverse_r3);

            
            a.acc += direction_scale * b.mass;
            b.acc -= direction_scale * a.mass;
        }
    }
}

} 
