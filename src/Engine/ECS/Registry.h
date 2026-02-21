#pragma once

#include <entt/entt.hpp>

namespace Engine {
    class Registry {
    public:
        Registry() = default;
        ~Registry() = default;

        entt::registry& Get() { return m_Registry; }
        
        entt::entity CreateEntity() {
            return m_Registry.create();
        }

        void DestroyEntity(entt::entity entity) {
            m_Registry.destroy(entity);
        }

    private:
        entt::registry m_Registry;
    };
}
