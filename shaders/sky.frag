#version 450

struct SceneGlobals {
    mat4 ViewProjection;
    vec3 CameraPosition;
    mat4 InverseViewProjection;
    vec3 SunDirection;
};

layout(set = 0, binding = 0) uniform SceneGlobalsData {
    SceneGlobals data;
} sceneGlobalsData;

layout(location = 0) in vec3 inRayDir;
layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const int PRIMARY_STEPS = 16;
layout(constant_id = 1) const int SECONDARY_STEPS = 8;

const float PI = 3.14159265359;
const float PLANET_RADIUS = 6371e3;
const float ATMOSPHERE_RADIUS = 6471e3;
const vec3 RAYLEIGH_COEFFICIENT = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float MIE_COEFFICIENT = 21e-6;
const float RAYLEIGH_SCALE_HEIGHT = 8e3;
const float MIE_SCALE_HEIGHT = 1.2e3;
const float MIE_DIRECTION = 0.758;
const float SUN_INTENSITY = 22.0;
// Fixed height above the virtual planet's surface: the engine's world units (chunks are 50
// units wide, TerrainManager.hpp) have no real mapping to atmosphere-scale meters, so
// CameraPosition.y is deliberately NOT used here - only ray direction (already
// camera-position-relative, from sky.vert) matters for an infinitely distant sky.
const float CAMERA_HEIGHT = 1000.0;

vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, origin);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - 4.0 * a * c;
    if (d < 0.0) { return vec2(1e5, -1e5); }
    return vec2((-b - sqrt(d)) / (2.0 * a), (-b + sqrt(d)) / (2.0 * a));
}

vec3 atmosphere(vec3 rayDir, vec3 sunDir) {
    vec3 origin = vec3(0.0, PLANET_RADIUS + CAMERA_HEIGHT, 0.0);

    vec2 primaryHit = raySphereIntersect(origin, rayDir, ATMOSPHERE_RADIUS);
    if (primaryHit.x > primaryHit.y) { return vec3(0.0); }
    primaryHit.y = min(primaryHit.y, raySphereIntersect(origin, rayDir, PLANET_RADIUS).x);
    float primaryStepSize = (primaryHit.y - primaryHit.x) / float(PRIMARY_STEPS);

    float primaryTime = 0.0;
    vec3 totalRayleigh = vec3(0.0);
    vec3 totalMie = vec3(0.0);
    float opticalDepthRayleigh = 0.0;
    float opticalDepthMie = 0.0;

    float mu = dot(rayDir, sunDir);
    float phaseRayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g2 = MIE_DIRECTION * MIE_DIRECTION;
    float phaseMie = 3.0 / (8.0 * PI) * ((1.0 - g2) * (mu * mu + 1.0))
        / (pow(1.0 + g2 - 2.0 * mu * MIE_DIRECTION, 1.5) * (2.0 + g2));

    for (int i = 0; i < PRIMARY_STEPS; i++) {
        vec3 samplePos = origin + rayDir * (primaryTime + primaryStepSize * 0.5);
        float sampleHeight = length(samplePos) - PLANET_RADIUS;

        float stepOdRayleigh = exp(-sampleHeight / RAYLEIGH_SCALE_HEIGHT) * primaryStepSize;
        float stepOdMie = exp(-sampleHeight / MIE_SCALE_HEIGHT) * primaryStepSize;
        opticalDepthRayleigh += stepOdRayleigh;
        opticalDepthMie += stepOdMie;

        float secondaryStepSize = raySphereIntersect(samplePos, sunDir, ATMOSPHERE_RADIUS).y / float(SECONDARY_STEPS);
        float secondaryTime = 0.0;
        float secondaryOdRayleigh = 0.0;
        float secondaryOdMie = 0.0;

        for (int j = 0; j < SECONDARY_STEPS; j++) {
            vec3 secondarySamplePos = samplePos + sunDir * (secondaryTime + secondaryStepSize * 0.5);
            float secondaryHeight = length(secondarySamplePos) - PLANET_RADIUS;
            secondaryOdRayleigh += exp(-secondaryHeight / RAYLEIGH_SCALE_HEIGHT) * secondaryStepSize;
            secondaryOdMie += exp(-secondaryHeight / MIE_SCALE_HEIGHT) * secondaryStepSize;
            secondaryTime += secondaryStepSize;
        }

        vec3 attenuation = exp(-(MIE_COEFFICIENT * (opticalDepthMie + secondaryOdMie)
            + RAYLEIGH_COEFFICIENT * (opticalDepthRayleigh + secondaryOdRayleigh)));

        totalRayleigh += stepOdRayleigh * attenuation;
        totalMie += stepOdMie * attenuation;
        primaryTime += primaryStepSize;
    }

    return SUN_INTENSITY * (phaseRayleigh * RAYLEIGH_COEFFICIENT * totalRayleigh + phaseMie * MIE_COEFFICIENT * totalMie);
}

void main() {
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);
    vec3 color = atmosphere(normalize(inRayDir), sunDir);

    // Reinhard tonemap - the raymarch above returns unbounded HDR-ish values.
    color = color / (color + vec3(1.0));
    outColor = vec4(color, 1.0);
}
