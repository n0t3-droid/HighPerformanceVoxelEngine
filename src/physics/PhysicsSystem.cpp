#include "PhysicsSystem.h"

#include "AABB.h"
#include "Game/World/Chunk.h"

#include <algorithm>
#include <cmath>

namespace Physics {
	namespace {
		constexpr float kEps = 0.001f;

		static AABB PlayerAABB(const PlayerState& state, const PlayerControllerConfig& config) {
			const glm::vec3 min(state.feetPosition.x - config.radius,
								state.feetPosition.y,
								state.feetPosition.z - config.radius);
			const glm::vec3 max(state.feetPosition.x + config.radius,
								state.feetPosition.y + config.height,
								state.feetPosition.z + config.radius);
			return AABB(min, max);
		}

		static bool IsSolid(const Game::World::Chunk& chunk, int x, int y, int z) {
			return chunk.GetBlock(x, y, z) != 0;
		}

		static bool AABBIntersectsSolid(const Game::World::Chunk& chunk, const AABB& box) {
			const int minX = (int)std::floor(box.min.x + kEps);
			const int minY = (int)std::floor(box.min.y + kEps);
			const int minZ = (int)std::floor(box.min.z + kEps);

			const int maxX = (int)std::floor(box.max.x - kEps);
			const int maxY = (int)std::floor(box.max.y - kEps);
			const int maxZ = (int)std::floor(box.max.z - kEps);

			for (int x = minX; x <= maxX; ++x) {
				for (int y = minY; y <= maxY; ++y) {
					for (int z = minZ; z <= maxZ; ++z) {
						if (IsSolid(chunk, x, y, z)) return true;
					}
				}
			}
			return false;
		}

		static void ResolveAxis(
			PlayerState& state,
			const PlayerControllerConfig& config,
			const Game::World::Chunk& chunk,
			float delta,
			int axis,
			bool& outHitGround
		) {
			if (std::abs(delta) < 1e-8f) return;

			glm::vec3 newPos = state.feetPosition;
			newPos[axis] += delta;

			PlayerState test = state;
			test.feetPosition = newPos;
			AABB box = PlayerAABB(test, config);

			if (!AABBIntersectsSolid(chunk, box)) {
				state.feetPosition = newPos;
				return;
			}

			// Collision: clamp to nearest voxel boundary along this axis.
			if (axis == 0) {
				// X axis
				const int xCell = (delta > 0.0f) ? (int)std::floor(box.max.x - kEps) : (int)std::floor(box.min.x + kEps);
				// Determine y/z spans using the *colliding* box.
				const int minY = (int)std::floor(box.min.y + kEps);
				const int maxY = (int)std::floor(box.max.y - kEps);
				const int minZ = (int)std::floor(box.min.z + kEps);
				const int maxZ = (int)std::floor(box.max.z - kEps);

				bool collides = false;
				for (int y = minY; y <= maxY && !collides; ++y) {
					for (int z = minZ; z <= maxZ && !collides; ++z) {
						if (IsSolid(chunk, xCell, y, z)) collides = true;
					}
				}

				if (collides) {
					if (delta > 0.0f) {
						state.feetPosition.x = (float)xCell - config.radius - kEps;
					} else {
						state.feetPosition.x = (float)(xCell + 1) + config.radius + kEps;
					}
					state.velocity.x = 0.0f;
				} else {
					state.feetPosition = newPos;
				}
			} else if (axis == 2) {
				// Z axis
				const int zCell = (delta > 0.0f) ? (int)std::floor(box.max.z - kEps) : (int)std::floor(box.min.z + kEps);
				const int minY = (int)std::floor(box.min.y + kEps);
				const int maxY = (int)std::floor(box.max.y - kEps);
				const int minX = (int)std::floor(box.min.x + kEps);
				const int maxX = (int)std::floor(box.max.x - kEps);

				bool collides = false;
				for (int y = minY; y <= maxY && !collides; ++y) {
					for (int x = minX; x <= maxX && !collides; ++x) {
						if (IsSolid(chunk, x, y, zCell)) collides = true;
					}
				}

				if (collides) {
					if (delta > 0.0f) {
						state.feetPosition.z = (float)zCell - config.radius - kEps;
					} else {
						state.feetPosition.z = (float)(zCell + 1) + config.radius + kEps;
					}
					state.velocity.z = 0.0f;
				} else {
					state.feetPosition = newPos;
				}
			} else {
				// Y axis
				const int yCell = (delta > 0.0f) ? (int)std::floor(box.max.y - kEps) : (int)std::floor(box.min.y + kEps);
				const int minX = (int)std::floor(box.min.x + kEps);
				const int maxX = (int)std::floor(box.max.x - kEps);
				const int minZ = (int)std::floor(box.min.z + kEps);
				const int maxZ = (int)std::floor(box.max.z - kEps);

				bool collides = false;
				for (int x = minX; x <= maxX && !collides; ++x) {
					for (int z = minZ; z <= maxZ && !collides; ++z) {
						if (IsSolid(chunk, x, yCell, z)) collides = true;
					}
				}

				if (collides) {
					if (delta > 0.0f) {
						// Hit ceiling
						state.feetPosition.y = (float)yCell - config.height - kEps;
					} else {
						// Landed on ground
						state.feetPosition.y = (float)(yCell + 1) + kEps;
						outHitGround = true;
					}
					state.velocity.y = 0.0f;
				} else {
					state.feetPosition = newPos;
				}
			}
		}
	}

	void StepPlayerInChunk(
		PlayerState& state,
		const PlayerControllerConfig& config,
		const PlayerInput& input,
		const Game::World::Chunk& chunk,
		float dt
	) {
		dt = std::clamp(dt, 0.0f, 0.05f);

		// Horizontal movement
		glm::vec3 wish = input.moveWorld;
		wish.y = 0.0f;
		const float wishLen = glm::length(wish);
		if (wishLen > 0.001f) {
			wish /= wishLen;
		} else {
			wish = glm::vec3(0.0f);
		}

		float speed = config.moveSpeed * (input.sprint ? config.sprintMultiplier : 1.0f);
		glm::vec3 targetVel = wish * speed;

		if (state.onGround) {
			state.velocity.x = targetVel.x;
			state.velocity.z = targetVel.z;
		} else {
			// Light air control
			state.velocity.x = state.velocity.x * 0.90f + targetVel.x * 0.10f;
			state.velocity.z = state.velocity.z * 0.90f + targetVel.z * 0.10f;
		}

		// Jump
		if (input.jumpPressed && state.onGround) {
			state.velocity.y = config.jumpVelocity;
			state.onGround = false;
		}

		// Gravity
		state.velocity.y -= config.gravity * dt;

		// Integrate + collide, axis-separated
		bool hitGround = false;

		ResolveAxis(state, config, chunk, state.velocity.x * dt, 0, hitGround);
		ResolveAxis(state, config, chunk, state.velocity.z * dt, 2, hitGround);
		ResolveAxis(state, config, chunk, state.velocity.y * dt, 1, hitGround);

		state.onGround = hitGround;

		// Simple world bounds safety for the single-chunk sandbox
		state.feetPosition.x = std::clamp(state.feetPosition.x, 0.0f + config.radius + kEps, 32.0f - config.radius - kEps);
		state.feetPosition.z = std::clamp(state.feetPosition.z, 0.0f + config.radius + kEps, 32.0f - config.radius - kEps);
		state.feetPosition.y = std::max(state.feetPosition.y, 0.0f + kEps);
	}
}

