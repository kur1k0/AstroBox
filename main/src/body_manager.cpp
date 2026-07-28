#include "orbit/body_manager.h"
#include "orbit/body_size.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orbit {

void Body::clear_trail()
{
    trail_head = 0;
    trail_count = 0;
    push_trail();
}

void Body::push_trail()
{
    trail[trail_head] = pos;
    trail_head = (trail_head + 1) % kTrailSize;
    trail_count = std::min(trail_count + 1, kTrailSize);
}

const Vector2D &Body::trail_at(std::size_t chronological_index) const
{
    const std::size_t oldest = (trail_head + kTrailSize - trail_count) % kTrailSize;
    return trail[(oldest + chronological_index) % kTrailSize];
}

Body *BodyManager::create(const BodySpec &spec)
{
    if (spec.mass <= 0.0 || spec.radius <= 0.0) return nullptr;
    for (Body &body : bodies_) {
        if (body.active) continue;
        body = Body{};
        body.id = next_id_++;
        body.active = true;
        std::strncpy(body.name, spec.name ? spec.name : "Body", sizeof(body.name) - 1);
        body.pos = spec.pos;
        body.vel = spec.vel;
        body.mass = spec.mass;
        body.radius = spec.radius;
        body.color = spec.color;
        body.type = spec.type;
        body.clear_trail();
        return &body;
    }
    return nullptr;
}

bool BodyManager::destroy(int id)
{
    Body *body = find(id);
    if (!body) return false;
    *body = Body{};
    return true;
}

void BodyManager::clear()
{
    bodies_ = {};
    next_id_ = 1;
}

Body *BodyManager::find(int id)
{
    for (Body &body : bodies_) {
        if (body.active && body.id == id) return &body;
    }
    return nullptr;
}

const Body *BodyManager::find(int id) const
{
    for (const Body &body : bodies_) {
        if (body.active && body.id == id) return &body;
    }
    return nullptr;
}

std::size_t BodyManager::active_count() const
{
    std::size_t count = 0;
    for (const Body &body : bodies_) count += body.active ? 1U : 0U;
    return count;
}

Body &BodyManager::merge(Body &survivor, Body &absorbed)
{
    const double total_mass = survivor.mass + absorbed.mass;
    survivor.pos = (survivor.pos * survivor.mass + absorbed.pos * absorbed.mass) / total_mass;
    survivor.vel = (survivor.vel * survivor.mass + absorbed.vel * absorbed.mass) / total_mass;
    survivor.mass = total_mass;
    survivor.radius = radius_for_mass(total_mass);
    survivor.acc = {};
    survivor.clear_trail();
    absorbed.active = false;
    return survivor;
}

} 
