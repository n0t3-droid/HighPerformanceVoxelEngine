#pragma once

#include <fmt/core.h>
#include <iostream>

namespace Engine {
    class Log {
    public:
        template<typename... Args>
        static void Info(fmt::format_string<Args...> fmt, Args&&... args) {
             std::cout << "[INFO] " << fmt::format(fmt, std::forward<Args>(args)...) << std::endl;
        }

        template<typename... Args>
        static void Error(fmt::format_string<Args...> fmt, Args&&... args) {
             std::cerr << "[ERROR] " << fmt::format(fmt, std::forward<Args>(args)...) << std::endl;
        }
    };
}
