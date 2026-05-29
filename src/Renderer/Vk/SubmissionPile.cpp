#include "SubmissionPile.hpp"

void SubmitHarvestedPiles(QueueContext& ctx, VkFence execution_fence, SubmissionPile* piles, u64 pile_count) {
    std::array<VkSubmitInfo2, 128> global_submits;
    u64 total_submits = 0;

    for (u64 i = 0; i < pile_count; i++) {
        SubmissionPile& pile = piles[i];

        for (u64 j = 0; j < pile.SubmitCount; j++) {
            global_submits[total_submits++] = pile.Submits[j];
        }

        pile.Reset();
    }

    if (total_submits > 0) {
        vkQueueSubmit2(ctx.Queue, static_cast<u32>(total_submits), global_submits.data(), execution_fence);
    }
}
