#pragma once

#include "Renderer/VkVault.hpp"

struct SubmissionPile;

void Reset(SubmissionPile& pile);

SubmissionPile& Begin(SubmissionPile& pile);

SubmissionPile& Command(SubmissionPile& pile, VkCommandBuffer command);

SubmissionPile& Wait(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage);

SubmissionPile& Signal(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage);

void End(SubmissionPile& pile);

void SubmitPile(QueueContext& ctx, SubmissionPile& pile, VkFence execution_fence);

void SubmitMultiplePiles(QueueContext& ctx, SubmissionPile* piles, u64 pile_count, VkFence execution_fence);
