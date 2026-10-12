// Shared by the particle and mark draws (engine/vfx.c): the quad's corners and the shapes drawn in the fragment shader.
__exported import vfx_common; // what imports this sees vfx_common too

static const float2 CORNERS[6] = {
    float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
    float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)};

float hash2(int2 p)
{
    return unit(pcg((uint(p.x) * 73856093u) ^ (uint(p.y) * 19349663u)));
}

float value_noise2(float2 x)
{
    int2 i = int2(floor(x));
    float2 f = frac(x);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash2(i), hash2(i + int2(1, 0)), u.x), lerp(hash2(i + int2(0, 1)), hash2(i + int2(1, 1)), u.x), u.y);
}

// How much of the quad's pixel is lit: 1 at the center of a disc, 0 outside.
float shape_alpha(uint shape, float2 uv, uint seed)
{
    float r = length(uv);
    if (shape == 1u) {
        return smoothstep(0.55, 0.8, r) * (1.0 - smoothstep(0.8, 1.0, r));
    }
    if (shape == 2u) {
        float ax = clamp(1.0 - abs(uv.x), 0.0, 1.0);
        float ay = clamp(1.0 - abs(uv.y), 0.0, 1.0);
        return ax * ax * ay * ay * ay;
    }
    if (shape == 3u) {
        float2 offset = float2(float(seed & 255u), float((seed >> 8u) & 255u));
        float n = value_noise2(uv * 2.5 + offset) * 0.6 + value_noise2(uv * 5.0 + offset * 1.7) * 0.4;
        return smoothstep(0.0, 0.45, 1.0 - r + 0.55 * (n - 0.5));
    }
    float a = clamp(1.0 - r, 0.0, 1.0);
    return a * a * (3.0 - 2.0 * a);
}
