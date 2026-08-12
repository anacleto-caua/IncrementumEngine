#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec3 debugColor;

layout(location = 0) out vec4 outColor;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const float GRID_CELLS = 63.0;

void main()
{
    // Scale UVs to grid space (0..63)
    vec2 pos = texCoord * GRID_CELLS;

    // Compute "Pixel Width" in grid units
    // fwidth: How much does 'pos' change between this pixel and the neighbor
    // This allows us to keep lines 1 pixel thick visually.
    vec2 derivative = fwidth(pos);

    // Distance to nearest line
    // Highlight where 'pos' is close to an integer.
    // Centers the line on the integer boundary.
    vec2 grid = abs(fract(pos - 0.5) - 0.5) / derivative;

    // Combine X and Z lines
    // 'line' is the distance (in pixels) to the closest line
    float line = min(grid.x, grid.y);

    // If within 1.0 pixel of the line, draw it
    // 1.0 = Center of line, 0.0 = Background
    float intensity = 1.0 - min(line, 1.0);

    // Color mixing
    // Background = debugColor
    // Line = White (vec3(1.0))
    vec3 finalColor = mix(debugColor, vec3(1.0), intensity);

    outColor = vec4(finalColor, 1.0);
}
