#pragma once

#include <chrono>
#include <thread>

class FrameTimer {
private:
    static constexpr f32 DEFAULT_TARGET_FPS = 60;

    using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;
    TimePoint last_frame_time;
    TimePoint frame_begin;

    std::chrono::duration<f32> target_frame_time = std::chrono::duration<f32>(1.0f / DEFAULT_TARGET_FPS);

    TimePoint Now() {
        return std::chrono::high_resolution_clock::now();
    }

public:
    FrameTimer(f32 target_fps) {
        SetTargetFPS(target_fps);
        last_frame_time = std::chrono::high_resolution_clock::now();
    }

    void SetTargetFPS(f32 target_fps) {
        target_frame_time = std::chrono::duration<f32>(1.0f / target_fps);
    }

    // Call at the very beginning of the loop to get delta_time
    f32 Tick() {
        frame_begin = Now();
        std::chrono::duration<f32> delta = frame_begin - last_frame_time;
        last_frame_time = frame_begin;
        return delta.count();
    }

    // Call at the very end of the loop to cap the framerate
    void Sleep() {
        auto frame_end = Now();
        auto frame_elapsed_time = frame_end - frame_begin;

        if (frame_elapsed_time < target_frame_time) {
            auto time_to_sleep = target_frame_time - frame_elapsed_time;

            // OS sleep to yield CPU (minus 1ms to prevent oversleeping due to OS scheduler)
            if (time_to_sleep > std::chrono::milliseconds(2)) {
                std::this_thread::sleep_for(time_to_sleep - std::chrono::milliseconds(1));
            }

            // Spin-lock busy wait for the final fraction of a millisecond
            while (Now() - frame_begin < target_frame_time) {
                std::this_thread::yield();
            }
        }
    }
};
