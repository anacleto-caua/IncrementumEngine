#pragma once

// One-shot proof that ComputePipe's whole path (pipeline creation, descriptor binding, push
// constants, dispatch, Ticket-based completion) actually works end to end - not a real gameplay
// system, a diagnostics/capability-proof concern, same spirit as the other Tools/ helpers.
namespace ComputeTestDemo {
    inline constexpr u32 TEST_ELEMENT_COUNT = 256; // 4 workgroups @ local_size_x=64
    inline constexpr u32 TEST_MULTIPLIER = 7;
    inline constexpr u32 TEST_OFFSET = 3;

    // Builds the test pipeline/layout/descriptor set/buffer, queues and flushes one dispatch,
    // reads the result back, and verifies it - logs the outcome either way.
    IncResult Init();
    void Destroy();
}
