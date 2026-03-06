#include "Engine/Core/RunLog.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace Engine {

    static std::mutex s_Mutex;
    static std::ofstream s_Out;
    static int s_WriteCounter = 0;

    static std::string NowStamp() {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const std::time_t t = clock::to_time_t(now);

        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    void RunLog::Init(const std::string& fileName) {
        std::scoped_lock lock(s_Mutex);
        if (s_Out.is_open()) return;

        s_Out.open(fileName, std::ios::out | std::ios::trunc);
        if (!s_Out) return;

        s_Out << "=== HVE Run Log ===\n";
        s_Out << "Started: " << NowStamp() << "\n";
        s_Out.flush();
    }

    void RunLog::Write(const char* level, const std::string& msg) {
        std::scoped_lock lock(s_Mutex);
        if (!s_Out.is_open()) return;
        s_Out << "[" << NowStamp() << "]" << "[" << level << "] " << msg << "\n";
        ++s_WriteCounter;
        if (s_WriteCounter >= 32 || level[0] != 'I') {
            s_Out.flush();
            s_WriteCounter = 0;
        }
    }

    void RunLog::Info(const std::string& msg) { Write("INFO", msg); }
    void RunLog::Warn(const std::string& msg) { Write("WARN", msg); }
    void RunLog::Error(const std::string& msg) { Write("ERR ", msg); }

    void RunLog::Flush() {
        std::scoped_lock lock(s_Mutex);
        if (s_Out.is_open()) s_Out.flush();
    }

}
