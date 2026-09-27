#pragma once

#include "nv/base.h"

#include <math.h>

// NOTE: Right-handed, Y up. Cameras look down -Z. Matrices are column-major (e[column * 4 + row]),
// matching WGSL's mat4x4f, and projections map depth to WebGPU's 0..1 range.

#define NV_PI 3.14159265358979323846f

typedef struct NvVec3 { f32 x, y, z; } NvVec3;
typedef struct NvQuat { f32 x, y, z, w; } NvQuat;
typedef struct NvMat4 { f32 e[16]; } NvMat4;

static inline NvVec3 nv_vec3(f32 x, f32 y, f32 z) { return (NvVec3){x, y, z}; }
static inline NvVec3 nv_vec3_add(NvVec3 a, NvVec3 b) { return (NvVec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline NvVec3 nv_vec3_sub(NvVec3 a, NvVec3 b) { return (NvVec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline NvVec3 nv_vec3_scale(NvVec3 v, f32 s) { return (NvVec3){v.x * s, v.y * s, v.z * s}; }
static inline f32 nv_vec3_dot(NvVec3 a, NvVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static inline NvVec3 nv_vec3_cross(NvVec3 a, NvVec3 b)
{
    return (NvVec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static inline NvVec3 nv_vec3_normalize(NvVec3 v)
{
    f32 length = sqrtf(nv_vec3_dot(v, v));
    NV_ASSERT(length > 0.0f);
    return nv_vec3_scale(v, 1.0f / length);
}

static inline NvQuat nv_quat_identity(void) { return (NvQuat){0.0f, 0.0f, 0.0f, 1.0f}; }

// `axis` must be normalized; `angle` is in radians.
static inline NvQuat nv_quat_axis_angle(NvVec3 axis, f32 angle)
{
    f32 s = sinf(angle * 0.5f);
    return (NvQuat){axis.x * s, axis.y * s, axis.z * s, cosf(angle * 0.5f)};
}

// Rotation that applies `b` first, then `a`.
static inline NvQuat nv_quat_mul(NvQuat a, NvQuat b)
{
    return (NvQuat){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

static inline NvMat4 nv_mat4_identity(void)
{
    NvMat4 m = {0};
    m.e[0] = m.e[5] = m.e[10] = m.e[15] = 1.0f;
    return m;
}

static inline NvMat4 nv_mat4_mul(NvMat4 a, NvMat4 b)
{
    NvMat4 r;
    for (u32 col = 0; col < 4; ++col) {
        for (u32 row = 0; row < 4; ++row) {
            f32 sum = 0.0f;
            for (u32 k = 0; k < 4; ++k)
                sum += a.e[k * 4 + row] * b.e[col * 4 + k];
            r.e[col * 4 + row] = sum;
        }
    }
    return r;
}

// Translation * rotation * scale.
static inline NvMat4 nv_mat4_trs(NvVec3 t, NvQuat q, NvVec3 s)
{
    f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    NvMat4 m;
    m.e[0] = (1.0f - 2.0f * (yy + zz)) * s.x;
    m.e[1] = (2.0f * (xy + wz)) * s.x;
    m.e[2] = (2.0f * (xz - wy)) * s.x;
    m.e[3] = 0.0f;
    m.e[4] = (2.0f * (xy - wz)) * s.y;
    m.e[5] = (1.0f - 2.0f * (xx + zz)) * s.y;
    m.e[6] = (2.0f * (yz + wx)) * s.y;
    m.e[7] = 0.0f;
    m.e[8] = (2.0f * (xz + wy)) * s.z;
    m.e[9] = (2.0f * (yz - wx)) * s.z;
    m.e[10] = (1.0f - 2.0f * (xx + yy)) * s.z;
    m.e[11] = 0.0f;
    m.e[12] = t.x;
    m.e[13] = t.y;
    m.e[14] = t.z;
    m.e[15] = 1.0f;
    return m;
}

// Right-handed perspective with depth mapped to 0 (near) .. 1 (far). `fov_y` is in radians.
static inline NvMat4 nv_mat4_perspective(f32 fov_y, f32 aspect, f32 near_z, f32 far_z)
{
    f32 f = 1.0f / tanf(fov_y * 0.5f);
    NvMat4 m = {0};
    m.e[0] = f / aspect;
    m.e[5] = f;
    m.e[10] = far_z / (near_z - far_z);
    m.e[11] = -1.0f;
    m.e[14] = near_z * far_z / (near_z - far_z);
    return m;
}

// Right-handed orthographic with depth mapped to 0 (near) .. 1 (far).
static inline NvMat4 nv_mat4_orthographic(f32 height, f32 aspect, f32 near_z, f32 far_z)
{
    f32 half_h = height * 0.5f;
    f32 half_w = half_h * aspect;
    NvMat4 m = {0};
    m.e[0] = 1.0f / half_w;
    m.e[5] = 1.0f / half_h;
    m.e[10] = 1.0f / (near_z - far_z);
    m.e[14] = near_z / (near_z - far_z);
    m.e[15] = 1.0f;
    return m;
}

// General inverse (cofactor expansion). Asserts on a singular matrix.
static inline NvMat4 nv_mat4_inverse(NvMat4 m)
{
    const f32* a = m.e;
    NvMat4 r;
    f32* inv = r.e;

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    f32 det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    NV_ASSERT(det != 0.0f);
    f32 inv_det = 1.0f / det;
    for (u32 i = 0; i < 16; ++i)
        inv[i] *= inv_det;
    return r;
}

static inline NvVec3 nv_mat4_translation(NvMat4 m) { return (NvVec3){m.e[12], m.e[13], m.e[14]}; }

static inline NvVec3 nv_mat4_transform_point(NvMat4 m, NvVec3 p)
{
    return (NvVec3){m.e[0] * p.x + m.e[4] * p.y + m.e[8] * p.z + m.e[12],
                    m.e[1] * p.x + m.e[5] * p.y + m.e[9] * p.z + m.e[13],
                    m.e[2] * p.x + m.e[6] * p.y + m.e[10] * p.z + m.e[14]};
}

// The direction the local -Z axis points in world space (where a camera or light faces).
static inline NvVec3 nv_mat4_forward(NvMat4 m)
{
    return nv_vec3_normalize((NvVec3){-m.e[8], -m.e[9], -m.e[10]});
}

// Rotates `v` by the unit quaternion `q`.
static inline NvVec3 nv_quat_rotate(NvQuat q, NvVec3 v)
{
    NvVec3 u = nv_vec3(q.x, q.y, q.z);
    NvVec3 t = nv_vec3_scale(nv_vec3_cross(u, v), 2.0f);
    return nv_vec3_add(nv_vec3_add(v, nv_vec3_scale(t, q.w)), nv_vec3_cross(u, t));
}

// Splits a matrix without shear into translation, rotation and scale (the inverse of nv_mat4_trs).
static inline void nv_mat4_decompose(NvMat4 m, NvVec3* translation, NvQuat* rotation, NvVec3* scale)
{
    const f32* e = m.e;
    *translation = nv_vec3(e[12], e[13], e[14]);
    NvVec3 s = nv_vec3(sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]),
                       sqrtf(e[4] * e[4] + e[5] * e[5] + e[6] * e[6]),
                       sqrtf(e[8] * e[8] + e[9] * e[9] + e[10] * e[10]));
    NV_ASSERT(s.x > 0.0f && s.y > 0.0f && s.z > 0.0f);
    *scale = s;

    // Rotation part, rows and columns named as in math notation (m_rowcol).
    f32 m00 = e[0] / s.x, m10 = e[1] / s.x, m20 = e[2] / s.x;
    f32 m01 = e[4] / s.y, m11 = e[5] / s.y, m21 = e[6] / s.y;
    f32 m02 = e[8] / s.z, m12 = e[9] / s.z, m22 = e[10] / s.z;
    f32 trace = m00 + m11 + m22;
    NvQuat q;
    if (trace > 0.0f) {
        f32 k = sqrtf(trace + 1.0f) * 2.0f;
        q = (NvQuat){(m21 - m12) / k, (m02 - m20) / k, (m10 - m01) / k, 0.25f * k};
    } else if (m00 > m11 && m00 > m22) {
        f32 k = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q = (NvQuat){0.25f * k, (m01 + m10) / k, (m02 + m20) / k, (m21 - m12) / k};
    } else if (m11 > m22) {
        f32 k = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q = (NvQuat){(m01 + m10) / k, 0.25f * k, (m12 + m21) / k, (m02 - m20) / k};
    } else {
        f32 k = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q = (NvQuat){(m02 + m20) / k, (m12 + m21) / k, 0.25f * k, (m10 - m01) / k};
    }
    *rotation = q;
}
