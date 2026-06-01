#pragma once

#include "Renderer/VkVault.hpp"

struct SubmissionPile;

void Reset(SubmissionPile& pile);

SubmissionPile& Begin(SubmissionPile& pile);

SubmissionPile& Command(SubmissionPile& pile, VkCommandBuffer command);

SubmissionPile& Wait(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);

SubmissionPile& Signal(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);

void End(SubmissionPile& pile);

void SubmitHarvestedPiles(QueueContext& ctx, VkFence execution_fence, SubmissionPile* piles, u64 pile_count);
