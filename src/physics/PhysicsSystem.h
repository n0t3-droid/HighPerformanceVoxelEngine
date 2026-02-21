#pragma once

#include <glm/glm.hpp>

namespace Game { namespace World { class Chunk; } }

namespace Physics {

	struct PlayerControllerConfig {
		float radius = 0.30f;      // ~0.6m wide
		float height = 1.80f;      // ~1.8m tall
		float eyeHeight = 1.62f;   // camera offset from feet

		float moveSpeed = 6.5f;
		float sprintMultiplier = 1.6f;
		float jumpVelocity = 7.5f;
		float gravity = 20.0f;
	};

	struct PlayerState {
		glm::vec3 feetPosition{16.0f, 20.0f, 16.0f};
		glm::vec3 velocity{0.0f};
		bool onGround = false;
	};

	struct PlayerInput {
		// Desired movement in world space (x/z only; y ignored)
		glm::vec3 moveWorld{0.0f};
		bool jumpPressed = false;
		bool sprint = false;
	};

	void StepPlayerInChunk(
		PlayerState& state,
		const PlayerControllerConfig& config,
		const PlayerInput& input,
		const Game::World::Chunk& chunk,
		float dt
	);

}

