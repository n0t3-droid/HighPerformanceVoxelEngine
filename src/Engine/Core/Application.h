#include "Engine/ECS/Registry.h"
#include <memory>

namespace Engine {
    class Application {
    public:
        Application();
        ~Application();

        void Run();

    private:
        std::unique_ptr<Registry> m_Registry;
    };
}
