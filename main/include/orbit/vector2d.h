#pragma once

#include <cmath>

namespace orbit {

struct Vector2D {
    double x = 0.0;
    double y = 0.0;

    Vector2D &operator+=(const Vector2D &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2D &operator-=(const Vector2D &other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2D &operator*=(double scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    double length_squared() const { return x * x + y * y; }
};

inline Vector2D operator+(Vector2D lhs, const Vector2D &rhs) { return lhs += rhs; }
inline Vector2D operator-(Vector2D lhs, const Vector2D &rhs) { return lhs -= rhs; }
inline Vector2D operator-(const Vector2D &value) { return {-value.x, -value.y}; }
inline Vector2D operator*(Vector2D value, double scalar) { return value *= scalar; }
inline Vector2D operator*(double scalar, Vector2D value) { return value *= scalar; }
inline Vector2D operator/(Vector2D value, double scalar) { return value *= (1.0 / scalar); }

} 
