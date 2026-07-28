#pragma once

namespace orbit {

class BodyManager;

class ForceSolver {
public:
    virtual ~ForceSolver() = default;
    virtual void compute_accelerations(BodyManager &bodies, double gravity,
                                       double softening) = 0;
};

class PairwiseForceSolver final : public ForceSolver {
public:
    void compute_accelerations(BodyManager &bodies, double gravity,
                               double softening) override;
};

} 
