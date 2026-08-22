#pragma once

#include "Engine/Engine.hpp"

namespace Game {
    // Owned, not an independent global - completes the same ownership chain as Engine owning
    // Renderer and Renderer owning TransferPipe: Engine has real RAII sub-members (transitively,
    // through Renderer/TransferPipe), so nesting it under Game - the literal program root -
    // makes construction/destruction order compiler-guaranteed instead of relying on every RAII
    // type staying carefully idempotent.
    inline Engine Engine;

    IncResult Create();
    void Destroy();

    void Run();
}

// Shortcuts to Game's owned components - single-hop ambient access from anywhere (GRenderer.X,
// not Game::Engine.Renderer.X), declared where the thing being aliased actually lives instead of
// in a separate top-level file. Every one of these is a reference into a real owned member - Game
// owns Engine owns Renderer owns TransferPipe/TerrainPass/ImGuiPass - there are no independent
// globals left in this chain.
inline Engine& GEngine = Game::Engine;
inline Renderer& GRenderer = GEngine.Renderer;
inline TransferPipe& GTransferPipe = GRenderer.TransferPipe;
inline TerrainPass& GTerrainPass = GRenderer.TerrainPass;
inline SkyPass& GSkyPass = GRenderer.SkyPass;
inline TextPass& GTextPass = GRenderer.TextPass;
inline ImGuiPass& GImGuiPass = GRenderer.ImGuiPass;
