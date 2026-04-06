#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>

namespace vec3 {
    static constexpr glm::vec3 ONE     = glm::vec3(1,1,1);
    static constexpr glm::vec3 ZERO    = glm::vec3(0,0,0);

    static constexpr glm::vec3 UP      = glm::vec3(0,1,0);
    static constexpr glm::vec3 RIGHT   = glm::vec3(1,0,0);
    static constexpr glm::vec3 FORWARD = glm::vec3(0,0,1);
};
