#pragma once

#include "orbit/body.h"

#include <array>
#include <cstddef>

namespace orbit {

class BodyManager {
public:
    static constexpr std::size_t kMaxBodies = 64;

    Body *create(const BodySpec &spec);
    bool destroy(int id);
    void clear();

    Body *find(int id);
    const Body *find(int id) const;
    std::size_t active_count() const;

    Body &merge(Body &survivor, Body &absorbed);

    std::array<Body, kMaxBodies> &bodies() { return bodies_; }
    const std::array<Body, kMaxBodies> &bodies() const { return bodies_; }

private:
    std::array<Body, kMaxBodies> bodies_ = {};
    int next_id_ = 1;
};

} 
