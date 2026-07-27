#pragma once

#include "Renderer/VkVault.hpp"
#include "Core/ResourcePool.hpp"

namespace ImageView {
    struct Value {
        VkImageView ImageView;
    };

    using Id = Handle<Value>;
}
