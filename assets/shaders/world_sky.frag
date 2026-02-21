#version 330 core

in vec3 fragPos;
out vec4 FragColor;

uniform vec3 u_SunDir;
uniform float u_TimeOfDay;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += valueNoise(p) * a;
        p = p * 2.03 + vec2(7.1, 3.9);
        a *= 0.5;
    }
    return v;
}

float cloudLayer(vec2 p, float tod) {
    vec2 wind = vec2(tod * 0.08, tod * 0.03);
    float base = fbm(p * 1.4 + wind);
    float detail = fbm(p * 3.2 - wind * 1.8);
    float c = mix(base, detail, 0.35);
    return smoothstep(0.48, 0.78, c);
}

float godRays(vec3 dir, vec3 sunDir, float horizon) {
    float sunDot = max(dot(dir, sunDir), 0.0);
    float radial = pow(sunDot, 8.0);
    float streak = fbm(dir.xz * 18.0 + vec2(0.0, u_TimeOfDay * 0.02));
    float banded = smoothstep(0.42, 0.95, streak);
    float horizonMask = smoothstep(0.0, 0.55, horizon);
    return radial * banded * horizonMask;
}

vec3 SkyColor(vec3 dir) {
    vec3 sunDir = normalize(u_SunDir);
    float sun = max(0.0, dot(dir, sunDir));
    float day = sin(u_TimeOfDay * 6.2831853) * 0.5 + 0.5;
    float horizon = clamp(1.0 - max(dir.y, 0.0), 0.0, 1.0);

    vec3 nightSky = vec3(0.02, 0.05, 0.18);
    vec3 daySky = vec3(0.55, 0.82, 1.0);
    vec3 sky = mix(nightSky, daySky, day);

    vec3 horizonCool = mix(vec3(0.10, 0.15, 0.28), vec3(0.72, 0.84, 0.96), day);
    sky = mix(sky, horizonCool, horizon * (0.18 + 0.10 * day));

    // Sun halo stack: broad glow + focused core
    float sunGlow = pow(sun, 12.0);
    float sunCore = pow(sun, 42.0);
    sky += vec3(0.95, 0.97, 1.00) * sunGlow * 1.0;
    sky += vec3(1.00, 1.00, 1.00) * sunCore * 2.2;

    float cloud = cloudLayer(dir.xz * 2.8 + vec2(11.2, -4.7), u_TimeOfDay);
    vec3 cloudTint = mix(vec3(0.68, 0.74, 0.82), vec3(1.0), day);
    sky = mix(sky, cloudTint, cloud * (0.22 + 0.28 * day));

    float rays = 0.0;
    sky += vec3(1.0) * rays;

    // Slight haze near horizon to soften banding and improve depth feel.
    sky = mix(sky, vec3(0.88, 0.92, 1.0), horizon * 0.10 * day);

    // Tiny star field at night only (very low-cost hash noise).
    float night = 1.0 - day;
    float starNoise = hash21(dir.xz * 420.0 + vec2(17.3, 91.7));
    float stars = step(0.9965, starNoise) * (0.35 + 0.65 * starNoise);
    sky += vec3(stars) * night * 0.9;

    return sky;
}

void main() {
    vec3 color = SkyColor(normalize(fragPos));
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
