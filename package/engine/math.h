#pragma once
#include <cmath>

namespace engine {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-6f ? v * (1.0f / l) : Vec3{};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
// Rotate v around a unit axis (Rodrigues).
inline Vec3 rotate(const Vec3& v, const Vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

} // namespace engine
