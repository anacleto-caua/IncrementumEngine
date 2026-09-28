// Shared atmosphere + lighting model - #included by sky.frag, terrain.frag and prop.frag so the
// sky, the light falling on opaque geometry and the fog that fades that geometry into the sky all
// derive from one set of scattering constants and can never drift apart.
//
// .glsl (not .vert/.frag) on purpose: the compile_shaders xmake rule only picks up stage
// extensions, so this is never compiled on its own - but it IS listed as a dependency of every
// stage shader there, so editing it recompiles all of them.
//
// Every value here is LINEAR, pre-tonemap radiance - scene shaders output it as-is into the HDR
// scene target, and PostPass tonemaps the whole frame exactly once.

const float PI = 3.14159265359;
const float PLANET_RADIUS = 6371e3;
const float ATMOSPHERE_RADIUS = 6471e3;
const vec3 RAYLEIGH_COEFFICIENT = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float MIE_COEFFICIENT = 21e-6;
const float RAYLEIGH_SCALE_HEIGHT = 8e3;
const float MIE_SCALE_HEIGHT = 1.2e3;
const float MIE_DIRECTION = 0.758;
const float SUN_INTENSITY = 22.0;
// Fixed height above the virtual planet's surface: the engine's world units have no real mapping
// to atmosphere-scale meters, so CameraPosition.y is deliberately NOT used here - only ray
// direction matters for an infinitely distant sky.
const float CAMERA_HEIGHT = 1000.0;

// Direct sunlight reaching a lit surface, relative to SUN_INTENSITY's sky scale. Tuned so a
// mid-grey albedo in full sun lands mid-range after the Reinhard tonemap.
const float SUN_LIGHT_INTENSITY = 3.2;

vec3 SrgbToLinear(vec3 c) {
    return pow(c, vec3(2.2));
}

vec2 RaySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, origin);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - 4.0 * a * c;
    if (d < 0.0) { return vec2(1e5, -1e5); }
    return vec2((-b - sqrt(d)) / (2.0 * a), (-b + sqrt(d)) / (2.0 * a));
}

// Single-scattering Rayleigh + Mie raymarch along `rayDir` - the sky's own color in that direction.
vec3 Atmosphere(vec3 rayDir, vec3 sunDir, int primarySteps, int secondarySteps) {
    vec3 origin = vec3(0.0, PLANET_RADIUS + CAMERA_HEIGHT, 0.0);

    vec2 primaryHit = RaySphereIntersect(origin, rayDir, ATMOSPHERE_RADIUS);
    if (primaryHit.x > primaryHit.y) { return vec3(0.0); }
    primaryHit.y = min(primaryHit.y, RaySphereIntersect(origin, rayDir, PLANET_RADIUS).x);
    float primaryStepSize = (primaryHit.y - primaryHit.x) / float(primarySteps);

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

    for (int i = 0; i < primarySteps; i++) {
        vec3 samplePos = origin + rayDir * (primaryTime + primaryStepSize * 0.5);
        float sampleHeight = length(samplePos) - PLANET_RADIUS;

        float stepOdRayleigh = exp(-sampleHeight / RAYLEIGH_SCALE_HEIGHT) * primaryStepSize;
        float stepOdMie = exp(-sampleHeight / MIE_SCALE_HEIGHT) * primaryStepSize;
        opticalDepthRayleigh += stepOdRayleigh;
        opticalDepthMie += stepOdMie;

        float secondaryStepSize = RaySphereIntersect(samplePos, sunDir, ATMOSPHERE_RADIUS).y / float(secondarySteps);
        float secondaryTime = 0.0;
        float secondaryOdRayleigh = 0.0;
        float secondaryOdMie = 0.0;

        for (int j = 0; j < secondarySteps; j++) {
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

// Fraction of sunlight surviving the trip through the atmosphere - analytic stand-in for the
// raymarch's own secondary (sun-ward) optical depth, cheap enough to run per pixel. Airmass grows
// as the sun nears the horizon, so blue scatters out first and low sun turns warm/red, matching
// the sky dome's own sunset tint.
vec3 SunTransmittance(vec3 sunDir) {
    float airmass = 1.0 / (max(sunDir.y, 0.0) + 0.035);
    vec3 verticalDepth = RAYLEIGH_COEFFICIENT * RAYLEIGH_SCALE_HEIGHT
                       + vec3(MIE_COEFFICIENT * 1.1 * MIE_SCALE_HEIGHT);
    return exp(-verticalDepth * airmass);
}

// Direct light: Lambert N.L times the sun's transmitted radiance. SunDirection points FROM the
// scene TOWARD the sun, so it's already the "L" vector - no negation.
vec3 SunLight(vec3 normal, vec3 sunDir) {
    float ndotl = max(dot(normal, sunDir), 0.0);
    return SUN_LIGHT_INTENSITY * SunTransmittance(sunDir) * ndotl;
}

// Hemispherical ambient - blue skylight from above, warm ground bounce from below, both fading
// with sun elevation. Replaces a flat constant ambient, which is what made shadowed slopes look
// grey and lifeless.
vec3 AmbientLight(vec3 normal, vec3 sunDir) {
    float dayFactor = smoothstep(-0.05, 0.35, sunDir.y);
    vec3 skyColor = vec3(0.30, 0.48, 0.85) * (0.08 + 0.55 * dayFactor);
    vec3 groundColor = vec3(0.30, 0.25, 0.18) * SunTransmittance(sunDir) * (0.03 + 0.25 * dayFactor);
    float hemisphere = normal.y * 0.5 + 0.5;
    return mix(groundColor, skyColor, hemisphere);
}

// Aerial perspective: blends lit geometry toward the sky's OWN color along the same view ray
// (clamped to at/above the horizon, since the sky dome has no below-horizon result worth fading
// into). Distant terrain then dissolves into exactly the sky behind it instead of into a fixed
// grey. The extra ramp to fully-fogged at `fogEnd` hides the edge of the streamed terrain disk.
vec3 ApplyAerialPerspective(vec3 color, vec3 worldPos, vec3 cameraPos, vec3 sunDir, float fogEnd) {
    vec3 toPoint = worldPos - cameraPos;
    float dist = length(toPoint);

    float fogFactor = 1.0 - exp(-dist * (2.2 / fogEnd));
    fogFactor = max(fogFactor, smoothstep(0.75 * fogEnd, fogEnd, dist));
    if (fogFactor < 0.002) { return color; }

    vec3 viewDir = toPoint / max(dist, 1e-3);
    vec3 horizonDir = normalize(vec3(viewDir.x, max(viewDir.y, 0.02), viewDir.z));
    vec3 skyColor = Atmosphere(horizonDir, sunDir, 8, 4);

    return mix(color, skyColor, fogFactor);
}
