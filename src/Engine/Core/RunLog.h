#pragma once

#include <string>

namespace Engine {
    class RunLog {
    public:
        static void Init(const std::string& fileName = "hve_last_run.log");
        static void Info(const std::string& msg);
        static void Warn(const std::string& msg);
        static void Error(const std::string& msg);
        static void Flush();

    private:
        static void Write(const char* level, const std::string& msg);
    };
}
