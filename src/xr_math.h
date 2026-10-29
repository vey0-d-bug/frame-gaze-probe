#pragma once

// Small vector, quaternion and matrix helpers for poses in OpenXR's
// convention: right-handed, +Y up, -Z forward, meters. Matrices are
// column-major (m[col * 4 + row]) to match GLSL.

#include <cmath>
#include <optional>

#include <openxr/openxr.h>

namespace gaze {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
// Returns `fallback` for a (near) zero vector instead of dividing by zero.
inline Vec3 normalize(Vec3 a, Vec3 fallback = {0, 0, -1}) {
    float len = length(a);
    return len > 1e-6f ? a * (1.0f / len) : fallback;
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// Rotates v by the unit quaternion q.
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = 2.0f * cross(u, v);
    return v + q.w * t + cross(u, t);
}

inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

// a * b rotates by b first, then by a.
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Rotation by `radians` about `axis` (right-handed).
inline Quat axis_angle(Vec3 axis, float radians) {
    Vec3 a = normalize(axis);
    float s = std::sin(radians * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
}

inline float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// The angle in radians of the rotation that takes unit quaternion a to b
// (atan2 rather than acos of the dot product, which in floats can't tell
// apart angles under about 0.05 degrees).
inline float angle_between(Quat a, Quat b) {
    Quat d = conjugate(a) * b;
    float s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return 2.0f * std::atan2(s, std::fabs(d.w));
}

// Spherical interpolation between unit quaternions along the shorter way:
// t = 0 gives a, t = 1 gives b.
inline Quat slerp(Quat a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float wa = 1.0f - t, wb = t;
    if (d < 0.9995f) {
        float theta = std::acos(d);
        float s = std::sin(theta);
        wa = std::sin(wa * theta) / s;
        wb = std::sin(wb * theta) / s;
    }
    Quat q{wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w};
    float len = std::sqrt(dot(q, q));
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

struct Pose {
    Quat orientation;
    Vec3 position;

    Vec3 transform(Vec3 p) const { return rotate(orientation, p) + position; }
    Vec3 forward() const { return rotate(orientation, {0, 0, -1}); }
    Vec3 right() const { return rotate(orientation, {1, 0, 0}); }
    Vec3 up() const { return rotate(orientation, {0, 1, 0}); }
};

inline Pose to_pose(const XrPosef& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(row, k) * b.at(k, col);
            r.at(row, col) = s;
        }
    }
    return r;
}

// (x, y, z, w) = m * (p, 1)
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};
inline Vec4 mul_point(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3),
            m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3)};
}

// World-from-pose matrix.
inline Mat4 pose_matrix(const Pose& p) {
    Mat4 r;
    Vec3 cx = rotate(p.orientation, {1, 0, 0});
    Vec3 cy = rotate(p.orientation, {0, 1, 0});
    Vec3 cz = rotate(p.orientation, {0, 0, 1});
    r.m[0] = cx.x, r.m[1] = cx.y, r.m[2] = cx.z;
    r.m[4] = cy.x, r.m[5] = cy.y, r.m[6] = cy.z;
    r.m[8] = cz.x, r.m[9] = cz.y, r.m[10] = cz.z;
    r.m[12] = p.position.x, r.m[13] = p.position.y, r.m[14] = p.position.z;
    return r;
}

// Pose-from-world matrix (the view matrix for an eye pose).
inline Mat4 view_matrix(const Pose& p) {
    Quat inv = conjugate(p.orientation);
    return pose_matrix({inv, rotate(inv, -p.position)});
}

// Projection for an OpenXR field of view into Vulkan clip space: x right,
// y down, depth 0 at `near_z` to 1 at `far_z`. Same matrix as the OpenXR SDK's
// XrMatrix4x4f_CreateProjectionFov with GRAPHICS_VULKAN.
inline Mat4 projection_matrix(const XrFovf& fov, float near_z, float far_z) {
    float tan_left = std::tan(fov.angleLeft);
    float tan_right = std::tan(fov.angleRight);
    // Vulkan's y axis points down, so up and down swap.
    float tan_down = std::tan(fov.angleUp);
    float tan_up = std::tan(fov.angleDown);
    float tan_width = tan_right - tan_left;
    float tan_height = tan_up - tan_down;

    Mat4 r;
    r.m[0] = 2.0f / tan_width;
    r.m[5] = 2.0f / tan_height;
    r.m[8] = (tan_right + tan_left) / tan_width;
    r.m[9] = (tan_up + tan_down) / tan_height;
    r.m[10] = -far_z / (far_z - near_z);
    r.m[11] = -1.0f;
    r.m[14] = -(far_z * near_z) / (far_z - near_z);
    r.m[15] = 0.0f;
    return r;
}

// Distance along a ray (unit `dir`) to a plane, if the ray hits its front or
// back within (0, max_t]. Rays parallel to the plane miss.
inline std::optional<float> ray_plane(Vec3 origin, Vec3 dir, Vec3 plane_point, Vec3 plane_normal, float max_t) {
    float denom = dot(dir, plane_normal);
    if (std::fabs(denom) < 1e-6f) return std::nullopt;
    float t = dot(plane_point - origin, plane_normal) / denom;
    if (t <= 0.0f || t > max_t) return std::nullopt;
    return t;
}

}  // namespace gaze
