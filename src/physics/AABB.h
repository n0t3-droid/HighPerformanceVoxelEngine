#pragma once

#include <glm/glm.hpp>

namespace Physics {

	struct AABB {
		glm::vec3 min{0.0f};
		glm::vec3 max{0.0f};

		AABB() = default;
		AABB(const glm::vec3& min_, const glm::vec3& max_) : min(min_), max(max_) {}

		glm::vec3 Size() const { return max - min; }
		glm::vec3 Center() const { return (min + max) * 0.5f; }

		AABB Translated(const glm::vec3& delta) const { return AABB(min + delta, max + delta); }
	};

}

