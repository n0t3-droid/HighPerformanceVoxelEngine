#include "Engine/Core/Application.h"
#include "Engine/Core/RunLog.h"
#include "Engine/Core/Telemetry.h"
#include <iostream>
#include <filesystem>
#include <exception>

static void TrySetWorkingDirectoryToProjectRoot(const char* argv0) {
    namespace fs = std::filesystem;

    fs::path startDir;
    try {
        if (argv0 && *argv0) {
            startDir = fs::absolute(fs::path(argv0)).parent_path();
        } else {
            startDir = fs::current_path();
        }
    } catch (...) {
        return;
    }

    auto hasShaderAssets = [](const fs::path& dir) {
        std::error_code ec;
        const bool hasShaders =
            fs::exists(dir / "assets" / "shaders" / "chunk_vert.glsl", ec) &&
            fs::exists(dir / "assets" / "shaders" / "chunk_frag.glsl", ec);
        return hasShaders;
    };

    fs::path probe = startDir;
    for (int i = 0; i < 6; ++i) {
        if (hasShaderAssets(probe)) {
            fs::current_path(probe);
            std::cout << "Working directory set to: " << probe.string() << std::endl;
            return;
        }
        if (!probe.has_parent_path()) break;
        probe = probe.parent_path();
    }
}

int main(int argc, char** argv) {
    TrySetWorkingDirectoryToProjectRoot((argc > 0) ? argv[0] : nullptr);
    Engine::RunLog::Init();
    Engine::RunLog::Info("Working directory: " + std::filesystem::current_path().string());
    Engine::Telemetry::Init();

    try {
        Engine::Application app;
        app.Run();
        Engine::RunLog::Info("Normal shutdown");
        Engine::Telemetry::Shutdown();
        return 0;
    } catch (const std::exception& e) {
        Engine::RunLog::Error(std::string("Fatal exception: ") + e.what());
        Engine::Telemetry::Shutdown();
        return -1;
    } catch (...) {
        Engine::RunLog::Error("Fatal unknown exception");
        Engine::Telemetry::Shutdown();
        return -2;
    }
}
