#pragma once

struct Camera;

// Diagnostic tooling for terrain streaming issues - not meant to stay long-term. Writes to a
// plain-text log (terrain_debug_log.txt, next to the executable): a ~0.5s periodic per-ring state
// dump (Drawn/Culled/Dark/GenerationsInFlight/etc. plus player position/look), and an F6 "mark"
// tool that ray-marches the camera's look direction and logs a full TerrainManager::ChunkDiagnostic
// for every distinct chunk crossed.
namespace TerrainDebugTools {
    void Init(Camera& camera);
    void Update(f32 delta_time);
}
