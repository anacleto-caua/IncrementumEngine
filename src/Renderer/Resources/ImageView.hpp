#pragma once

#include "Renderer/Vk.hpp"
#include "Asl/ResourcePool.hpp"

namespace ImageView {
    struct Value {
        VkImageView ImageView;
    };

    using Id = asl::Handle<Value>;
}
