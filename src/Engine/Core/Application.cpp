#include "Application.h"
#include "Window.h"
#include "Input.h"
#include "Engine/Core/HardwareDetector.h"
#include "Engine/Core/GameTime.h"
#include "Engine/Core/RunLog.h"
#include "Engine/Core/TimeSlicedJobSystem.h"
#include "Engine/Core/Telemetry.h"
#include "Engine/Core/QualityManager.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Mesh.h"
#include "Engine/Renderer/Camera.h"
#include "Engine/Renderer/BindlessTextures.h"
#include "Game/World/Generation.h"
#include "Game/World/TerrainPreset.h"
#include "Game/World/ChunkManager.h"
#include "Engine/Renderer/WorldRenderer.h"
#include "threading/ThreadPool.h"
#include <iostream>
#include <memory>
#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include <fstream>
#include <limits>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <unordered_map>
#include <set>
#include <array>
#include <atomic>
#include <chrono>

#ifdef _MSC_VER
#pragma warning(disable:4005)
#endif

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <glad/glad.h>

// Minimal text rendering for menus (no external font assets needed).
// Provided by stb (already fetched via CMake FetchContent).
#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>
#include <stb_image.h>

// Some build environments/headers don't expose these enums even when KHR_debug is usable.
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif
#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif

// Define GLFW constants if not present (should be in Input.h/cpp but we need them here for direction)
#define GLFW_KEY_W 87
#define GLFW_KEY_S 83
#define GLFW_KEY_A 65
#define GLFW_KEY_D 68
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_LEFT_SHIFT 340
#define GLFW_KEY_G 71
#define GLFW_KEY_Z 90
#define GLFW_KEY_Y 89
#define GLFW_KEY_M 77
#define GLFW_KEY_N 78
#define GLFW_KEY_P 80
#define GLFW_KEY_V 86
#define GLFW_KEY_X 88

#define GLFW_KEY_E 69

#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_TAB 258
#define GLFW_KEY_ENTER 257

#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_UP 265

#define GLFW_KEY_0 48
#define GLFW_KEY_1 49
#define GLFW_KEY_2 50
#define GLFW_KEY_3 51
#define GLFW_KEY_4 52
#define GLFW_KEY_5 53
#define GLFW_KEY_6 54
#define GLFW_KEY_7 55
#define GLFW_KEY_8 56
#define GLFW_KEY_9 57
#define GLFW_KEY_MINUS 45
#define GLFW_KEY_EQUAL 61

#define GLFW_KEY_F1 290
#define GLFW_KEY_F2 291
#define GLFW_KEY_F3 292
#define GLFW_KEY_F4 293
#define GLFW_KEY_F5 294
#define GLFW_KEY_F6 295
#define GLFW_KEY_F7 296
#define GLFW_KEY_F9 298
#define GLFW_KEY_F10 299
#define GLFW_KEY_F11 300

namespace Engine {

    static bool GetEnvBool(const char* name, bool defaultValue);
    static int GetEnvIntClamped(const char* name, int defaultValue, int minValue, int maxValue);
    static float GetEnvFloatClamped(const char* name, float defaultValue, float minValue, float maxValue);

    static inline std::string TrimCopy(const std::string& s) {
        std::size_t a = 0;
        while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
        std::size_t b = s.size();
        while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
    }

    static std::unordered_map<std::string, std::string> LoadIniKeyValue(const char* path) {
        std::unordered_map<std::string, std::string> out;
        if (!path || !*path) return out;

        std::ifstream f(path);
        if (!f.is_open()) return out;

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            const std::string t = TrimCopy(line);
            if (t.empty()) continue;
            if (t[0] == '#' || t[0] == ';') continue;

            const std::size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = TrimCopy(t.substr(0, eq));
            const std::string v = TrimCopy(t.substr(eq + 1));
            if (!k.empty()) out[k] = v;
        }
        return out;
    }

    static void SaveIniKeyValue(const char* path, const std::unordered_map<std::string, std::string>& kv) {
        if (!path || !*path) return;
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) return;

        f << "# HighPerformanceVoxelEngine settings\n";
        f << "# Env vars still override these values.\n\n";

        auto write = [&](const char* key) {
            auto it = kv.find(key);
            if (it != kv.end()) {
                f << key << "=" << it->second << "\n";
            }
        };

        write("quality");
        write("world_file");
        write("mouse_sens");
        write("invert_y");
        write("fov");
        write("wireframe");
        write("vsync");
        write("day_night_cycle");

        write("key_forward");
        write("key_back");
        write("key_left");
        write("key_right");
        write("key_up");
        write("key_down");
        write("key_inventory");
        write("key_brake");
    }

    static std::string KeyName(int key) {
        if (key >= 48 && key <= 57) return std::string(1, (char)key); // 0-9
        if (key >= 65 && key <= 90) return std::string(1, (char)key); // A-Z
        switch (key) {
            case GLFW_KEY_SPACE: return "SPACE";
            case GLFW_KEY_LEFT_SHIFT: return "LSHIFT";
            case GLFW_KEY_ESCAPE: return "ESC";
            case GLFW_KEY_TAB: return "TAB";
            default: break;
        }
        return std::string("KEY ") + std::to_string(key);
    }

    static int GetEnvOrIniIntClamped(const char* envName, const std::unordered_map<std::string, std::string>& ini, const char* iniKey, int defaultValue, int minValue, int maxValue) {
        if (const char* v = std::getenv(envName); v && *v) {
            return GetEnvIntClamped(envName, defaultValue, minValue, maxValue);
        }
        auto it = ini.find(iniKey);
        if (it == ini.end()) return defaultValue;
        try {
            int parsed = std::stoi(it->second);
            if (parsed < minValue) parsed = minValue;
            if (parsed > maxValue) parsed = maxValue;
            return parsed;
        } catch (...) {
            return defaultValue;
        }
    }

    static std::string GetEnvOrIniString(const char* envName, const std::unordered_map<std::string, std::string>& ini, const char* iniKey, const char* defaultValue) {
        if (const char* v = std::getenv(envName); v && *v) {
            return std::string(v);
        }
        auto it = ini.find(iniKey);
        if (it == ini.end()) return std::string(defaultValue ? defaultValue : "");
        return it->second;
    }

    static bool GetEnvOrIniBool(const char* envName, const std::unordered_map<std::string, std::string>& ini, const char* iniKey, bool defaultValue) {
        if (const char* v = std::getenv(envName); v && *v) {
            return GetEnvBool(envName, defaultValue);
        }
        auto it = ini.find(iniKey);
        if (it == ini.end()) return defaultValue;
        const std::string s = it->second;
        if (s.empty()) return defaultValue;
        const char c = s[0];
        if (c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T') return true;
        if (c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F') return false;
        return defaultValue;
    }

    static float GetEnvOrIniFloatClamped(const char* envName, const std::unordered_map<std::string, std::string>& ini, const char* iniKey, float defaultValue, float minValue, float maxValue) {
        if (const char* v = std::getenv(envName); v && *v) {
            return GetEnvFloatClamped(envName, defaultValue, minValue, maxValue);
        }
        auto it = ini.find(iniKey);
        if (it == ini.end()) return defaultValue;
        try {
            float parsed = std::stof(it->second);
            if (parsed < minValue) parsed = minValue;
            if (parsed > maxValue) parsed = maxValue;
            return parsed;
        } catch (...) {
            return defaultValue;
        }
    }

    static bool GetEnvBool(const char* name, bool defaultValue) {
        const char* v = std::getenv(name);
        if (!v || !*v) return defaultValue;
        if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
        if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return false;
        return defaultValue;
    }

    static int GetEnvIntClamped(const char* name, int defaultValue, int minValue, int maxValue) {
        const char* v = std::getenv(name);
        if (!v || !*v) return defaultValue;
        try {
            int parsed = std::stoi(v);
            if (parsed < minValue) parsed = minValue;
            if (parsed > maxValue) parsed = maxValue;
            return parsed;
        } catch (...) {
            return defaultValue;
        }
    }

    static float GetEnvFloatClamped(const char* name, float defaultValue, float minValue, float maxValue) {
        const char* v = std::getenv(name);
        if (!v || !*v) return defaultValue;
        try {
            float parsed = std::stof(v);
            if (parsed < minValue) parsed = minValue;
            if (parsed > maxValue) parsed = maxValue;
            return parsed;
        } catch (...) {
            return defaultValue;
        }
    }

    constexpr int kAtlasTilesX = 16;
    constexpr int kAtlasTilesY = 16;
    constexpr int kAtlasTileCount = kAtlasTilesX * kAtlasTilesY;
    constexpr uint8_t kMaxBlockId = 255;
    constexpr int kBuiltinBlockMaxId = 83;
    constexpr int kGlassBlockStart = 19;
    constexpr int kGlassBlockEnd = 34;
    constexpr int kPaneBlockStart = 36;
    constexpr int kPaneBlockEnd = 51;
    constexpr int kArchitectureStart = 52;
    constexpr int kArchitectureEnd = 83;
    constexpr uint8_t kItemStackMaxCount = 64;
    constexpr uint8_t BLOCK_DIRT = 1;
    constexpr uint8_t BLOCK_GRASS = 2;
    constexpr uint8_t BLOCK_STONE = 3;
    constexpr uint8_t BLOCK_WATER = 5;
    static int g_MinecraftImportPage = 0;

    struct ItemStack {
        uint16_t id = 0;
        uint8_t count = 0;
    };

    static bool IsStackEmpty(const ItemStack& stack) {
        return stack.id == 0 || stack.count == 0;
    }

    static void NormalizeStack(ItemStack& stack) {
        if (stack.id == 0 || stack.count == 0) {
            stack.id = 0;
            stack.count = 0;
            return;
        }
        if (stack.count > kItemStackMaxCount) {
            stack.count = kItemStackMaxCount;
        }
    }

    static bool CanStacksMerge(const ItemStack& a, const ItemStack& b) {
        return !IsStackEmpty(a) && !IsStackEmpty(b) && a.id == b.id;
    }

    static bool IsGlassBlockId(int id) {
        return id == 11 || (id >= kGlassBlockStart && id <= kGlassBlockEnd);
    }

    static bool IsPaneBlockId(int id) {
        return id == 35 || (id >= kPaneBlockStart && id <= kPaneBlockEnd);
    }

    static bool IsArchitectureBlockId(int id) {
        return id >= kArchitectureStart && id <= kArchitectureEnd;
    }

    static std::string HumanizeBlockToken(const std::string& token) {
        std::string out;
        out.reserve(token.size() + 8);
        bool newWord = true;
        for (char c : token) {
            if (c == '_' || c == '-' || c == '/') {
                out.push_back(' ');
                newWord = true;
                continue;
            }
            if (newWord && c >= 'a' && c <= 'z') out.push_back((char)(c - 32));
            else out.push_back(c);
            newWord = (c == ' ');
        }
        return out;
    }

    static std::string ResolveMinecraftTexturePath(const std::string& texName) {
        if (texName.empty()) return {};
        static const std::array<std::string, 8> bases = {
            "minecraft/textures/block/",
            "HighPerformanceVoxelEngine/minecraft/textures/block/",
            "./minecraft/textures/block/",
            "../minecraft/textures/block/",
            "../../minecraft/textures/block/",
            "../../../minecraft/textures/block/",
            "../../../../minecraft/textures/block/",
            "../../../../../minecraft/textures/block/"
        };

        for (const auto& base : bases) {
            std::filesystem::path p = std::filesystem::path(base) / (texName + ".png");
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) return p.generic_string();
        }
        return {};
    }

    static std::string ResolveHighEndTexturePath(const std::string& fileName) {
        if (fileName.empty()) return {};
        static const std::array<std::string, 8> bases = {
            "assets/textures/highend/",
            "HighPerformanceVoxelEngine/assets/textures/highend/",
            "./assets/textures/highend/",
            "../assets/textures/highend/",
            "../../assets/textures/highend/",
            "../../../assets/textures/highend/",
            "../../../../assets/textures/highend/",
            "../../../../../assets/textures/highend/"
        };

        for (const auto& base : bases) {
            std::filesystem::path p = std::filesystem::path(base) / fileName;
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) return p.generic_string();
        }
        return {};
    }

    static const std::vector<std::string>& GetCustomTextureSetFiles() {
        static const std::vector<std::string> files = []() {
            std::vector<std::string> out;
            static const std::array<std::string, 7> dirs = {
                "Texture.set.impl",
                "HighPerformanceVoxelEngine/Texture.set.impl",
                "./Texture.set.impl",
                "../Texture.set.impl",
                "../../Texture.set.impl",
                "../../../Texture.set.impl",
                "../../../../Texture.set.impl"
            };

            for (const auto& d : dirs) {
                std::error_code ec;
                if (!std::filesystem::exists(d, ec)) continue;

                for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
                    if (ec) break;
                    if (!e.is_regular_file()) continue;

                    std::string ext = e.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;

                    out.push_back(e.path().generic_string());
                }

                if (!out.empty()) break;
            }

            std::sort(out.begin(), out.end());
            return out;
        }();

        return files;
    }

    static std::string GetCustomTexturePathForBlockId(int blockId) {
        switch (blockId) {
            case BLOCK_GRASS: {
                const std::string p = ResolveHighEndTexturePath("grass.png");
                if (!p.empty()) return p;
            } break;
            case BLOCK_DIRT: {
                const std::string p = ResolveHighEndTexturePath("dirt.png");
                if (!p.empty()) return p;
            } break;
            case BLOCK_STONE: {
                const std::string p = ResolveHighEndTexturePath("stone.png");
                if (!p.empty()) return p;
            } break;
            case 4: {
                const std::string p = ResolveHighEndTexturePath("sand.png");
                if (!p.empty()) return p;
            } break;
            case BLOCK_WATER: {
                const std::string p = ResolveHighEndTexturePath("water.png");
                if (!p.empty()) return p;
            } break;
            case 6: {
                const std::string p = ResolveHighEndTexturePath("snow.png");
                if (!p.empty()) return p;
            } break;
            case 7: {
                const std::string p = ResolveHighEndTexturePath("wood.png");
                if (!p.empty()) return p;
            } break;
            case 8: {
                const std::string p = ResolveHighEndTexturePath("leaves.png");
                if (!p.empty()) return p;
            } break;
            case 9: {
                const std::string p = ResolveHighEndTexturePath("concrete.png");
                if (!p.empty()) return p;
            } break;
            case 10: {
                const std::string p = ResolveHighEndTexturePath("brick.png");
                if (!p.empty()) return p;
            } break;
            case 12: {
                const std::string p = ResolveHighEndTexturePath("metal.png");
                if (!p.empty()) return p;
            } break;
            case 13: {
                const std::string p = ResolveHighEndTexturePath("marble.png");
                if (!p.empty()) return p;
            } break;
            case 14: {
                const std::string p = ResolveHighEndTexturePath("basalt.png");
                if (!p.empty()) return p;
            } break;
            case 15: {
                const std::string p = ResolveHighEndTexturePath("limestone.png");
                if (!p.empty()) return p;
            } break;
            case 16: {
                const std::string p = ResolveHighEndTexturePath("slate.png");
                if (!p.empty()) return p;
            } break;
            case 17: {
                const std::string p = ResolveHighEndTexturePath("terracotta.png");
                if (!p.empty()) return p;
            } break;
            case 18: {
                const std::string p = ResolveHighEndTexturePath("asphalt.png");
                if (!p.empty()) return p;
            } break;
            case 52:
            case 56:
            case 60:
            case 64:
            case 71:
            case 73:
            case 77:
            case 78:
            case 79: {
                const std::string p = ResolveHighEndTexturePath("stone.png");
                if (!p.empty()) return p;
            } break;
            case 53:
            case 54: {
                const std::string p = ResolveHighEndTexturePath("concrete.png");
                if (!p.empty()) return p;
            } break;
            case 55:
            case 76:
            case 83: {
                const std::string p = ResolveHighEndTexturePath("brick.png");
                if (!p.empty()) return p;
            } break;
            case 57:
            case 59:
            case 62:
            case 63:
            case 80:
            case 82: {
                const std::string p = ResolveHighEndTexturePath("metal.png");
                if (!p.empty()) return p;
            } break;
            case 58:
            case 65:
            case 66:
            case 68:
            case 70: {
                const std::string p = ResolveHighEndTexturePath("basalt.png");
                if (!p.empty()) return p;
            } break;
            case 61: {
                const std::string p = ResolveHighEndTexturePath("slate.png");
                if (!p.empty()) return p;
            } break;
            case 67: {
                const std::string p = ResolveHighEndTexturePath("limestone.png");
                if (!p.empty()) return p;
            } break;
            case 69:
            case 72:
            case 75: {
                const std::string p = ResolveHighEndTexturePath("terracotta.png");
                if (!p.empty()) return p;
            } break;
            case 74: {
                const std::string p = ResolveHighEndTexturePath("dirt.png");
                if (!p.empty()) return p;
            } break;
            case 81: {
                const std::string p = ResolveHighEndTexturePath("asphalt.png");
                if (!p.empty()) return p;
            } break;
            default: break;
        }
        return {};
    }

    static const std::vector<std::string>& GetMinecraftBlockTexturePool() {
        static const std::vector<std::string> pool = []() {
            std::vector<std::string> names;
            std::set<std::string> uniq;

            static const std::array<std::string, 8> dirs = {
                "minecraft/textures/block",
                "HighPerformanceVoxelEngine/minecraft/textures/block",
                "./minecraft/textures/block",
                "../minecraft/textures/block",
                "../../minecraft/textures/block",
                "../../../minecraft/textures/block",
                "../../../../minecraft/textures/block",
                "../../../../../minecraft/textures/block"
            };

            for (const auto& d : dirs) {
                std::error_code ec;
                if (!std::filesystem::exists(d, ec)) continue;
                for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
                    if (ec) break;
                    if (!e.is_regular_file()) continue;
                    if (e.path().extension() != ".png") continue;
                    const std::string stem = e.path().stem().string();
                    if (stem.empty()) continue;
                    if (uniq.insert(stem).second) names.push_back(stem);
                }
                if (!names.empty()) break;
            }

            std::sort(names.begin(), names.end());
            return names;
        }();

        return pool;
    }

    static int GetMinecraftImportPageSize() {
        return std::max(0, (int)kMaxBlockId - kBuiltinBlockMaxId);
    }

    static int GetMinecraftImportPageMax() {
        const auto& pool = GetMinecraftBlockTexturePool();
        const int pageSize = GetMinecraftImportPageSize();
        if (pageSize <= 0 || pool.empty()) return 0;
        return std::max(0, ((int)pool.size() - 1) / pageSize);
    }

    static int ClampMinecraftImportPage(int page) {
        return std::clamp(page, 0, GetMinecraftImportPageMax());
    }

    static std::string GetTextureNameForBlockId(int blockId) {
        static const char* kGlassColors[16] = {
            "white", "light_gray", "gray", "black", "red", "orange", "yellow", "lime",
            "green", "cyan", "light_blue", "blue", "purple", "magenta", "pink", "brown"
        };

        if (blockId >= 19 && blockId <= 34) {
            return std::string(kGlassColors[blockId - 19]) + "_stained_glass";
        }
        if (blockId >= 36 && blockId <= 51) {
            return std::string(kGlassColors[blockId - 36]) + "_stained_glass_pane_top";
        }

        switch (blockId) {
            case 1: return "dirt";
            case 2: return "grass_block_top";
            case 3: return "stone";
            case 4: return "sand";
            case 5: return "water_still";
            case 6: return "snow";
            case 7: return "oak_planks";
            case 8: return "oak_leaves";
            case 9: return "gray_concrete";
            case 10: return "bricks";
            case 11: return "glass";
            case 12: return "iron_block";
            case 13: return "quartz_block_top";
            case 14: return "basalt_top";
            case 15: return "sandstone_top";
            case 16: return "deepslate";
            case 17: return "terracotta";
            case 18: return "black_concrete";
            case 35: return "glass_pane_top";
            case 52: return "smooth_stone";
            case 53: return "gray_concrete";
            case 54: return "white_concrete";
            case 55: return "stone_bricks";
            case 56: return "smooth_stone_slab_top";
            case 57: return "iron_block";
            case 58: return "polished_andesite";
            case 59: return "iron_bars";
            case 60: return "smooth_quartz";
            case 61: return "deepslate_tiles";
            case 62: return "copper_block";
            case 63: return "oxidized_copper";
            case 64: return "stone";
            case 65: return "polished_blackstone";
            case 66: return "polished_granite";
            case 67: return "smooth_sandstone";
            case 68: return "deepslate_tiles";
            case 69: return "brown_terracotta";
            case 70: return "blackstone";
            case 71: return "quartz_block_top";
            case 72: return "gray_glazed_terracotta";
            case 73: return "calcite";
            case 74: return "packed_mud";
            case 75: return "white_terracotta";
            case 76: return "tuff_bricks";
            case 77: return "stone_bricks";
            case 78: return "chiseled_stone_bricks";
            case 79: return "polished_andesite";
            case 80: return "blast_furnace_front";
            case 81: return "sculk";
            case 82: return "sea_lantern";
            case 83: return "deepslate_bricks";
            default: break;
        }

        if (blockId > kBuiltinBlockMaxId) {
            const auto& pool = GetMinecraftBlockTexturePool();
            const int localIdx = blockId - (kBuiltinBlockMaxId + 1);
            const int pageSize = GetMinecraftImportPageSize();
            const int globalIdx = g_MinecraftImportPage * pageSize + localIdx;
            if (globalIdx >= 0 && globalIdx < (int)pool.size()) return pool[(size_t)globalIdx];
        }

        return {};
    }

    static int GetRuntimeMaxBlockId() {
        const auto& pool = GetMinecraftBlockTexturePool();
        const int pageSize = GetMinecraftImportPageSize();
        const int clampedPage = ClampMinecraftImportPage(g_MinecraftImportPage);
        const int importedStart = clampedPage * pageSize;
        const int remaining = std::max(0, (int)pool.size() - importedStart);
        const int importedCount = std::min(remaining, pageSize);
        return std::clamp(kBuiltinBlockMaxId + importedCount, 1, (int)kMaxBlockId);
    }

    static const char* GetBlockDisplayName(int id) {
        static std::unordered_map<std::uint64_t, std::string> dynamicNameCache;
        switch (id) {
            case 1: return "Dirt";
            case 2: return "Grass Block";
            case 3: return "Stone";
            case 4: return "Sand";
            case 5: return "Water";
            case 6: return "Snow";
            case 7: return "Wood Planks";
            case 8: return "Leaves";
            case 9: return "Concrete";
            case 10: return "Brick";
            case 11: return "Clear Glass";
            case 12: return "Metal Block";
            case 13: return "Marble";
            case 14: return "Basalt";
            case 15: return "Limestone";
            case 16: return "Slate";
            case 17: return "Terracotta";
            case 18: return "Asphalt";
            case 19: return "White Glass";
            case 20: return "Light Gray Glass";
            case 21: return "Gray Glass";
            case 22: return "Black Glass";
            case 23: return "Red Glass";
            case 24: return "Orange Glass";
            case 25: return "Yellow Glass";
            case 26: return "Lime Glass";
            case 27: return "Green Glass";
            case 28: return "Cyan Glass";
            case 29: return "Light Blue Glass";
            case 30: return "Blue Glass";
            case 31: return "Purple Glass";
            case 32: return "Magenta Glass";
            case 33: return "Pink Glass";
            case 34: return "Brown Glass";
            case 35: return "Clear Glass Pane";
            case 36: return "White Glass Pane";
            case 37: return "Light Gray Glass Pane";
            case 38: return "Gray Glass Pane";
            case 39: return "Black Glass Pane";
            case 40: return "Red Glass Pane";
            case 41: return "Orange Glass Pane";
            case 42: return "Yellow Glass Pane";
            case 43: return "Lime Glass Pane";
            case 44: return "Green Glass Pane";
            case 45: return "Cyan Glass Pane";
            case 46: return "Light Blue Glass Pane";
            case 47: return "Blue Glass Pane";
            case 48: return "Purple Glass Pane";
            case 49: return "Magenta Glass Pane";
            case 50: return "Pink Glass Pane";
            case 51: return "Brown Glass Pane";
            case 52: return "Smooth Concrete";
            case 53: return "Dark Concrete";
            case 54: return "White Concrete";
            case 55: return "Concrete Tile Grid";
            case 56: return "Concrete Panel Joints";
            case 57: return "Brushed Steel";
            case 58: return "Steel Plate";
            case 59: return "Steel Grate";
            case 60: return "Aluminum Panel";
            case 61: return "Carbon Steel";
            case 62: return "Copper Cladding";
            case 63: return "Oxidized Copper";
            case 64: return "Polished Stone";
            case 65: return "Polished Basalt";
            case 66: return "Polished Granite";
            case 67: return "Polished Limestone";
            case 68: return "Slate Tile Grid";
            case 69: return "Roof Shingles";
            case 70: return "Dark Roof Shingles";
            case 71: return "Ceramic Tile";
            case 72: return "Ceramic Tile Dark";
            case 73: return "Fine Plaster";
            case 74: return "Stucco";
            case 75: return "White Brick";
            case 76: return "Dark Brick";
            case 77: return "Trim Stone";
            case 78: return "Column Stone";
            case 79: return "Window Frame";
            case 80: return "Vent Panel";
            case 81: return "Circuit Panel";
            case 82: return "Light Panel";
            case 83: return "Structural Beam";
            default: {
                if (id > kBuiltinBlockMaxId) {
                    const std::uint64_t key = (std::uint64_t((unsigned)g_MinecraftImportPage) << 32) | (std::uint64_t)(unsigned)id;
                    auto it = dynamicNameCache.find(key);
                    if (it != dynamicNameCache.end()) return it->second.c_str();
                    const std::string texName = GetTextureNameForBlockId(id);
                    if (!texName.empty()) {
                        auto [ins, _ok] = dynamicNameCache.emplace(key, HumanizeBlockToken(texName));
                        return ins->second.c_str();
                    }
                }
                return "Unknown Block";
            }
        }
    }

    static void GetGlassColorByIndex(int index, float& r, float& g, float& b) {
        static const float colors[16][3] = {
            {0.92f, 0.92f, 0.92f}, // White
            {0.71f, 0.71f, 0.71f}, // Light Gray
            {0.41f, 0.41f, 0.41f}, // Gray
            {0.14f, 0.14f, 0.14f}, // Black
            {0.71f, 0.16f, 0.16f}, // Red
            {0.82f, 0.39f, 0.16f}, // Orange
            {0.88f, 0.78f, 0.18f}, // Yellow
            {0.47f, 0.78f, 0.18f}, // Lime
            {0.24f, 0.55f, 0.24f}, // Green
            {0.18f, 0.63f, 0.63f}, // Cyan
            {0.35f, 0.59f, 0.86f}, // Light Blue
            {0.20f, 0.27f, 0.75f}, // Blue
            {0.47f, 0.27f, 0.75f}, // Purple
            {0.75f, 0.31f, 0.67f}, // Magenta
            {0.86f, 0.47f, 0.63f}, // Pink
            {0.47f, 0.31f, 0.20f}  // Brown
        };
        index = std::clamp(index, 0, 15);
        r = colors[index][0];
        g = colors[index][1];
        b = colors[index][2];
    }

    static void GetGlassColorForBlock(int id, float& r, float& g, float& b) {
        if (id == 11 || id == 35) {
            r = 0.51f; g = 0.71f; b = 0.86f;
            return;
        }
        if (id >= 19 && id <= 34) {
            GetGlassColorByIndex(id - 19, r, g, b);
            return;
        }
        if (id >= 36 && id <= 51) {
            GetGlassColorByIndex(id - 36, r, g, b);
            return;
        }
        r = 0.9f; g = 0.0f; b = 0.9f;
    }

    static void GetArchitectureColorForBlock(int id, float& r, float& g, float& b) {
        switch (id) {
            case 52: r = 0.72f; g = 0.72f; b = 0.74f; break; // Smooth Concrete
            case 53: r = 0.22f; g = 0.23f; b = 0.26f; break; // Dark Concrete
            case 54: r = 0.88f; g = 0.88f; b = 0.90f; break; // White Concrete
            case 55: r = 0.62f; g = 0.63f; b = 0.66f; break; // Concrete Tiles
            case 56: r = 0.50f; g = 0.52f; b = 0.55f; break; // Concrete Panels
            case 57: r = 0.58f; g = 0.60f; b = 0.64f; break; // Brushed Steel
            case 58: r = 0.44f; g = 0.45f; b = 0.49f; break; // Steel Plate
            case 59: r = 0.38f; g = 0.41f; b = 0.45f; break; // Steel Grate
            case 60: r = 0.74f; g = 0.76f; b = 0.80f; break; // Aluminum Panel
            case 61: r = 0.30f; g = 0.31f; b = 0.34f; break; // Carbon Steel
            case 62: r = 0.70f; g = 0.42f; b = 0.24f; break; // Copper Panel
            case 63: r = 0.34f; g = 0.54f; b = 0.46f; break; // Oxide Copper
            case 64: r = 0.58f; g = 0.58f; b = 0.60f; break; // Polished Stone
            case 65: r = 0.28f; g = 0.29f; b = 0.31f; break; // Polished Basalt
            case 66: r = 0.63f; g = 0.51f; b = 0.45f; break; // Polished Granite
            case 67: r = 0.78f; g = 0.72f; b = 0.63f; break; // Polished Limestone
            case 68: r = 0.34f; g = 0.38f; b = 0.45f; break; // Slate Tiles
            case 69: r = 0.55f; g = 0.46f; b = 0.36f; break; // Roof Shingles
            case 70: r = 0.22f; g = 0.22f; b = 0.24f; break; // Asphalt Shingles
            case 71: r = 0.82f; g = 0.82f; b = 0.86f; break; // Ceramic Tile
            case 72: r = 0.33f; g = 0.35f; b = 0.40f; break; // Ceramic Tile Dark
            case 73: r = 0.88f; g = 0.84f; b = 0.78f; break; // Plaster
            case 74: r = 0.76f; g = 0.68f; b = 0.60f; break; // Stucco
            case 75: r = 0.86f; g = 0.86f; b = 0.88f; break; // Brick White
            case 76: r = 0.25f; g = 0.25f; b = 0.28f; break; // Brick Dark
            case 77: r = 0.72f; g = 0.70f; b = 0.68f; break; // Trim Stone
            case 78: r = 0.64f; g = 0.62f; b = 0.60f; break; // Column Stone
            case 79: r = 0.50f; g = 0.52f; b = 0.58f; break; // Window Frame
            case 80: r = 0.34f; g = 0.36f; b = 0.40f; break; // Vent Panel
            case 81: r = 0.20f; g = 0.30f; b = 0.38f; break; // Circuit Panel
            case 82: r = 0.92f; g = 0.90f; b = 0.70f; break; // Light Panel
            case 83: r = 0.48f; g = 0.52f; b = 0.56f; break; // Structural Beam
            default: r = 0.9f; g = 0.0f; b = 0.9f; break;
        }
    }

    static void PrintControls() {
        std::cout << "Controls:\n"
                  << "  WASD: move | SPACE: up | LSHIFT: down\n"
                  << "  Hold CTRL: speed boost\n"
                  << "  Mouse: look | TAB: toggle mouse capture\n"
                  << "  E: inventory (click to select block)\n"
                  << "  G: precision build (RMB anchor/confirm plane)\n"
                  << "  V: build mode (Plane/Line/Box)\n"
                  << "  M: mirror build | N: mirror axis\n"
                  << "  P: plane lock (fixes Y)\n"
                  << "  X: flight brake (stop precisely)\n"
                  << "  Ctrl+Z: undo | Ctrl+Y: redo\n"
                  << "  LMB: break block | RMB: place selected block (hold to repeat)\n"
                  << "  1..9 or Mouse Wheel: select block id (scroll cycles all blocks)\n"
                  << "  PageUp/PageDown: switch imported Minecraft asset page\n"
                  << "  Double-tap SPACE: toggle walk mode\n"
                  << "  ESC: release mouse (when captured) / quit (when released)\n"
                  << "  F1: print this help | F2: toggle stats in title\n"
                  << "  F3 (or HVE_KEY_WIREFRAME): toggle wireframe | F4: toggle vsync\n"
                  << "  F5: toggle crosshair | F6: toggle framebuffer dump\n"
                  << "  F7: cycle quality preset (Low/Med/High)\n"
                  << "  F9: save world | F10: load world\n"
                  << "  AutoSave: HVE_AUTOSAVE=1, HVE_AUTOSAVE_SEC (default 180)\n"
                  << "  Env: HVE_FLY_SPEED (default 25), HVE_FLY_BOOST_MULT (default 4)\n"
                  << "  Fly: HVE_FLY_ACCEL, HVE_FLY_DRAG, HVE_FLY_MAX_MULT\n"
                  << "  Fly: HVE_FLY_PRECISION_MULT, HVE_FLY_PRECISION_DRAG\n"
                  << "  Stream: HVE_VIEWDIST, HVE_STREAM_MARGIN, HVE_HEIGHT_CHUNKS, HVE_UPLOAD_BUDGET\n"
                  << "  Wireframe: HVE_KEY_WIREFRAME, HVE_WIREFRAME_STREAM, HVE_WIREFRAME_UPLOAD, HVE_WIREFRAME_HEIGHT, HVE_WIREFRAME_CULL\n"
                  << "  Cache: HVE_DISABLE_UNLOAD=1, HVE_CACHE_MARGIN (extra unload radius)\n"
                  << "  Preload: HVE_PRELOAD_RADIUS, HVE_PRELOAD_MAX_SEC\n"
                  << "  World: HVE_WORLD_FILE (default hve_world.hvew), HVE_QUALITY (0..2)\n"
                  << "  Settings: writes hve_settings.ini (or HVE_SETTINGS_FILE)\n"
                  << "  Terrain: HVE_FORCE_FLAT_WORLD=1, HVE_FORCE_FLAT_Y=40, HVE_SUPERFLAT_MODE=1\n"
                  << "  Walk: HVE_WALK_SPEED (default 8), HVE_WALK_EYE_HEIGHT (default 1.8)\n"
                  << "  Walk: HVE_WALK_ACCEL, HVE_WALK_DRAG, HVE_WALK_MAX_MULT\n"
                  << "  Build: HVE_REACH, HVE_PLACE_RATE, HVE_BREAK_RATE, HVE_PLACE_MAX, HVE_UNDO_MAX\n"
                  << "  Atlas: HVE_ATLAS_SIZE (32..256), HVE_ATLAS_MIPMAPS=1\n"
                  << "  HUD: HVE_HOTBAR=1 (toggle)\n"
                  << "  Debug: HVE_BUILD_WATCH=1, HVE_BUILD_WATCH_SEC (default 0.75)\n"
                  << "  Live: HVE_LIVE=1 (writes hve_live.log), HVE_LIVE_HZ=5\n"
                  << "  Trace: HVE_TRACE=1 (writes hve_trace.log), Replay: HVE_REPLAY_FILE=hve_trace.log\n"
                  << std::endl;
    }

    static void AppendBuildWatchLog(const std::string& line) {
        std::ofstream out("build_watch.log", std::ios::app);
        if (!out) return;
        out << line << "\n";
    }

    static void AppendSpikeLog(const std::string& line) {
        std::ofstream out("hve_spike_watch.log", std::ios::app);
        if (!out) return;
        out << line << "\n";
    }

    static GLuint CompileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);

        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            GLsizei len = 0;
            glGetShaderInfoLog(s, (GLsizei)sizeof(log), &len, log);
            std::cerr << "[Crosshair] Shader compile failed: " << log << std::endl;
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    static GLuint CreateProgram(const char* vsSrc, const char* fsSrc) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return 0;
        }

        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            GLsizei len = 0;
            glGetProgramInfoLog(p, (GLsizei)sizeof(log), &len, log);
            std::cerr << "[Crosshair] Program link failed: " << log << std::endl;
            glDeleteProgram(p);
            return 0;
        }
        return p;
    }

    static std::string ReadTextFileOrEmpty(const std::string& path) {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return std::string();
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::string EscapeJsonString(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += (c >= 0 && c < 32) ? ' ' : c; break;
            }
        }
        return out;
    }

    static std::string ToLowerCopy(const std::string& s) {
        std::string out = s;
        for (char& c : out) {
            c = (char)std::tolower((unsigned char)c);
        }
        return out;
    }

#ifdef _WIN32
    static std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return std::wstring();
        const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (needed <= 0) return std::wstring();
        std::wstring out;
        out.resize((size_t)needed);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], needed);
        return out;
    }
#endif

    static bool StartDetachedProcess(const std::string& cmd, std::string& outError) {
#ifdef _WIN32
        std::wstring wcmd = Utf8ToWide(cmd);
        if (wcmd.empty()) {
            outError = "CreateProcess: empty command";
            return false;
        }

        STARTUPINFOW si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        std::wstring cmdLine = wcmd;
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
            outError = "CreateProcess failed";
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
#else
        std::thread([cmd]() {
            std::system(cmd.c_str());
        }).detach();
        (void)outError;
        return true;
#endif
    }

    static bool SendTcpPayload(const std::string& host, uint16_t port, const std::string& payload,
                               int timeoutMs, int retryMs, std::string& outError) {
#ifdef _WIN32
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            outError = "WSAStartup failed";
            return false;
        }
#endif

        const auto start = std::chrono::steady_clock::now();
        while (true) {
#ifdef _WIN32
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET) {
#else
            int sock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock < 0) {
#endif
                outError = "socket failed";
                break;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
#ifdef _WIN32
            addr.sin_addr.S_un.S_addr = inet_addr(host.c_str());
#else
            addr.sin_addr.s_addr = inet_addr(host.c_str());
#endif

            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
                const char* data = payload.c_str();
                const int total = (int)payload.size();
                int sent = 0;
                while (sent < total) {
                    const int n = (int)send(sock, data + sent, total - sent, 0);
                    if (n <= 0) break;
                    sent += n;
                }
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                if (sent == total) {
                    return true;
                }
                outError = "send failed";
                break;
            }

#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs) {
                outError = "connect timeout";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(10, retryMs)));
        }

#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    static void ListenForAmuletReturn(uint16_t port, std::atomic<int>& state, int timeoutSec) {
#ifdef _WIN32
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            state.store(2);
            return;
        }
#endif

    #ifdef _WIN32
        SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == INVALID_SOCKET) {
    #else
        int server = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server < 0) {
    #endif
            state.store(2);
            return;
        }

        int opt = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
#ifdef _WIN32
        addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
#else
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif

        if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
            closesocket(server);
            WSACleanup();
#else
            close(server);
#endif
            state.store(2);
            return;
        }

        if (listen(server, 1) != 0) {
#ifdef _WIN32
            closesocket(server);
            WSACleanup();
#else
            close(server);
#endif
            state.store(2);
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        while (state.load() == 0) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(server, &set);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
#ifdef _WIN32
            const int ready = select(0, &set, nullptr, nullptr, &tv);
#else
            const int ready = select(server + 1, &set, nullptr, nullptr, &tv);
#endif
            if (ready > 0 && FD_ISSET(server, &set)) {
                sockaddr_in client{};
                socklen_t clen = sizeof(client);
#ifdef _WIN32
                SOCKET clientSock = accept(server, (sockaddr*)&client, &clen);
                if (clientSock != INVALID_SOCKET) {
#else
                int clientSock = (int)accept(server, (sockaddr*)&client, &clen);
                if (clientSock >= 0) {
#endif
                    char buf[4096];
                    const int n = (int)recv(clientSock, buf, sizeof(buf) - 1, 0);
                    if (n > 0) {
                        buf[n] = '\0';
                        const std::string msg = ToLowerCopy(std::string(buf, n));
                        const bool ok = (msg.find("save complete") != std::string::npos) ||
                                        (msg.find("save_complete") != std::string::npos);
                        state.store(ok ? 1 : 2);
                    } else {
                        state.store(2);
                    }
#ifdef _WIN32
                    closesocket(clientSock);
#else
                    close(clientSock);
#endif
                    break;
                }
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (timeoutSec > 0 && elapsed >= timeoutSec) {
                state.store(2);
                break;
            }
        }

#ifdef _WIN32
        closesocket(server);
        WSACleanup();
#else
        close(server);
#endif
    }

    static std::string GetBindlessBlacklistKey() {
        const char* vendor = (const char*)glGetString(GL_VENDOR);
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* version = (const char*)glGetString(GL_VERSION);

        std::ostringstream ss;
        ss << (vendor ? vendor : "") << " | "
           << (renderer ? renderer : "") << " | "
           << (version ? version : "");
        return ss.str();
    }

    static bool IsBindlessBlacklisted(const std::string& key) {
        std::ifstream in("bindless_blacklist.txt");
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (line == key) return true;
        }
        return false;
    }

    static void ClearBindlessBlacklistFile() {
        std::error_code ec;
        std::filesystem::remove("bindless_blacklist.txt", ec);
    }

    static void AddBindlessBlacklist(const std::string& key) {
        if (key.empty()) return;
        if (IsBindlessBlacklisted(key)) return;
        std::ofstream out("bindless_blacklist.txt", std::ios::app);
        if (!out) return;
        out << key << "\n";
    }

    static bool IsArraySamplerUniformType(GLenum uniformType) {
        switch (uniformType) {
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_2D_ARRAY_SHADOW:
            case GL_INT_SAMPLER_2D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
                return true;
            default:
                return false;
        }
    }

    static bool Is2DSamplerUniformType(GLenum uniformType) {
        switch (uniformType) {
            case GL_SAMPLER_2D:
            case GL_SAMPLER_2D_SHADOW:
            case GL_INT_SAMPLER_2D:
            case GL_UNSIGNED_INT_SAMPLER_2D:
                return true;
            default:
                return false;
        }
    }

    static GLenum GetUniformType(GLuint program, const char* name) {
        const char* names[] = { name };
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(program, 1, names, &index);
        if (index == GL_INVALID_INDEX) return 0;

        GLint type = 0;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
        return (GLenum)type;
    }


    static void DumpFrameBufferPPMOnce(int width, int height) {
        static bool dumped = false;
        if (dumped) return;

        if (std::filesystem::exists("debug_frame0.ppm")) {
            dumped = true;
            return;
        }

        if (width <= 0 || height <= 0) return;

        std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        std::ofstream out("debug_frame0.ppm", std::ios::binary);
        if (!out) return;
        out << "P6\n" << width << " " << height << "\n255\n";

        for (int y = height - 1; y >= 0; --y) {
            const uint8_t* row = rgba.data() + (size_t(y) * size_t(width) * 4);
            for (int x = 0; x < width; ++x) {
                const uint8_t* px = row + size_t(x) * 4;
                out.put(char(px[0]));
                out.put(char(px[1]));
                out.put(char(px[2]));
            }
        }
        dumped = true;
        std::cout << "Wrote debug_frame0.ppm" << std::endl;
    }

    static GLuint CreateProceduralBlockTextureAtlas() {
        const int tileSize = GetEnvIntClamped("HVE_ATLAS_SIZE", 256, 64, 512);
        const int tileW = tileSize;
        const int tileH = tileSize;
        const int tilesX = kAtlasTilesX;
        const int tilesY = kAtlasTilesY;
        const int atlasW = tileW * tilesX;
        const int atlasH = tileH * tilesY;
        // Enable mipmaps by default so textures (especially grass) hold up at distance.
        const bool useMipmaps = GetEnvBool("HVE_ATLAS_MIPMAPS", true);

        auto hash01 = [](uint32_t x) -> float {
            // Simple integer hash to [0,1)
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return (x & 0x00FFFFFFu) / float(0x01000000u);
        };

        auto clampByte = [](int v) -> uint8_t {
            if (v < 0) return 0;
            if (v > 255) return 255;
            return (uint8_t)v;
        };

        auto smoothstep01 = [](float edge0, float edge1, float x) {
            float t = (x - edge0) / (edge1 - edge0);
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        auto mixByte = [](uint8_t a, uint8_t b, float t) -> uint8_t {
            t = std::clamp(t, 0.0f, 1.0f);
            return (uint8_t)std::clamp(int(std::round(float(a) + (float(b) - float(a)) * t)), 0, 255);
        };

        struct RGBA {
            uint8_t r, g, b, a;
        };

        struct LoadedTexture {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> rgba;
            bool valid = false;
        };

        std::unordered_map<std::string, LoadedTexture> textureCache;

        auto getTextureFromPath = [&](const std::string& cacheKey, const std::string& path) -> const LoadedTexture* {
            if (cacheKey.empty() || path.empty()) return nullptr;
            auto it = textureCache.find(cacheKey);
            if (it != textureCache.end()) {
                return it->second.valid ? &it->second : nullptr;
            }

            LoadedTexture loaded{};
            int w = 0, h = 0, channels = 0;
            unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
            if (data && w > 0 && h > 0) {
                loaded.w = w;
                loaded.h = h;
                loaded.rgba.assign(data, data + (size_t)w * (size_t)h * 4u);
                loaded.valid = true;
            }
            if (data) stbi_image_free(data);

            auto [insertedIt, _] = textureCache.emplace(cacheKey, std::move(loaded));
            return insertedIt->second.valid ? &insertedIt->second : nullptr;
        };

        auto getExternalTexture = [&](const std::string& texName) -> const LoadedTexture* {
            if (texName.empty()) return nullptr;
            const std::string path = ResolveMinecraftTexturePath(texName);
            if (path.empty()) return nullptr;
            return getTextureFromPath("mc:" + texName, path);
        };

        const RGBA glassClear{130, 180, 220, 255};
        const RGBA glassDyes[16] = {
            {235, 235, 235, 255}, // White
            {180, 180, 180, 255}, // Light Gray
            {105, 105, 105, 255}, // Gray
            {35,  35,  35,  255}, // Black
            {180, 40,  40,  255}, // Red
            {210, 100, 40,  255}, // Orange
            {225, 200, 45,  255}, // Yellow
            {120, 200, 45,  255}, // Lime
            {60,  140, 60,  255}, // Green
            {45,  160, 160, 255}, // Cyan
            {90,  150, 220, 255}, // Light Blue
            {50,  70,  190, 255}, // Blue
            {120, 70,  190, 255}, // Purple
            {190, 80,  170, 255}, // Magenta
            {220, 120, 160, 255}, // Pink
            {120, 80,  50,  255}  // Brown
        };

        auto setPx = [&](std::vector<uint8_t>& out, int x, int y, RGBA c) {
            const size_t idx = (size_t(y) * tileW + size_t(x)) * 4;
            out[idx + 0] = c.r;
            out[idx + 1] = c.g;
            out[idx + 2] = c.b;
            out[idx + 3] = c.a;
        };

        auto jitterColor = [&](RGBA base, float amp, uint32_t seed) -> RGBA {
            const float n = hash01(seed);
            const int j = int((n - 0.5f) * 2.0f * amp);
            return RGBA{
                clampByte(int(base.r) + j),
                clampByte(int(base.g) + j),
                clampByte(int(base.b) + j),
                base.a
            };
        };

        auto macroNoise = [&](int x, int y, int scale, uint32_t seed) {
            const int sx = (scale > 0) ? (x / scale) : x;
            const int sy = (scale > 0) ? (y / scale) : y;
            uint32_t s = seed ^ uint32_t(sx * 374761393u) ^ uint32_t(sy * 668265263u);
            return hash01(s) * 2.0f - 1.0f;
        };

        auto fillTile = [&](std::vector<uint8_t>& outRGBA, int tileIndex) {
            outRGBA.assign(tileW * tileH * 4, 255);

            const int blockId = tileIndex + 1;
            const int visualBlockId = blockId;
            const bool isGlassBlock = IsGlassBlockId(visualBlockId);
            const bool isPaneBlock = IsPaneBlockId(visualBlockId);
            const bool isArchitecture = IsArchitectureBlockId(visualBlockId);

            const std::string customTexPath = GetCustomTexturePathForBlockId(visualBlockId);
            const std::string externalTexName = GetTextureNameForBlockId(visualBlockId);

            const LoadedTexture* externalTex = nullptr;
            if (!customTexPath.empty()) {
                externalTex = getTextureFromPath("custom:" + customTexPath, customTexPath);
            }
            if (!externalTex) {
                externalTex = getExternalTexture(externalTexName);
            }

            // Tile indices correspond to block IDs (index = id-1)
            // 1..12: base blocks, 13..18: building blocks, 19..34: glass colors, 35..51: glass panes,
            // 52..83: architecture blocks

            for (int y = 0; y < tileH; ++y) {
                for (int x = 0; x < tileW; ++x) {
                    const uint32_t seed = uint32_t(tileIndex * 1337) ^ uint32_t(x * 92837111) ^ uint32_t(y * 689287499);

                    const float fx = (tileW > 1) ? (float)x / (float)(tileW - 1) : 0.0f;
                    const float fy = (tileH > 1) ? (float)y / (float)(tileH - 1) : 0.0f;
                    const float edge = std::min(std::min(fx, 1.0f - fx), std::min(fy, 1.0f - fy));
                    const float edgeDark = 1.0f - smoothstep01(0.02f, 0.10f, edge);

                    RGBA c{128, 128, 128, 255};

                    if (isGlassBlock || isPaneBlock) {
                        RGBA base = glassClear;
                        if (visualBlockId >= 19 && visualBlockId <= 34) {
                            base = glassDyes[visualBlockId - 19];
                        } else if (visualBlockId >= 36 && visualBlockId <= 51) {
                            base = glassDyes[visualBlockId - 36];
                        }

                        c = jitterColor(base, 10.0f, seed);
                        const int diag = (x - y + tileW) % 9;
                        if (diag == 0) {
                            c.r = clampByte(int(c.r) + 40);
                            c.g = clampByte(int(c.g) + 40);
                            c.b = clampByte(int(c.b) + 40);
                        }

                        if (isPaneBlock) {
                            const int border = std::max(1, tileW / 24);
                            if (x < border || y < border || x >= tileW - border || y >= tileH - border) {
                                c.r = clampByte(int(c.r) - 18);
                                c.g = clampByte(int(c.g) - 18);
                                c.b = clampByte(int(c.b) - 18);
                            }
                        }
                    } else {
                        switch (visualBlockId) {
                            case 1: { // Dirt
                                RGBA base{105, 74, 45, 255};
                                c = jitterColor(base, 22.0f, seed);
                                const float n = hash01(seed ^ 0xA123u);
                                if (n > 0.92f) c = jitterColor(RGBA{80, 55, 30, 255}, 18.0f, seed ^ 0x31u);
                                if (n < 0.04f) c = jitterColor(RGBA{140, 98, 58, 255}, 16.0f, seed ^ 0xB7u);
                                const float m = macroNoise(x, y, std::max(8, tileW / 12), seed ^ 0xD11Du);
                                c.r = clampByte(int(c.r) + int(m * 10.0f));
                                c.g = clampByte(int(c.g) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 4.0f));
                            } break;
                            case 2: { // Grass (more readable, better from distance)
                                // Richer base + multi-scale variation so mipmaps keep interesting detail.
                                RGBA base{60, 132, 52, 255};
                                c = jitterColor(base, 20.0f, seed);

                                const float m1 = macroNoise(x, y, std::max(14, tileW / 6), seed ^ 0xBEEFu);
                                const float m2 = macroNoise(x, y, std::max(6, tileW / 18), seed ^ 0xCAFEu);
                                const float m = 0.65f * m1 + 0.35f * m2;

                                // Broad patches (sunlit vs shaded)
                                c.g = clampByte(int(c.g) + int(m * 18.0f));
                                c.r = clampByte(int(c.r) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 4.0f));

                                // Tiny speckles: lighter blades + darker soil bits
                                const float p = hash01(seed ^ 0x55CCu);
                                if (p > 0.965f) {
                                    c.g = clampByte(int(c.g) + 30);
                                    c.r = clampByte(int(c.r) - 6);
                                } else if (p < 0.030f) {
                                    c.g = clampByte(int(c.g) - 26);
                                    c.r = clampByte(int(c.r) - 8);
                                    c.b = clampByte(int(c.b) - 6);
                                } else if (p > 0.90f) {
                                    // Warm, slightly yellow grass highlights
                                    c.r = clampByte(int(c.r) + 10);
                                    c.g = clampByte(int(c.g) + 12);
                                }
                            } break;
                            case 3: { // Stone
                                RGBA base{120, 120, 125, 255};
                                c = jitterColor(base, 24.0f, seed);
                                // Simple crack lines
                                const int v = (x + y * 5 + tileIndex * 3) % (tileW / 10 + 5);
                                if (v == 0 || v == 1) {
                                    c.r = clampByte(int(c.r) - 28);
                                    c.g = clampByte(int(c.g) - 28);
                                    c.b = clampByte(int(c.b) - 28);
                                }
                                const float m = macroNoise(x, y, std::max(10, tileW / 9), seed ^ 0xC0DEu);
                                c.r = clampByte(int(c.r) + int(m * 10.0f));
                                c.g = clampByte(int(c.g) + int(m * 10.0f));
                                c.b = clampByte(int(c.b) + int(m * 10.0f));
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 24.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 24.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 24.0f));
                                }
                            } break;
                            case 4: { // Sand
                                RGBA base{210, 195, 150, 255};
                                c = jitterColor(base, 16.0f, seed);
                                const float n = hash01(seed ^ 0x9911u);
                                if (n > 0.94f) {
                                    c.r = clampByte(int(c.r) - 14);
                                    c.g = clampByte(int(c.g) - 14);
                                    c.b = clampByte(int(c.b) - 8);
                                }
                                const float m = macroNoise(x, y, std::max(10, tileW / 9), seed ^ 0xA55Au);
                                c.r = clampByte(int(c.r) + int(m * 8.0f));
                                c.g = clampByte(int(c.g) + int(m * 7.0f));
                                c.b = clampByte(int(c.b) + int(m * 5.0f));
                            } break;
                            case 5: { // Water (texture is subtle; shader does most of the look)
                                RGBA base{60, 120, 210, 255};
                                c = jitterColor(base, 18.0f, seed);
                                const int band = (y + (int)(hash01(seed ^ 0x123u) * 3.0f)) % 6;
                                if (band == 0) {
                                    c.b = clampByte(int(c.b) + 25);
                                    c.g = clampByte(int(c.g) + 10);
                                }
                            } break;
                            case 6: { // Snow
                                RGBA base{240, 240, 250, 255};
                                c = jitterColor(base, 14.0f, seed);
                                const float shade = (float)y / float(tileH - 1);
                                c.b = clampByte(int(c.b) + int(10.0f * (1.0f - shade)));
                            } break;
                            case 7: { // Wood (planks)
                                const int plankW = std::max(6, tileW / 8);
                                const int plank = x / plankW;
                                const uint32_t pseed = seed ^ uint32_t(plank * 0x9E37u);
                                RGBA base{105, 70, 40, 255};
                                c = jitterColor(base, 16.0f, pseed);
                                if (x % plankW == 0) {
                                    c.r = clampByte(int(c.r) - 22);
                                    c.g = clampByte(int(c.g) - 16);
                                    c.b = clampByte(int(c.b) - 10);
                                }
                                // Knot
                                const int cx = plank * plankW + plankW / 2;
                                const int cy = tileH / 2;
                                const int dx = x - cx;
                                const int dy = y - cy;
                                if (dx * dx + dy * dy <= 4) {
                                    c.r = clampByte(int(c.r) - 30);
                                    c.g = clampByte(int(c.g) - 20);
                                    c.b = clampByte(int(c.b) - 10);
                                }
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 18.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 12.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 8.0f));
                                }
                            } break;
                            case 8: { // Leaves
                                RGBA base{35, 95, 35, 255};
                                c = jitterColor(base, 26.0f, seed);
                                const float n = hash01(seed ^ 0x0F0Fu);
                                if (n > 0.90f) {
                                    c.g = clampByte(int(c.g) + 28);
                                }
                                if (n < 0.07f) {
                                    c.r = clampByte(int(c.r) - 10);
                                    c.g = clampByte(int(c.g) - 18);
                                    c.b = clampByte(int(c.b) - 10);
                                }
                            } break;
                            case 9: { // Concrete
                                RGBA base{200, 200, 200, 255};
                                c = jitterColor(base, 14.0f, seed);
                                const float agg = hash01(seed ^ 0x2222u);
                                if (agg > 0.92f) {
                                    c.r = clampByte(int(c.r) - 28);
                                    c.g = clampByte(int(c.g) - 28);
                                    c.b = clampByte(int(c.b) - 28);
                                }
                                const float m = macroNoise(x, y, std::max(10, tileW / 10), seed ^ 0xACEDu);
                                c.r = clampByte(int(c.r) + int(m * 8.0f));
                                c.g = clampByte(int(c.g) + int(m * 8.0f));
                                c.b = clampByte(int(c.b) + int(m * 8.0f));
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 22.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 22.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 22.0f));
                                }
                            } break;
                            case 10: { // Brick
                                // Brick layout: scaled with tile size
                                const int mortar = std::max(1, tileW / 64);
                                const int brickW = std::max(10, tileW / 8);
                                const int brickH = std::max(8, tileH / 8);
                                const int row = y / brickH;
                                const int xOff = (row & 1) ? (brickW / 2) : 0;
                                const int lx = (x + xOff) % brickW;
                                const int ly = y % brickH;
                                const bool isMortar = (lx < mortar) || (ly < mortar);
                                if (isMortar) {
                                    c = jitterColor(RGBA{210, 210, 210, 255}, 10.0f, seed);
                                } else {
                                    const int brick = (x + xOff) / brickW + row * 7;
                                    const uint32_t bseed = seed ^ uint32_t(brick * 0xB5297A4Du);
                                    c = jitterColor(RGBA{160, 60, 60, 255}, 26.0f, bseed);
                                    // Edge darkening
                                    if (lx == mortar || lx == brickW - 1 || ly == mortar || ly == brickH - 1) {
                                        c.r = clampByte(int(c.r) - 16);
                                        c.g = clampByte(int(c.g) - 10);
                                        c.b = clampByte(int(c.b) - 10);
                                    }
                                }
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 18.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 12.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 12.0f));
                                }
                            } break;
                            case 12: { // Metal
                                RGBA base{120, 120, 140, 255};
                                c = jitterColor(base, 18.0f, seed);
                                // Panel seams
                                const int seam = tileW / 2;
                                if (x == 0 || y == 0 || x == tileW - 1 || y == tileH - 1 || x == seam || y == seam) {
                                    c.r = clampByte(int(c.r) - 35);
                                    c.g = clampByte(int(c.g) - 35);
                                    c.b = clampByte(int(c.b) - 35);
                                }
                                // Rivets
                                const int rv = std::max(2, tileW / 16);
                                if ((x == rv || x == tileW - rv - 1) && (y == rv || y == tileH - rv - 1)) {
                                    c.r = clampByte(int(c.r) + 25);
                                    c.g = clampByte(int(c.g) + 25);
                                    c.b = clampByte(int(c.b) + 25);
                                }
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 24.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 24.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 24.0f));
                                }
                            } break;
                            case 13: { // Marble
                                RGBA base{225, 225, 232, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const float v = macroNoise(x, y, std::max(12, tileW / 10), seed ^ 0x1A2Bu);
                                if (v > 0.35f) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 20);
                                    c.b = clampByte(int(c.b) - 10);
                                }
                            } break;
                            case 14: { // Basalt
                                RGBA base{45, 45, 50, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const float n = hash01(seed ^ 0x7B7Bu);
                                if (n > 0.92f) {
                                    c.r = clampByte(int(c.r) + 25);
                                    c.g = clampByte(int(c.g) + 25);
                                    c.b = clampByte(int(c.b) + 25);
                                }
                            } break;
                            case 15: { // Limestone
                                RGBA base{210, 200, 175, 255};
                                c = jitterColor(base, 14.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 12), seed ^ 0x3C3Cu);
                                c.r = clampByte(int(c.r) + int(m * 6.0f));
                                c.g = clampByte(int(c.g) + int(m * 4.0f));
                                c.b = clampByte(int(c.b) + int(m * 3.0f));
                            } break;
                            case 16: { // Slate
                                RGBA base{70, 80, 95, 255};
                                c = jitterColor(base, 14.0f, seed);
                                const float m = macroNoise(x, y, std::max(12, tileW / 10), seed ^ 0x5D5Du);
                                c.r = clampByte(int(c.r) + int(m * 10.0f));
                                c.g = clampByte(int(c.g) + int(m * 10.0f));
                                c.b = clampByte(int(c.b) + int(m * 12.0f));
                            } break;
                            case 17: { // Terracotta
                                RGBA base{180, 90, 55, 255};
                                c = jitterColor(base, 16.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 11), seed ^ 0x6E6Eu);
                                c.r = clampByte(int(c.r) + int(m * 12.0f));
                                c.g = clampByte(int(c.g) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 6.0f));
                            } break;
                            case 18: { // Asphalt
                                RGBA base{50, 50, 52, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const float n = hash01(seed ^ 0x9A9Au);
                                if (n > 0.90f) {
                                    c.r = clampByte(int(c.r) + 30);
                                    c.g = clampByte(int(c.g) + 30);
                                    c.b = clampByte(int(c.b) + 30);
                                }
                            } break;
                            case 52: { // Smooth Concrete
                                RGBA base{185, 185, 190, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const float m = macroNoise(x, y, std::max(12, tileW / 10), seed ^ 0x2A2Au);
                                c.r = clampByte(int(c.r) + int(m * 6.0f));
                                c.g = clampByte(int(c.g) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 6.0f));
                            } break;
                            case 53: { // Dark Concrete
                                RGBA base{55, 56, 60, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const float m = macroNoise(x, y, std::max(12, tileW / 10), seed ^ 0x3A3Au);
                                c.r = clampByte(int(c.r) + int(m * 5.0f));
                                c.g = clampByte(int(c.g) + int(m * 5.0f));
                                c.b = clampByte(int(c.b) + int(m * 5.0f));
                            } break;
                            case 54: { // White Concrete
                                RGBA base{220, 220, 225, 255};
                                c = jitterColor(base, 6.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 12), seed ^ 0x4A4Au);
                                c.r = clampByte(int(c.r) + int(m * 4.0f));
                                c.g = clampByte(int(c.g) + int(m * 4.0f));
                                c.b = clampByte(int(c.b) + int(m * 4.0f));
                            } break;
                            case 55: { // Concrete Tiles
                                RGBA base{165, 166, 172, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const int grout = std::max(1, tileW / 24);
                                const int tile = std::max(8, tileW / 5);
                                if (x % tile < grout || y % tile < grout) {
                                    c.r = clampByte(int(c.r) - 25);
                                    c.g = clampByte(int(c.g) - 25);
                                    c.b = clampByte(int(c.b) - 25);
                                }
                            } break;
                            case 56: { // Concrete Panels
                                RGBA base{150, 155, 160, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int seam = tileW / 2;
                                if (x == seam || y == seam) {
                                    c.r = clampByte(int(c.r) - 30);
                                    c.g = clampByte(int(c.g) - 30);
                                    c.b = clampByte(int(c.b) - 30);
                                }
                            } break;
                            case 57: { // Brushed Steel
                                RGBA base{150, 152, 160, 255};
                                c = jitterColor(base, 10.0f, seed);
                                if ((y + (tileIndex % 3)) % 6 == 0) {
                                    c.r = clampByte(int(c.r) + 20);
                                    c.g = clampByte(int(c.g) + 20);
                                    c.b = clampByte(int(c.b) + 20);
                                }
                            } break;
                            case 58: { // Steel Plate
                                RGBA base{120, 122, 130, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int seam = tileW / 3;
                                if (x == seam || y == seam || x == tileW - seam - 1 || y == tileH - seam - 1) {
                                    c.r = clampByte(int(c.r) - 25);
                                    c.g = clampByte(int(c.g) - 25);
                                    c.b = clampByte(int(c.b) - 25);
                                }
                            } break;
                            case 59: { // Steel Grate
                                RGBA base{105, 110, 120, 255};
                                c = jitterColor(base, 12.0f, seed);
                                if (x % 5 == 0 || y % 5 == 0) {
                                    c.r = clampByte(int(c.r) + 20);
                                    c.g = clampByte(int(c.g) + 20);
                                    c.b = clampByte(int(c.b) + 20);
                                }
                            } break;
                            case 60: { // Aluminum Panel
                                RGBA base{190, 195, 205, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int seam = tileW / 2;
                                if (x == seam || y == seam) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 20);
                                    c.b = clampByte(int(c.b) - 20);
                                }
                            } break;
                            case 61: { // Carbon Steel
                                RGBA base{70, 72, 78, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 10), seed ^ 0x5A5Au);
                                c.r = clampByte(int(c.r) + int(m * 10.0f));
                                c.g = clampByte(int(c.g) + int(m * 10.0f));
                                c.b = clampByte(int(c.b) + int(m * 10.0f));
                            } break;
                            case 62: { // Copper Panel
                                RGBA base{170, 105, 70, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const int seam = tileW / 2;
                                if (x == seam || y == seam) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 12);
                                    c.b = clampByte(int(c.b) - 10);
                                }
                            } break;
                            case 63: { // Oxide Copper
                                RGBA base{80, 140, 120, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 12), seed ^ 0x6A6Au);
                                c.r = clampByte(int(c.r) + int(m * 8.0f));
                                c.g = clampByte(int(c.g) + int(m * 10.0f));
                                c.b = clampByte(int(c.b) + int(m * 8.0f));
                            } break;
                            case 64: { // Polished Stone
                                RGBA base{150, 150, 155, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const float m = macroNoise(x, y, std::max(14, tileW / 8), seed ^ 0x7A7Au);
                                c.r = clampByte(int(c.r) + int(m * 6.0f));
                                c.g = clampByte(int(c.g) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 6.0f));
                            } break;
                            case 65: { // Polished Basalt
                                RGBA base{70, 72, 78, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const float m = macroNoise(x, y, std::max(12, tileW / 10), seed ^ 0x8A8Au);
                                c.r = clampByte(int(c.r) + int(m * 5.0f));
                                c.g = clampByte(int(c.g) + int(m * 5.0f));
                                c.b = clampByte(int(c.b) + int(m * 5.0f));
                            } break;
                            case 66: { // Polished Granite
                                RGBA base{160, 130, 115, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const float n = hash01(seed ^ 0x9B9Bu);
                                if (n > 0.92f) {
                                    c.r = clampByte(int(c.r) + 20);
                                    c.g = clampByte(int(c.g) + 10);
                                }
                            } break;
                            case 67: { // Polished Limestone
                                RGBA base{200, 185, 165, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const float m = macroNoise(x, y, std::max(10, tileW / 12), seed ^ 0xABABu);
                                c.r = clampByte(int(c.r) + int(m * 6.0f));
                                c.g = clampByte(int(c.g) + int(m * 5.0f));
                                c.b = clampByte(int(c.b) + int(m * 4.0f));
                            } break;
                            case 68: { // Slate Tiles
                                RGBA base{85, 95, 110, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int grout = std::max(1, tileW / 28);
                                const int tile = std::max(10, tileW / 6);
                                if (x % tile < grout || y % tile < grout) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 20);
                                    c.b = clampByte(int(c.b) - 20);
                                }
                            } break;
                            case 69: { // Roof Shingles
                                RGBA base{140, 120, 95, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const int row = y / std::max(6, tileH / 6);
                                if ((x + row * 3) % std::max(8, tileW / 6) < 2) {
                                    c.r = clampByte(int(c.r) - 15);
                                    c.g = clampByte(int(c.g) - 10);
                                    c.b = clampByte(int(c.b) - 8);
                                }
                            } break;
                            case 70: { // Asphalt Shingles
                                RGBA base{55, 55, 60, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int row = y / std::max(6, tileH / 6);
                                if ((x + row * 2) % std::max(8, tileW / 6) < 2) {
                                    c.r = clampByte(int(c.r) - 12);
                                    c.g = clampByte(int(c.g) - 12);
                                    c.b = clampByte(int(c.b) - 12);
                                }
                            } break;
                            case 71: { // Ceramic Tile
                                RGBA base{210, 210, 220, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const int grout = std::max(1, tileW / 26);
                                const int tile = std::max(8, tileW / 5);
                                if (x % tile < grout || y % tile < grout) {
                                    c.r = clampByte(int(c.r) - 25);
                                    c.g = clampByte(int(c.g) - 25);
                                    c.b = clampByte(int(c.b) - 25);
                                }
                            } break;
                            case 72: { // Ceramic Tile Dark
                                RGBA base{85, 90, 100, 255};
                                c = jitterColor(base, 8.0f, seed);
                                const int grout = std::max(1, tileW / 26);
                                const int tile = std::max(8, tileW / 5);
                                if (x % tile < grout || y % tile < grout) {
                                    c.r = clampByte(int(c.r) - 18);
                                    c.g = clampByte(int(c.g) - 18);
                                    c.b = clampByte(int(c.b) - 18);
                                }
                            } break;
                            case 73: { // Plaster
                                RGBA base{220, 210, 195, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const float m = macroNoise(x, y, std::max(8, tileW / 14), seed ^ 0xBDBDu);
                                c.r = clampByte(int(c.r) + int(m * 8.0f));
                                c.g = clampByte(int(c.g) + int(m * 6.0f));
                                c.b = clampByte(int(c.b) + int(m * 6.0f));
                            } break;
                            case 74: { // Stucco
                                RGBA base{195, 175, 155, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const float n = hash01(seed ^ 0xCDCDu);
                                if (n > 0.88f) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 16);
                                    c.b = clampByte(int(c.b) - 12);
                                }
                            } break;
                            case 75: { // Brick White
                                RGBA base{220, 220, 225, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int mortar = std::max(1, tileW / 64);
                                const int brickW = std::max(10, tileW / 8);
                                const int brickH = std::max(8, tileH / 8);
                                const int row = y / brickH;
                                const int xOff = (row & 1) ? (brickW / 2) : 0;
                                const int lx = (x + xOff) % brickW;
                                const int ly = y % brickH;
                                if (lx < mortar || ly < mortar) {
                                    c.r = clampByte(int(c.r) - 18);
                                    c.g = clampByte(int(c.g) - 18);
                                    c.b = clampByte(int(c.b) - 18);
                                }
                            } break;
                            case 76: { // Brick Dark
                                RGBA base{60, 60, 65, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int mortar = std::max(1, tileW / 64);
                                const int brickW = std::max(10, tileW / 8);
                                const int brickH = std::max(8, tileH / 8);
                                const int row = y / brickH;
                                const int xOff = (row & 1) ? (brickW / 2) : 0;
                                const int lx = (x + xOff) % brickW;
                                const int ly = y % brickH;
                                if (lx < mortar || ly < mortar) {
                                    c.r = clampByte(int(c.r) - 16);
                                    c.g = clampByte(int(c.g) - 16);
                                    c.b = clampByte(int(c.b) - 16);
                                }
                            } break;
                            case 77: { // Trim Stone
                                RGBA base{190, 185, 180, 255};
                                c = jitterColor(base, 10.0f, seed);
                                if (edgeDark > 0.0f) {
                                    c.r = clampByte(int(c.r) - int(edgeDark * 20.0f));
                                    c.g = clampByte(int(c.g) - int(edgeDark * 20.0f));
                                    c.b = clampByte(int(c.b) - int(edgeDark * 20.0f));
                                }
                            } break;
                            case 78: { // Column Stone
                                RGBA base{170, 165, 160, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int band = (x + y) % std::max(10, tileW / 6);
                                if (band < 2) {
                                    c.r = clampByte(int(c.r) - 18);
                                    c.g = clampByte(int(c.g) - 18);
                                    c.b = clampByte(int(c.b) - 18);
                                }
                            } break;
                            case 79: { // Window Frame
                                RGBA base{130, 135, 150, 255};
                                c = jitterColor(base, 10.0f, seed);
                                const int border = std::max(1, tileW / 14);
                                if (x < border || y < border || x >= tileW - border || y >= tileH - border) {
                                    c.r = clampByte(int(c.r) - 24);
                                    c.g = clampByte(int(c.g) - 24);
                                    c.b = clampByte(int(c.b) - 24);
                                }
                            } break;
                            case 80: { // Vent Panel
                                RGBA base{90, 95, 105, 255};
                                c = jitterColor(base, 10.0f, seed);
                                if (x % 4 == 0) {
                                    c.r = clampByte(int(c.r) - 20);
                                    c.g = clampByte(int(c.g) - 20);
                                    c.b = clampByte(int(c.b) - 20);
                                }
                            } break;
                            case 81: { // Circuit Panel
                                RGBA base{55, 75, 95, 255};
                                c = jitterColor(base, 10.0f, seed);
                                if ((x + y) % 7 == 0) {
                                    c.r = clampByte(int(c.r) + 25);
                                    c.g = clampByte(int(c.g) + 35);
                                    c.b = clampByte(int(c.b) + 25);
                                }
                            } break;
                            case 82: { // Light Panel
                                RGBA base{235, 230, 180, 255};
                                c = jitterColor(base, 6.0f, seed);
                                const int border = std::max(1, tileW / 16);
                                if (x < border || y < border || x >= tileW - border || y >= tileH - border) {
                                    c.r = clampByte(int(c.r) - 25);
                                    c.g = clampByte(int(c.g) - 25);
                                    c.b = clampByte(int(c.b) - 25);
                                }
                            } break;
                            case 83: { // Structural Beam
                                RGBA base{125, 135, 145, 255};
                                c = jitterColor(base, 12.0f, seed);
                                const int seam = tileW / 3;
                                if (x == seam || x == tileW - seam - 1) {
                                    c.r = clampByte(int(c.r) - 25);
                                    c.g = clampByte(int(c.g) - 25);
                                    c.b = clampByte(int(c.b) - 25);
                                }
                            } break;
                            default: {
                                c = jitterColor(RGBA{255, 0, 255, 255}, 0.0f, seed);
                            } break;
                        }
                    }

                    if (isGlassBlock || isPaneBlock) {
                        const float centerDist = std::abs(fx - 0.5f) + std::abs(fy - 0.5f);
                        const float centerGlow = std::clamp(1.0f - centerDist * 1.35f, 0.0f, 1.0f);
                        c.r = clampByte(int(c.r) + int(centerGlow * 12.0f));
                        c.g = clampByte(int(c.g) + int(centerGlow * 14.0f));
                        c.b = clampByte(int(c.b) + int(centerGlow * 18.0f));

                        const int frame = std::max(1, tileW / 18);
                        if (x < frame || y < frame || x >= tileW - frame || y >= tileH - frame) {
                            c.r = clampByte(int(c.r) - 16);
                            c.g = clampByte(int(c.g) - 16);
                            c.b = clampByte(int(c.b) - 12);
                        }

                        const float streak = hash01(seed ^ 0xA51Cu);
                        if (streak > 0.92f && ((x + y) % std::max(7, tileW / 10) == 0)) {
                            c.r = clampByte(int(c.r) + 28);
                            c.g = clampByte(int(c.g) + 28);
                            c.b = clampByte(int(c.b) + 30);
                        }
                    }

                    if (externalTex && externalTex->valid) {
                        const int sx = std::clamp((x * externalTex->w) / std::max(1, tileW), 0, externalTex->w - 1);
                        const int sy = std::clamp((y * externalTex->h) / std::max(1, tileH), 0, externalTex->h - 1);
                        const size_t si = (size_t(sy) * (size_t)externalTex->w + (size_t)sx) * 4u;

                        const uint8_t tr = externalTex->rgba[si + 0];
                        const uint8_t tg = externalTex->rgba[si + 1];
                        const uint8_t tb = externalTex->rgba[si + 2];
                        const uint8_t ta = externalTex->rgba[si + 3];

                        float blend = 0.72f;
                        if (isGlassBlock || isPaneBlock) blend = 0.86f;
                        else if (isArchitecture) blend = 0.78f;
                        blend *= float(ta) / 255.0f;

                        c.r = mixByte(c.r, tr, blend);
                        c.g = mixByte(c.g, tg, blend);
                        c.b = mixByte(c.b, tb, blend);
                    }

                    if (IsArchitectureBlockId(visualBlockId)) {
                        const float coarse = macroNoise(x, y, std::max(8, tileW / 11), seed ^ 0xE11Eu);
                        const float fine = macroNoise(x, y, std::max(3, tileW / 24), seed ^ 0xF22Fu);

                        if (visualBlockId >= 52 && visualBlockId <= 56) {
                            c.r = clampByte(int(c.r) + int(coarse * 7.0f + fine * 4.0f));
                            c.g = clampByte(int(c.g) + int(coarse * 7.0f + fine * 4.0f));
                            c.b = clampByte(int(c.b) + int(coarse * 7.0f + fine * 4.0f));
                        } else if (visualBlockId >= 57 && visualBlockId <= 63) {
                            c.r = clampByte(int(c.r) + int(coarse * 6.0f + fine * 10.0f));
                            c.g = clampByte(int(c.g) + int(coarse * 6.0f + fine * 10.0f));
                            c.b = clampByte(int(c.b) + int(coarse * 6.0f + fine * 12.0f));
                            if (((x + tileIndex) % std::max(9, tileW / 7)) == 0) {
                                c.r = clampByte(int(c.r) + 12);
                                c.g = clampByte(int(c.g) + 12);
                                c.b = clampByte(int(c.b) + 14);
                            }
                        } else if (visualBlockId >= 64 && visualBlockId <= 70) {
                            c.r = clampByte(int(c.r) + int(coarse * 9.0f));
                            c.g = clampByte(int(c.g) + int(coarse * 8.0f));
                            c.b = clampByte(int(c.b) + int(coarse * 7.0f));
                        } else if (visualBlockId >= 71 && visualBlockId <= 76) {
                            c.r = clampByte(int(c.r) + int(coarse * 6.0f + fine * 5.0f));
                            c.g = clampByte(int(c.g) + int(coarse * 6.0f + fine * 5.0f));
                            c.b = clampByte(int(c.b) + int(coarse * 5.0f + fine * 4.0f));
                        } else {
                            c.r = clampByte(int(c.r) + int(coarse * 7.0f));
                            c.g = clampByte(int(c.g) + int(coarse * 7.0f));
                            c.b = clampByte(int(c.b) + int(coarse * 8.0f));
                        }

                        if (edgeDark > 0.0f) {
                            const int d = int(edgeDark * 18.0f);
                            c.r = clampByte(int(c.r) - d);
                            c.g = clampByte(int(c.g) - d);
                            c.b = clampByte(int(c.b) - d);
                        }
                    }

                    setPx(outRGBA, x, y, c);
                }
            }
        };

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        // Prefer trilinear + mipmaps for readability at distance.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, useMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    #ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
        {
            const float requestedAniso = float(GetEnvIntClamped("HVE_ATLAS_ANISO", 12, 1, 16));
            GLfloat maxAniso = 0.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            if (maxAniso > 1.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(requestedAniso, maxAniso));
            }
        }
    #endif

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        std::vector<uint8_t> pixels;

        auto uploadTile = [&](int tileIndex) {
            fillTile(pixels, tileIndex);
            const int tileX = tileIndex % tilesX;
            const int tileY = tileIndex / tilesX;
            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            tileX * tileW, tileY * tileH,
                            tileW, tileH,
                            GL_RGBA, GL_UNSIGNED_BYTE,
                            pixels.data());
        };

        // 8x8 atlas tiles. Fill all tiles so unused slots are deterministic.
        for (int tileIndex = 0; tileIndex < tilesX * tilesY; ++tileIndex) {
            uploadTile(tileIndex);
        }

        if (useMipmaps) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    static GLuint LoadHighEndTextures() {
        const std::string g = ResolveHighEndTexturePath("grass.png");
        const std::string d = ResolveHighEndTexturePath("dirt.png");
        const std::string s = ResolveHighEndTexturePath("stone.png");
        const std::string w = ResolveHighEndTexturePath("water.png");

        if (!g.empty() && !d.empty() && !s.empty() && !w.empty()) {
            std::cout << "[HIGH-END] assets/textures/highend gefunden (grass/dirt/stone/water)" << std::endl;
            std::cout << "[HIGH-END] Atlas: mipmaps + trilinear + anisotropic aktiviert" << std::endl;
        } else {
            std::cout << "[HIGH-END] Warnung: unvollständige High-End Texturen, nutze Fallback-Mapping" << std::endl;
        }

        return CreateProceduralBlockTextureAtlas();
    }

    Application::Application() {
        Window::Init(1600, 900, "High Performance Voxel Engine");
        Input::Init();
        Input::SetCursorMode(true); // Capture mouse
        Renderer::Init();
        Game::World::Generation::Init(12345);
        Engine::QualityManager::Init();
        
        m_Registry = std::make_unique<Registry>();
    }

    Application::~Application() {
        Renderer::Shutdown();
        Window::Shutdown();
    }

    void Application::Run() {
        // Setup Camera - start high enough for immediate terrain visibility.
        Camera camera(glm::vec3(384.0f, 105.0f, 384.0f));
        camera.Pitch = -35.0f;
        camera.Yaw = -135.0f;
        camera.updateCameraVectors();

        auto resetCamera = [&]() {
            camera.Position = glm::vec3(384.0f, 105.0f, 384.0f);
            camera.Pitch = -35.0f;
            camera.Yaw = -135.0f;
            camera.Velocity = glm::vec3(0.0f);
            camera.updateCameraVectors();
        };

        const char* settingsPath = std::getenv("HVE_SETTINGS_FILE");
        if (!settingsPath || !*settingsPath) settingsPath = "hve_settings.ini";
        std::unordered_map<std::string, std::string> ini = LoadIniKeyValue(settingsPath);

        bool dumpFrameEnabled = GetEnvBool("HVE_DUMP_FRAME", true);
        const bool logHits = GetEnvBool("HVE_LOG_HITS", false);
        const bool hardSafeMode = GetEnvBool("HVE_HARD_SAFE_MODE", false);
        const bool horizonMode = GetEnvBool("HVE_HORIZON_MODE", true);
        const float fogDensityBase = GetEnvFloatClamped("HVE_FOG_DENSITY", 0.0020f, 0.0001f, 0.03f);
        const float fogNightBoost = GetEnvFloatClamped("HVE_FOG_NIGHT_BOOST", 1.22f, 1.0f, 2.5f);
        const int wireframeStreamCapCfg = GetEnvIntClamped("HVE_WIREFRAME_STREAM", 16, 8, 96);
        const int wireframeUploadCapCfg = GetEnvIntClamped("HVE_WIREFRAME_UPLOAD", 12, 4, 64);
        const int wireframeHeightCapCfg = GetEnvIntClamped("HVE_WIREFRAME_HEIGHT", 8, 4, 32);
        const int wireframeCullChunksCfg = GetEnvIntClamped("HVE_WIREFRAME_CULL", 14, 6, 64);
        const int streamDistanceCap = GetEnvIntClamped("HVE_STREAM_DISTANCE_CAP", 2048, 64, 32768);
        int viewDistanceChunks = GetEnvIntClamped("HVE_VIEWDIST", 64, 1, streamDistanceCap);
        int streamMarginChunks = GetEnvIntClamped("HVE_STREAM_MARGIN", 48, 0, streamDistanceCap);
        int streamDistanceChunks = std::min(streamDistanceCap, viewDistanceChunks + streamMarginChunks);
        const int heightChunks = GetEnvIntClamped("HVE_HEIGHT_CHUNKS", 128, 1, 256); // Default 128 (4096 blocks), max 256
        const int preloadHeightChunks = GetEnvIntClamped("HVE_PRELOAD_HEIGHT_CHUNKS", std::min(32, heightChunks), 1, 256);

        int uploadBudget = GetEnvIntClamped("HVE_UPLOAD_BUDGET", 512, 1, 2048);
        int baseUploadBudget = uploadBudget;
        const bool adaptiveBudget = GetEnvBool("HVE_ADAPTIVE_STREAM_BUDGET", true);
        const bool largeMode = GetEnvBool("HVE_LARGE_MODE", true);
        const float largeTargetFps = GetEnvFloatClamped("HVE_LARGE_TARGET_FPS", 55.0f, 30.0f, 120.0f);
        const int largeMinStream = GetEnvIntClamped("HVE_LARGE_MIN_STREAM", 64, 1, streamDistanceCap);
        const int largeMinUpload = GetEnvIntClamped("HVE_LARGE_MIN_UPLOAD", 64, 1, 1024);
        const int largeUploadBoost = GetEnvIntClamped("HVE_LARGE_UPLOAD_BOOST", 256, 0, 1024);
        const bool lowEndController = GetEnvBool("HVE_LOWEND_CONTROLLER", true);
        const float lowEndTargetMs = GetEnvFloatClamped("HVE_LOWEND_TARGET_MS", 22.2f, 10.0f, 60.0f);
        const float lowEndHysteresisMs = GetEnvFloatClamped("HVE_LOWEND_HYST_MS", 0.6f, 0.1f, 8.0f);
        const float lowEndFastEma = GetEnvFloatClamped("HVE_LOWEND_EMA_FAST", 0.20f, 0.02f, 0.95f);
        const float lowEndSlowEma = GetEnvFloatClamped("HVE_LOWEND_EMA_SLOW", 0.06f, 0.01f, 0.50f);
        const int lowEndMaxLevel = GetEnvIntClamped("HVE_LOWEND_MAX_LEVEL", 5, 1, 8);
        bool disableUnload = GetEnvBool("HVE_DISABLE_UNLOAD", true);
        int cacheMarginChunks = GetEnvIntClamped("HVE_CACHE_MARGIN", 0, 0, 64);

        int preloadRadiusChunks = GetEnvIntClamped("HVE_PRELOAD_RADIUS", std::min(12, viewDistanceChunks), 0, streamDistanceCap);
        float preloadMaxSec = GetEnvFloatClamped("HVE_PRELOAD_MAX_SEC", 14.0f, 0.0f, 120.0f);
        bool preloadBlocksInput = GetEnvBool("HVE_PRELOAD_BLOCKS_INPUT", false);
        const bool instantHugeSight = GetEnvBool("HVE_INSTANT_HUGE_SIGHT", false);
        const bool instantHugeReposition = GetEnvBool("HVE_INSTANT_HUGE_REPOSITION", true);
        const int instantHugeRadius = GetEnvIntClamped("HVE_INSTANT_HUGE_RADIUS", 168, 16, streamDistanceCap);
        const int instantHugeVertical = GetEnvIntClamped("HVE_INSTANT_HUGE_VERTICAL", 64, 8, 1024);
        const bool megaPreloadContinuous = GetEnvBool("HVE_MEGA_PRELOAD_CONTINUOUS", false);
        const float megaPreloadPulseSec = GetEnvFloatClamped("HVE_MEGA_PRELOAD_PULSE_SEC", 0.8f, 0.05f, 10.0f);
        const int megaPreloadRadiusStep = GetEnvIntClamped("HVE_MEGA_PRELOAD_RADIUS_STEP", 64, 1, streamDistanceCap);
        const int megaPreloadRadiusMax = GetEnvIntClamped("HVE_MEGA_PRELOAD_RADIUS_MAX", streamDistanceCap, 16, streamDistanceCap);
        const int megaPreloadVerticalStep = GetEnvIntClamped("HVE_MEGA_PRELOAD_VERTICAL_STEP", 16, 1, 2048);
        const int megaPreloadVerticalMax = GetEnvIntClamped("HVE_MEGA_PRELOAD_VERTICAL_MAX", 512, 8, 2048);

        // Quality presets: tuned for "potato" -> "cinematic" without changing core rendering.
        // This only adjusts streaming/load behavior (view distance, upload budget, preload radius).
        int qualityLevel = GetEnvOrIniIntClamped("HVE_QUALITY", ini, "quality", 1, 0, 2); // 0=Low, 1=Med, 2=High
        auto hasEnvValue = [](const char* name) -> bool {
            const char* v = std::getenv(name);
            return v && *v;
        };
        const bool hasEnvViewDist = hasEnvValue("HVE_VIEWDIST");
        const bool hasEnvStreamMargin = hasEnvValue("HVE_STREAM_MARGIN");
        const bool hasEnvUploadBudget = hasEnvValue("HVE_UPLOAD_BUDGET");
        const bool hasEnvPreloadRadius = hasEnvValue("HVE_PRELOAD_RADIUS");
        const bool hasEnvPreloadMaxSec = hasEnvValue("HVE_PRELOAD_MAX_SEC");
        const bool autoHardwareQuality = GetEnvBool("HVE_AUTO_HW_QUALITY", true);
        const bool autoVegaAscension = GetEnvBool("HVE_AUTO_VEGA_ASCENSION", true);
        const HardwareProfile hardware = HardwareDetector::Detect();
        // INVENTION: Re-initialize QualityManager with hardware-aware limits.
        Engine::QualityManager::InitWithProfile(hardware);
        if (autoHardwareQuality && hardware.IsToasterClass()) {
            qualityLevel = 0;
        }
        std::cout << "Hardware: CPU(logical=" << hardware.logicalCores
                  << ", physical=" << hardware.physicalCores
                  << "), RAM=" << hardware.systemRamGB << " GB"
                  << ", GPU=" << hardware.gpuVendor << " / " << hardware.gpuRenderer
                  << ", Toaster=" << (hardware.IsToasterClass() ? "yes" : "no")
                  << std::endl;
        std::string worldFile = GetEnvOrIniString("HVE_WORLD_FILE", ini, "world_file", "hve_world.hvew");
        auto applyQuality = [&]() {
            int qViewDistance = viewDistanceChunks;
            int qStreamMargin = streamMarginChunks;
            int qUploadBudget = uploadBudget;
            int qPreloadRadius = preloadRadiusChunks;
            float qPreloadMaxSec = preloadMaxSec;
            if (qualityLevel == 0) {
                qViewDistance = 6;
                qStreamMargin = 2;
                qUploadBudget = 32;
                qPreloadRadius = 2;
                qPreloadMaxSec = 6.0f;
            } else if (qualityLevel == 1) {
                qViewDistance = 14;
                qStreamMargin = 6;
                qUploadBudget = 96;
                qPreloadRadius = 4;
                qPreloadMaxSec = 6.0f;
            } else {
                qViewDistance = 24;
                qStreamMargin = 10;
                qUploadBudget = 140;
                qPreloadRadius = 8;
                qPreloadMaxSec = 10.0f;
            }
            if (!hasEnvViewDist) viewDistanceChunks = qViewDistance;
            if (!hasEnvStreamMargin) streamMarginChunks = qStreamMargin;
            if (!hasEnvUploadBudget) uploadBudget = qUploadBudget;
            if (!hasEnvPreloadRadius) preloadRadiusChunks = qPreloadRadius;
            if (!hasEnvPreloadMaxSec) preloadMaxSec = qPreloadMaxSec;
            if (viewDistanceChunks < 1) viewDistanceChunks = 1;
            if (streamMarginChunks < 0) streamMarginChunks = 0;
            streamDistanceChunks = std::min(streamDistanceCap, viewDistanceChunks + streamMarginChunks);
            baseUploadBudget = uploadBudget;
        };
        applyQuality();

        auto gpuScoreFromTier = [&](Engine::GpuTier tier) {
            switch (tier) {
                case Engine::GpuTier::UltraLow: return 0.30f;
                case Engine::GpuTier::Low: return 0.48f;
                case Engine::GpuTier::Mid: return 0.72f;
                case Engine::GpuTier::High: return 1.00f;
                default: return 0.50f;
            }
        };

        const float gpuScore = gpuScoreFromTier(hardware.gpuTier);
        const float detailLevel = std::clamp((gpuScore * 0.8f) + ((float)hardware.systemRamGB * 1024.0f / 32000.0f), 0.3f, 1.0f);
        const int adaptiveViewDistance = std::clamp((int)std::round(64.0f + gpuScore * 96.0f), 48, 256);
        if (autoHardwareQuality) {
            viewDistanceChunks = std::min(viewDistanceChunks, adaptiveViewDistance);
            streamMarginChunks = std::min(streamMarginChunks, std::max(4, (int)std::round(12.0f * detailLevel)));
            streamDistanceChunks = std::min(streamDistanceCap, viewDistanceChunks + streamMarginChunks);
            uploadBudget = std::max(16, (int)std::round((float)uploadBudget * detailLevel));
            baseUploadBudget = uploadBudget;

            if (autoVegaAscension && hardware.IsVegaApuClass()) {
                if (!hasEnvViewDist) viewDistanceChunks = std::min(viewDistanceChunks, 14);
                if (!hasEnvStreamMargin) streamMarginChunks = std::min(streamMarginChunks, 8);
                if (!hasEnvUploadBudget) uploadBudget = std::min(160, std::max(96, uploadBudget));
                if (!hasEnvPreloadRadius) preloadRadiusChunks = std::min(preloadRadiusChunks, 8);
                if (!hasEnvPreloadMaxSec) preloadMaxSec = std::min(preloadMaxSec, 18.0f);
                streamDistanceChunks = std::min(streamDistanceCap, viewDistanceChunks + streamMarginChunks);
                baseUploadBudget = uploadBudget;
                std::cout << "VEGA ASCENSION auto-profile: ON (stability + fast input)" << std::endl;
            }
        }

        if (hardSafeMode) {
            qualityLevel = 0;
            viewDistanceChunks = std::min(viewDistanceChunks, 4);
            streamMarginChunks = std::min(streamMarginChunks, 2);
            streamDistanceChunks = std::min(streamDistanceCap, viewDistanceChunks + streamMarginChunks);
            uploadBudget = std::max(uploadBudget, 48);
            baseUploadBudget = uploadBudget;
            preloadRadiusChunks = 0;
            preloadMaxSec = std::min(preloadMaxSec, 4.0f);
            preloadBlocksInput = false;
            disableUnload = false;
            std::cout << "SAFE MODE: ON (reduced preload/view + conservative runtime)" << std::endl;
        }

        const float walkSpeed = GetEnvFloatClamped("HVE_WALK_SPEED", 8.0f, 1.0f, 50.0f);
        const float walkEyeHeight = GetEnvFloatClamped("HVE_WALK_EYE_HEIGHT", 2.2f, 1.5f, 4.0f);
        const float walkAccel = GetEnvFloatClamped("HVE_WALK_ACCEL", 12.0f, 2.0f, 60.0f);
        const float walkDrag = GetEnvFloatClamped("HVE_WALK_DRAG", 14.0f, 1.0f, 60.0f);
        const float walkMaxSpeedMult = GetEnvFloatClamped("HVE_WALK_MAX_MULT", 1.3f, 1.0f, 3.0f);

        float mouseSens = GetEnvOrIniFloatClamped("HVE_MOUSE_SENS", ini, "mouse_sens", 0.14f, 0.01f, 2.0f);
        const float mouseDeltaMax = GetEnvFloatClamped("HVE_MOUSE_DELTA_MAX", 280.0f, 8.0f, 5000.0f);
        const float maxCameraStep = GetEnvFloatClamped("HVE_MAX_CAMERA_STEP", 12.0f, 4.0f, 5000.0f);
        bool invertY = GetEnvOrIniBool("HVE_INVERT_Y", ini, "invert_y", false);
        float fovDeg = GetEnvOrIniFloatClamped("HVE_FOV", ini, "fov", 45.0f, 30.0f, 100.0f);

        // Keybinds (controls)
        int keyForward = GetEnvOrIniIntClamped("HVE_KEY_FORWARD", ini, "key_forward", GLFW_KEY_W, 0, 512);
        int keyBack = GetEnvOrIniIntClamped("HVE_KEY_BACK", ini, "key_back", GLFW_KEY_S, 0, 512);
        int keyLeft = GetEnvOrIniIntClamped("HVE_KEY_LEFT", ini, "key_left", GLFW_KEY_A, 0, 512);
        int keyRight = GetEnvOrIniIntClamped("HVE_KEY_RIGHT", ini, "key_right", GLFW_KEY_D, 0, 512);
        int keyUp = GetEnvOrIniIntClamped("HVE_KEY_UP", ini, "key_up", GLFW_KEY_SPACE, 0, 512);
        int keyDown = GetEnvOrIniIntClamped("HVE_KEY_DOWN", ini, "key_down", GLFW_KEY_LEFT_SHIFT, 0, 512);
        int keyInventory = GetEnvOrIniIntClamped("HVE_KEY_INVENTORY", ini, "key_inventory", GLFW_KEY_E, 0, 512);
        int keyBrake = GetEnvOrIniIntClamped("HVE_KEY_BRAKE", ini, "key_brake", GLFW_KEY_X, 0, 512);
        int keyWireframe = GetEnvOrIniIntClamped("HVE_KEY_WIREFRAME", ini, "key_wireframe", GLFW_KEY_F3, 0, 512);
        int keyImportPagePrev = GetEnvOrIniIntClamped("HVE_KEY_IMPORT_PREV", ini, "key_import_prev", GLFW_KEY_PAGE_UP, 0, 512);
        int keyImportPageNext = GetEnvOrIniIntClamped("HVE_KEY_IMPORT_NEXT", ini, "key_import_next", GLFW_KEY_PAGE_DOWN, 0, 512);
        const bool forceWasd = GetEnvBool("HVE_FORCE_WASD", true);
        if (forceWasd) {
            keyForward = GLFW_KEY_W;
            keyBack = GLFW_KEY_S;
            keyLeft = GLFW_KEY_A;
            keyRight = GLFW_KEY_D;
            keyUp = GLFW_KEY_SPACE;
            keyDown = GLFW_KEY_LEFT_SHIFT;
        }
        const bool badMoveKeyMap =
            keyForward <= 0 || keyBack <= 0 || keyLeft <= 0 || keyRight <= 0 ||
            keyForward == keyBack || keyForward == keyLeft || keyForward == keyRight ||
            keyBack == keyLeft || keyBack == keyRight || keyLeft == keyRight;
        if (badMoveKeyMap) {
            keyForward = GLFW_KEY_W;
            keyBack = GLFW_KEY_S;
            keyLeft = GLFW_KEY_A;
            keyRight = GLFW_KEY_D;
            keyUp = GLFW_KEY_SPACE;
            keyDown = GLFW_KEY_LEFT_SHIFT;
            std::cout << "Input fallback: invalid movement keymap detected, restoring WASD defaults" << std::endl;
        }
        const bool startWithMouseCaptured = GetEnvBool("HVE_MOUSE_CAPTURED", true);
        const float inputGraceSec = GetEnvFloatClamped("HVE_INPUT_GRACE_SEC", 0.0f, 0.0f, 2.0f);
        const bool loadingInputBreakout = GetEnvBool("HVE_LOADING_INPUT_BREAKOUT", true);
        const float loadingOverlayMaxSec = GetEnvFloatClamped("HVE_LOADING_OVERLAY_MAX_SEC", 2.5f, 0.0f, 120.0f);
        const bool startupForcePreload = GetEnvBool("HVE_STARTUP_FORCE_PRELOAD", false);
        const bool startupInputHardUnlock = GetEnvBool("HVE_STARTUP_INPUT_HARD_UNLOCK", true);
        const float startupInputUnlockSec = GetEnvFloatClamped("HVE_STARTUP_INPUT_UNLOCK_SEC", 1.5f, 0.0f, 30.0f);
        const int startupBlockMinChunks = GetEnvIntClamped("HVE_STARTUP_BLOCK_MIN_CHUNKS", 512, 0, 500000);
        const float startupBlockMaxSec = GetEnvFloatClamped("HVE_STARTUP_BLOCK_MAX_SEC", 60.0f, 0.0f, 300.0f);
        const float startupBlockStallSec = GetEnvFloatClamped("HVE_STARTUP_BLOCK_STALL_SEC", 10.0f, 0.5f, 60.0f);
        const float streamLookAheadSec = GetEnvFloatClamped("HVE_STREAM_LOOKAHEAD_SEC", 1.10f, 0.0f, 4.0f);
        const int streamLookAheadMaxChunks = GetEnvIntClamped("HVE_STREAM_LOOKAHEAD_MAX_CHUNKS", 20, 0, 128);
        const float streamLookAheadSpeedMult = GetEnvFloatClamped("HVE_STREAM_LOOKAHEAD_SPEED_MULT", 0.55f, 0.1f, 2.5f);
        const int streamFlightStepsBonus = GetEnvIntClamped("HVE_STREAM_FLIGHT_STEPS_BONUS", 2, 0, 8);
        const int streamFlightUploadBoost = GetEnvIntClamped("HVE_STREAM_FLIGHT_UPLOAD_BOOST", 96, 0, 2048);
        const int streamInFlightSoftCap = GetEnvIntClamped("HVE_STREAM_INFLIGHT_SOFTCAP", 1200, 64, 200000);
        const float starvationRescueHoldSec = GetEnvFloatClamped("HVE_STARVATION_RESCUE_HOLD_SEC", 1.4f, 0.0f, 10.0f);
        const int starvationRescueMinStream = GetEnvIntClamped("HVE_STARVATION_RESCUE_MIN_STREAM", 24, 8, streamDistanceCap);
        const int starvationRescueMinHeight = GetEnvIntClamped("HVE_STARVATION_RESCUE_MIN_HEIGHT", 24, 8, 256);
        const int starvationRescueUploadBoost = GetEnvIntClamped("HVE_STARVATION_RESCUE_UPLOAD_BOOST", 192, 0, 4096);
        const int starvationExtraPumpPasses = GetEnvIntClamped("HVE_STARVATION_EXTRA_PUMP_PASSES", 2, 0, 6);
        const int starvationExtraPumpBudget = GetEnvIntClamped("HVE_STARVATION_EXTRA_PUMP_BUDGET", 96, 1, 4096);
        const float startupPerfProtectSec = GetEnvFloatClamped("HVE_STARTUP_PERF_PROTECT_SEC", 15.0f, 0.0f, 60.0f);
        const int startupProtectStream = GetEnvIntClamped("HVE_STARTUP_STREAM", 64, 6, 128);
        const int startupProtectUploadMin = GetEnvIntClamped("HVE_STARTUP_UPLOAD_MIN", 128, 4, 512);
        const int startupProtectHeight = GetEnvIntClamped("HVE_STARTUP_HEIGHT", 64, 4, 128);
        const int startupWarmChunksTarget = GetEnvIntClamped("HVE_STARTUP_WARM_CHUNKS", 1024, 24, 200000);
        const int startupMinStreamSteps = GetEnvIntClamped("HVE_STARTUP_STREAM_STEPS", 16, 1, 32);
        const bool chunkCatchupEnabled = GetEnvBool("HVE_CHUNK_CATCHUP_ENABLED", true);
        const int chunkCatchupTarget = GetEnvIntClamped("HVE_CHUNK_CATCHUP_TARGET", 4096, 24, 200000);
        const int chunkCatchupUploadBoost = GetEnvIntClamped("HVE_CHUNK_CATCHUP_UPLOAD_BOOST", 256, 0, 1024);
        const int chunkCatchupStreamBonus = GetEnvIntClamped("HVE_CHUNK_CATCHUP_STREAM_BONUS", 128, 0, 512);
        const int chunkCatchupStepsBonus = GetEnvIntClamped("HVE_CHUNK_CATCHUP_STEPS_BONUS", 8, 0, 16);
        const int streamStepsWhileMoving = GetEnvIntClamped("HVE_STREAM_STEPS_WHILE_MOVING", 16, 1, 32);
        const int chunkCatchupDeficitSoft = GetEnvIntClamped("HVE_CHUNK_CATCHUP_DEFICIT_SOFT", 4096, 1, 500000);
        const int chunkCatchupStreamBurstMax = GetEnvIntClamped("HVE_CHUNK_CATCHUP_STREAM_BURST_MAX", 256, 1, 512);
        const int chunkCatchupUploadBurstMax = GetEnvIntClamped("HVE_CHUNK_CATCHUP_UPLOAD_BURST_MAX", 4096, 8, 8192);
        const float chunkCatchupMinFps = GetEnvFloatClamped("HVE_CHUNK_CATCHUP_MIN_FPS", 45.0f, 20.0f, 240.0f);
        const float chunkCatchupMaxFrameMs = GetEnvFloatClamped("HVE_CHUNK_CATCHUP_MAX_FRAME_MS", 22.0f, 5.0f, 80.0f);
        const int uploadPumpCap = GetEnvIntClamped("HVE_UPLOAD_PUMP_CAP", 2048, 16, 4096);

        const float flySpeed = GetEnvFloatClamped("HVE_FLY_SPEED", 25.0f, 1.0f, 500.0f);
        const float flyBoostMult = GetEnvFloatClamped("HVE_FLY_BOOST_MULT", 4.0f, 1.0f, 20.0f);
        const bool flyBoostEnabled = GetEnvBool("HVE_FLY_BOOST", true);
        const float flyAccel = GetEnvFloatClamped("HVE_FLY_ACCEL", 6.5f, 1.0f, 40.0f);
        const float flyDrag = GetEnvFloatClamped("HVE_FLY_DRAG", 2.6f, 0.5f, 20.0f);
        const float flyMaxSpeedMult = GetEnvFloatClamped("HVE_FLY_MAX_MULT", 1.8f, 1.0f, 4.0f);
        const float flyPrecisionMult = GetEnvFloatClamped("HVE_FLY_PRECISION_MULT", 0.35f, 0.1f, 1.0f);
        const float flyPrecisionDrag = GetEnvFloatClamped("HVE_FLY_PRECISION_DRAG", 6.5f, 0.5f, 40.0f);
        const float reachDist = GetEnvFloatClamped("HVE_REACH", 16.0f, 2.0f, 20.0f);
        const float placeRate = GetEnvFloatClamped("HVE_PLACE_RATE", 60.0f, 1.0f, 240.0f);
        const float breakRate = GetEnvFloatClamped("HVE_BREAK_RATE", 9.0f, 1.0f, 60.0f);
        const float placeHoldDelay = GetEnvFloatClamped("HVE_PLACE_HOLD_DELAY", 0.0f, 0.0f, 0.8f);
        const float breakHoldDelay = GetEnvFloatClamped("HVE_BREAK_HOLD_DELAY", 0.18f, 0.05f, 0.8f);
        const bool fogStarvationRelief = GetEnvBool("HVE_FOG_STARVATION_RELIEF", true);
        const float fogStarvationMinScale = GetEnvFloatClamped("HVE_FOG_STARVATION_MIN_SCALE", 0.58f, 0.05f, 1.0f);
        const bool fogHighAltitudeRelief = GetEnvBool("HVE_FOG_HIGHALT_RELIEF", true);
        const float fogHighAltStart = GetEnvFloatClamped("HVE_FOG_HIGHALT_START", 130.0f, 16.0f, 4000.0f);
        const float fogHighAltEnd = GetEnvFloatClamped("HVE_FOG_HIGHALT_END", 360.0f, 24.0f, 5000.0f);
        const float fogHighAltMinScale = GetEnvFloatClamped("HVE_FOG_HIGHALT_MIN_SCALE", 0.68f, 0.10f, 1.0f);
        const float playerBuildHalfWidth = GetEnvFloatClamped("HVE_PLAYER_BUILD_HALF_WIDTH", 0.22f, 0.12f, 0.45f);
        const float playerBuildHeight = GetEnvFloatClamped("HVE_PLAYER_BUILD_HEIGHT", 1.65f, 1.2f, 2.4f);
        const int placeMax = GetEnvIntClamped("HVE_PLACE_MAX", 512, 1, 8192);
        const bool buildWatchEnabled = GetEnvBool("HVE_BUILD_WATCH", false);
        const float buildWatchSec = GetEnvFloatClamped("HVE_BUILD_WATCH_SEC", 0.75f, 0.1f, 10.0f);
        const int buildWatchMax = GetEnvIntClamped("HVE_BUILD_WATCH_MAX", 128, 1, 2048);
        camera.MovementSpeed = flySpeed;
        camera.AccelMult = flyAccel;
        camera.Drag = flyDrag;
        camera.MaxSpeedMult = flyMaxSpeedMult;

        const int traceCameraHz = GetEnvIntClamped("HVE_TRACE_CAMERA_HZ", 30, 0, 240);
        double lastCamTraceTime = -1.0;

        struct BuildWatchEntry {
            glm::ivec3 pos{0};
            uint8_t id = 0;
            double time = 0.0;
            std::uint64_t meshStampAtPlace = 0;
            bool warned = false;
        };

        auto worldToChunk = [](const glm::ivec3& world) {
            auto divFloor = [](int a, int b) {
                int q = a / b;
                int r = a % b;
                if ((r != 0) && ((r > 0) != (b > 0))) --q;
                return q;
            };
            return glm::ivec3(
                divFloor(world.x, Game::World::CHUNK_SIZE),
                divFloor(world.y, Game::World::CHUNK_SIZE),
                divFloor(world.z, Game::World::CHUNK_SIZE)
            );
        };

        std::vector<BuildWatchEntry> buildWatch;
        buildWatch.reserve((std::size_t)buildWatchMax);

        const bool helpOnStart = GetEnvBool("HVE_HELP_ON_START", false);
        if (helpOnStart) {
            PrintControls();
        }

        const bool clearBindlessBlacklist = GetEnvBool("HVE_BINDLESS_CLEAR_BLACKLIST", false);
        const bool forceBindless = GetEnvBool("HVE_BINDLESS_FORCE", false);
        const bool disableBindless = GetEnvBool("HVE_BINDLESS_DISABLE", false);
        if (clearBindlessBlacklist) {
            ClearBindlessBlacklistFile();
            std::cout << "Bindless textures: blacklist cleared" << std::endl;
        }

        // Optional bindless path (fallback to bound texture unit if unsupported)
        const std::string bindlessKey = GetBindlessBlacklistKey();
        const bool bindlessBlacklisted = IsBindlessBlacklisted(bindlessKey);
        const bool bindlessAllowed = !disableBindless && (forceBindless || !bindlessBlacklisted);
        if (disableBindless) {
            std::cout << "Bindless textures: DISABLED (env)" << std::endl;
        } else if (forceBindless) {
            std::cout << "Bindless textures: FORCE (ignoring blacklist)" << std::endl;
        } else if (!bindlessAllowed) {
            std::cout << "Bindless textures: BLACKLISTED (skipping)" << std::endl;
        }
        const bool bindlessOk = bindlessAllowed && Engine::RendererUtil::BindlessTextures::Init();
        if (bindlessAllowed) {
            std::cout << "Bindless textures: " << (bindlessOk ? "ENABLED" : "DISABLED") << std::endl;
        }

        // Setup Shader
        Shader shader("assets/shaders/chunk_vert.glsl",
                  bindlessOk ? "assets/shaders/chunk_frag_bindless.glsl"
                     : "assets/shaders/chunk_frag.glsl");

        bool erosionComputeEnabled = GetEnvBool("HVE_EROSION_COMPUTE", false);
        if (hardSafeMode && erosionComputeEnabled) {
            erosionComputeEnabled = false;
            std::cout << "SAFE MODE: erosion compute disabled" << std::endl;
        }
        std::unique_ptr<Shader> erosionComputeShader;
        if (erosionComputeEnabled) {
            erosionComputeShader = std::make_unique<Shader>("", "", "assets/shaders/erosion.comp");
            if (erosionComputeShader && erosionComputeShader->GetID() != 0) {
                std::cout << "Erosion compute shader initialized (assets/shaders/erosion.comp)" << std::endl;
            }
        }

        // High-end atlas loader (4 custom textures + quality filtering, atlas-compatible runtime path)
        GLuint blockTexAtlas = LoadHighEndTextures();
        const bool atlasMipmaps = GetEnvBool("HVE_ATLAS_MIPMAPS", true);

        bool bindlessActive = bindlessOk;
        std::uint64_t blockTexHandle = 0;
        GLuint blockSampler = 0;
        const GLenum blockTexUniformType = GetUniformType(shader.GetID(), "u_BlockAtlas");
        if (bindlessOk && IsArraySamplerUniformType(blockTexUniformType)) {
            bindlessActive = false;
            std::cout << "Bindless: u_BlockAtlas is an array sampler -> using bound texture" << std::endl;
        }

        if (bindlessOk && bindlessActive && !Is2DSamplerUniformType(blockTexUniformType)) {
            bindlessActive = false;
            std::cout << "Bindless: u_BlockAtlas is not sampler2D -> using bound texture" << std::endl;
        }

        if (bindlessOk && bindlessActive) {
            // Create an explicit sampler and prefer a texture+sampler handle (more reliable on some drivers)
            glGenSamplers(1, &blockSampler);
            glSamplerParameteri(blockSampler, GL_TEXTURE_MIN_FILTER, atlasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glSamplerParameteri(blockSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glSamplerParameteri(blockSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glSamplerParameteri(blockSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);

#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
            {
                const float requestedAniso = float(GetEnvIntClamped("HVE_ATLAS_ANISO", 12, 1, 16));
                GLfloat maxAniso = 0.0f;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
                if (maxAniso > 1.0f) {
                    glSamplerParameterf(blockSampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(requestedAniso, maxAniso));
                }
            }
#endif

            blockTexHandle = Engine::RendererUtil::BindlessTextures::GetTextureHandle(blockTexAtlas);

            if (blockTexHandle != 0) {
                Engine::RendererUtil::BindlessTextures::MakeTextureHandleResident(blockTexHandle);

                // Some drivers advertise bindless but fail to make handles resident.
                // Disable bindless before attempting to set the uniform to avoid GL errors.
                const bool resident = Engine::RendererUtil::BindlessTextures::IsTextureHandleResident(blockTexHandle);
                const GLenum residentErr = glGetError();
                if (!resident || residentErr != GL_NO_ERROR) {
                    bindlessActive = false;
                    std::cout << "Bindless handle not resident -> fallback to bound textures" << std::endl;
                }
            } else {
                bindlessActive = false;
            }
        }

        // If bindless is available, set the sampler handle once (uniform value persists).
        if (bindlessActive && blockTexHandle != 0) {
            shader.Use();
            GLint loc = glGetUniformLocation(shader.GetID(), "u_BlockAtlas");
            if (loc < 0) {
                bindlessActive = false;
                std::cout << "Bindless: u_BlockAtlas location invalid -> using bound texture" << std::endl;
            } else {
                const GLboolean hadDebugOutput = glIsEnabled(GL_DEBUG_OUTPUT);
                const GLboolean hadDebugSync = glIsEnabled(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                if (hadDebugOutput) glDisable(GL_DEBUG_OUTPUT);
                if (hadDebugSync) glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

                // Clear any prior errors so we only observe errors from this probe.
                while (glGetError() != GL_NO_ERROR) {}

                Engine::RendererUtil::BindlessTextures::SetUniformHandle(shader.GetID(), loc, blockTexHandle);

                GLenum err = glGetError();

                if (hadDebugOutput) glEnable(GL_DEBUG_OUTPUT);
                if (hadDebugSync) glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                if (err != GL_NO_ERROR) {
                    bindlessActive = false;
                    std::cout << "Bindless sampler rejected (glGetError=0x" << std::hex << err << std::dec
                              << ") -> fallback to bound textures" << std::endl;

                    // Persistently disable bindless for this driver so future runs don't even attempt the call.
                    AddBindlessBlacklist(bindlessKey);
                }
            }
        }

        const int erosionRegionBlocks = GetEnvIntClamped("HVE_EROSION_REGION", 512, 64, 2048);
        const int erosionSampleStep = GetEnvIntClamped("HVE_EROSION_STEP", 2, 1, 8);
        const int erosionPasses = GetEnvIntClamped("HVE_EROSION_PASSES", 8, 1, 32);
        const float erosionDispatchSec = GetEnvFloatClamped("HVE_EROSION_DISPATCH_SEC", 4.0f, 0.2f, 10.0f);
        const float erosionStrength = GetEnvFloatClamped("HVE_EROSION_STRENGTH", 0.65f, 0.0f, 2.0f);

        const int erosionGrid = std::max(16, erosionRegionBlocks / erosionSampleStep);
        const int erosionWorldSpan = erosionGrid * erosionSampleStep;

        GLuint erosionHeightTexA = 0;
        GLuint erosionHeightTexB = 0;
        GLuint erosionSedTexA = 0;
        GLuint erosionSedTexB = 0;
        GLuint erosionVegTex = 0;

        std::vector<float> erosionBase;
        std::vector<float> erosionOut;
        std::vector<float> erosionSed;
        std::vector<float> erosionVeg;
        std::vector<float> erosionDelta;
        erosionBase.resize((size_t)erosionGrid * (size_t)erosionGrid, 0.0f);
        erosionOut.resize((size_t)erosionGrid * (size_t)erosionGrid, 0.0f);
        erosionSed.resize((size_t)erosionGrid * (size_t)erosionGrid, 0.0f);
        erosionVeg.resize((size_t)erosionGrid * (size_t)erosionGrid, 0.12f);
        erosionDelta.resize((size_t)erosionGrid * (size_t)erosionGrid, 0.0f);

        if (erosionComputeEnabled && erosionComputeShader && erosionComputeShader->GetID() != 0) {
            glGenTextures(1, &erosionHeightTexA);
            glBindTexture(GL_TEXTURE_2D, erosionHeightTexA);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, erosionGrid, erosionGrid, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glGenTextures(1, &erosionHeightTexB);
            glBindTexture(GL_TEXTURE_2D, erosionHeightTexB);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, erosionGrid, erosionGrid, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glGenTextures(1, &erosionSedTexA);
            glBindTexture(GL_TEXTURE_2D, erosionSedTexA);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, erosionGrid, erosionGrid, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glGenTextures(1, &erosionSedTexB);
            glBindTexture(GL_TEXTURE_2D, erosionSedTexB);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, erosionGrid, erosionGrid, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glGenTextures(1, &erosionVegTex);
            glBindTexture(GL_TEXTURE_2D, erosionVegTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, erosionGrid, erosionGrid, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, erosionGrid, erosionGrid, GL_RED, GL_FLOAT, erosionVeg.data());

            glBindTexture(GL_TEXTURE_2D, 0);
        }

        typedef void (APIENTRY* PFNBindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
        typedef void (APIENTRY* PFNDispatchCompute)(GLuint, GLuint, GLuint);
        typedef void (APIENTRY* PFNMemoryBarrier)(GLbitfield);

        PFNBindImageTexture pBindImageTexture = reinterpret_cast<PFNBindImageTexture>(glfwGetProcAddress("glBindImageTexture"));
        PFNDispatchCompute pDispatchCompute = reinterpret_cast<PFNDispatchCompute>(glfwGetProcAddress("glDispatchCompute"));
        PFNMemoryBarrier pMemoryBarrier = reinterpret_cast<PFNMemoryBarrier>(glfwGetProcAddress("glMemoryBarrier"));

        const bool erosionComputeRuntimeReady = (pBindImageTexture != nullptr && pDispatchCompute != nullptr && pMemoryBarrier != nullptr);
        if (erosionComputeEnabled && !erosionComputeRuntimeReady) {
            std::cout << "Erosion compute disabled: GL 4.3 image/dispatch/barrier functions unavailable" << std::endl;
        }

        double lastErosionDispatch = -1.0;

        // Streaming world (multi-chunk) + renderer
        Game::World::ChunkManager chunkManager;

        const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        const std::size_t maxWorkers = std::max<std::size_t>(4, hw - 1); // Allow more workers on high-end CPUs
        const std::size_t workerCount = std::min<std::size_t>(maxWorkers, (hw > 1) ? (hw - 1) : 1);
        Threading::ThreadPool pool(workerCount);
        chunkManager.Init(&pool);

        Engine::WorldRenderer worldRenderer;
        
        // Matrix Setup
        int lastWindowWidth = Window::GetWidth();
        int lastWindowHeight = Window::GetHeight();
        if (lastWindowHeight < 1) lastWindowHeight = 1;
        const float farClip = GetEnvFloatClamped("HVE_FAR_CLIP", 6500.0f, 800.0f, 20000.0f);
        glm::mat4 projection = glm::perspective(glm::radians(fovDeg), (float)lastWindowWidth / (float)lastWindowHeight, 0.1f, farClip);

        double fpsAccumTime = 0.0;
        int fpsFrames = 0;
        double lastFpsUpdate = 0.0;
        GameTime gameTime(1.0 / 120.0, 0.1);
        TimeSlicedJobSystem timeSlicedJobs;
        const int maxFixedStepsPerFrame = GetEnvIntClamped("HVE_MAX_FIXED_STEPS", 6, 1, 32);
        const int recoveryFixedSteps = std::clamp(GetEnvIntClamped("HVE_RECOVERY_FIXED_STEPS", 2, 1, 32), 1, maxFixedStepsPerFrame);
        const int fixedStepsWhileMoving = std::clamp(GetEnvIntClamped("HVE_FIXED_STEPS_WHILE_MOVING", 2, 1, 8), 1, maxFixedStepsPerFrame);
        const int recoveryHoldFrames = GetEnvIntClamped("HVE_RECOVERY_HOLD_FRAMES", 120, 0, 2000);
        int fixedStepRecoveryFrames = 0;
        int fixedStepSaturatedFrames = 0;
        int fixedStepSaturatedFramesSec = 0;
        float stableFpsEma = 0.0f;
        float maxSpikeWindowMs = 0.0f;
        double lastEliteMetricUpdate = -1.0;
        double lastTerrainMetricSample = -1.0;
        Game::World::TerrainMetrics terrainMetrics{};
        Game::World::TerrainMetrics terrainMetricsPending{};
        bool terrainMetricJobQueued = false;
        bool terrainMetricReady = false;
        float estimatedCpuRamPeakGB = 0.0f;
        const float streamTps = GetEnvFloatClamped("HVE_STREAM_TPS", 20.0f, 2.0f, 120.0f);
        const double streamTickSec = 1.0 / (double)streamTps;

        const bool startMenuEnabled = GetEnvOrIniBool("HVE_START_MENU", ini, "start_menu", false);
        bool menuOpen = startMenuEnabled;
        enum class MenuScreen { Main = 0, Load = 1, Settings = 2, Controls = 3, Loading = 4, Amulet = 5, Terrain = 6 };
        MenuScreen menuScreen = MenuScreen::Main;

        MenuScreen lastMenuScreen = menuScreen;
        bool lastMenuLmb = false;
        bool lastMenuUp = false;
        bool lastMenuDown = false;
        bool lastMenuLeft = false;
        bool lastMenuRight = false;
        bool lastMenuEnter = false;
        std::vector<std::string> worldList;
        int selectedWorldIndex = 0;
        std::string menuStatusText;
        std::string amuletStatusText;
        std::atomic<int> amuletReturnState{0}; // 0=waiting, 1=save complete, 2=failed
        bool amuletBridgeActive = false;
        double amuletStartTime = -1.0;
        bool amuletPayloadSent = false;
        bool amuletSaveDetected = false;
        bool amuletReloadQueued = false;
        bool amuletReloadDone = false;
        std::vector<std::string> amuletLog;

        bool rebindActive = false;
        int rebindAction = -1;
        double rebindStartTime = -1.0;
        std::vector<unsigned char> rebindPrev;

        int menuMainIndex = 0;
        int menuLoadIndex = 0;
        int menuSettingsIndex = 0;
        int menuControlsIndex = 0;
        int menuTerrainIndex = 0;
        int selectedTerrain = std::clamp(Game::World::g_CurrentPresetIndex, 0, 7);

        bool worldStarted = !startMenuEnabled;
        bool loadingWorld = false;
        bool showLoadingOverlay = false;
        bool showLoading = true;
        float loadingProgress = 0.0f;
        std::string loadingStatus = "AETHERFORGE wird initialisiert...";

        bool mouseCaptured = (!menuOpen) && startWithMouseCaptured;
        bool lastMouseCapturedState = mouseCaptured;
        double captureGuardUntil = -1.0;
        bool lastTab = false;
        bool lastLmb = false;
        bool lastRmb = false;
        bool lastMmb = false;
        bool lastEsc = false;
        bool lastE = false;
        bool lastG = false;
        bool inventoryOpen = false;
        bool inventoryRestoreCapture = false;
        bool lastInvLmb = false;
        bool lastInvRmb = false;

        bool precisionBuild = false;
        bool precisionAnchor = false;
        glm::ivec3 precisionAnchorPos{0};
        glm::ivec3 precisionAnchorNormal{0};

        enum class BuildMode {
            Plane = 0,
            Line = 1,
            Box = 2
        };

        enum class EditorMode {
            Fill = 0,
            Replace = 1,
            Hollow = 2,
            Remove = 3
        };

        enum class EditorDragMode {
            Box = 0,
            Face = 1,
            Column = 2
        };

        BuildMode buildMode = BuildMode::Plane;
        bool mirrorBuild = false;
        int mirrorAxis = 0; // 0=X, 1=Z, 2=Y
        glm::ivec3 mirrorOrigin{0};
        bool planeLock = false;
        int planeLockY = 0;

        bool editorEnabled = false;
        bool editorStartValid = false;
        bool editorEndValid = false;
        glm::ivec3 editorStart{0};
        glm::ivec3 editorEnd{0};
        bool editorDragActive = false;
        glm::ivec3 editorDragNormal{0};
        uint8_t editorTargetId = 1;
        EditorMode editorMode = EditorMode::Fill;
        EditorDragMode editorDragMode = EditorDragMode::Box;
        EditorDragMode editorDragModeUsed = EditorDragMode::Box;

        struct BlockChange {
            glm::ivec3 pos{0};
            uint8_t before = 0;
            uint8_t after = 0;
        };

        struct BuildAction {
            std::vector<BlockChange> changes;
        };

        struct EditorBatch {
            bool active = false;
            EditorMode mode = EditorMode::Fill;
            glm::ivec3 min{0};
            glm::ivec3 max{0};
            glm::ivec3 cursor{0};
            uint8_t fillId = 1;
            uint8_t targetId = 1;
            std::uint64_t visited = 0;
            std::uint64_t total = 0;
            BuildAction action;
        };

        std::vector<BuildAction> undoStack;
        std::vector<BuildAction> redoStack;
        const int undoMax = GetEnvIntClamped("HVE_UNDO_MAX", 100, 1, 10000);
        const int editorBatchPerFrame = GetEnvIntClamped("HVE_EDITOR_BATCH", 4096, 128, 65536);
        EditorBatch editorBatch;

        enum class VisualTool {
            None = 0,
            Selection = 1,
            Pattern = 2,
            Fill = 3,
            Copy = 4,
            Cut = 5,
            Paste = 6
        };

        enum class SelectionState {
            Idle = 0,
            Selecting = 1,
            Locked = 2
        };

        struct ClipboardVoxel {
            glm::ivec3 offset{0};
            uint8_t id = 0;
        };

        const bool visualBuildEnabled = false;
        const bool amuletEnabled = false;
        VisualTool visualTool = VisualTool::Selection;
        SelectionState selectionState = SelectionState::Idle;
        glm::ivec3 selectionPos1{0};
        glm::ivec3 selectionPos2{0};
        glm::ivec3 visualSelMin{0};
        glm::ivec3 visualSelMax{0};
        bool visualSelValid = false;

        g_MinecraftImportPage = ClampMinecraftImportPage(
            GetEnvOrIniIntClamped("HVE_IMPORT_PAGE", ini, "import_page", 0, 0, 4096)
        );
        int runtimeMaxBlockId = GetRuntimeMaxBlockId();
        uint8_t selectedBlockId = (uint8_t)GetEnvIntClamped("HVE_BLOCK", 1, 1, runtimeMaxBlockId);
        uint8_t patternBlockId = selectedBlockId;
        std::vector<ClipboardVoxel> clipboard;
        glm::ivec3 clipboardMin{0};
        glm::ivec3 clipboardMax{0};
        bool clipboardValid = false;
        glm::ivec3 pasteAnchor{0};
        bool pasteAnchorValid = false;
        const int clipboardMaxBlocks = GetEnvIntClamped("HVE_CLIP_MAX", 200000, 1024, 2000000);
        const int clipboardPreviewMax = GetEnvIntClamped("HVE_CLIP_PREVIEW_MAX", 2048, 128, 65536);

        bool showStatsInTitle = true;
        bool perfHudEnabled = GetEnvBool("HVE_PERF_HUD", false);
        const float eliteTargetFps = GetEnvFloatClamped("HVE_TARGET_FPS", 144.0f, 30.0f, 240.0f);
        const float eliteFrameBudgetMs = 1000.0f / eliteTargetFps;
        const float eliteMetricsHz = GetEnvFloatClamped("HVE_ELITE_HZ", 5.0f, 1.0f, 30.0f);
        bool wireframe = GetEnvOrIniBool("HVE_WIREFRAME", ini, "wireframe", false);
        bool vsync = GetEnvOrIniBool("HVE_VSYNC", ini, "vsync", false);
        bool dayNightCycle = GetEnvOrIniBool("HVE_DAY_NIGHT", ini, "day_night_cycle", false);
        bool crosshairEnabled = GetEnvBool("HVE_CROSSHAIR", true);
        bool hotbarEnabled = GetEnvBool("HVE_HOTBAR", true);
        const bool futuristicUi = true;
        const bool simpleBuild = GetEnvBool("HVE_SIMPLE_BUILD", true);
        const bool strictMouseCapture = GetEnvBool("HVE_STRICT_MOUSE_CAPTURE", true);
        const bool traceRuntime = GetEnvBool("HVE_TRACE_RUNTIME", false);
        const bool traceInput = GetEnvBool("HVE_TRACE_INPUT", false);
        const bool spikeGuardEnabled = GetEnvBool("HVE_SPIKE_GUARD", true);
        const bool spikeLogEnabled = GetEnvBool("HVE_SPIKE_LOG", true);
        const bool coverageMetricEnabled = GetEnvBool("HVE_COVERAGE_METRIC", true);
        const int coverageRadiusChunks = GetEnvIntClamped("HVE_COVERAGE_RADIUS", 20, 2, 128);
        const int coverageStrideChunks = GetEnvIntClamped("HVE_COVERAGE_STRIDE", 2, 1, 8);
        const bool coverageAlarmEnabled = GetEnvBool("HVE_COVERAGE_ALARM", true);
        const int coverageAlarmHigh = GetEnvIntClamped("HVE_COVERAGE_ALARM_HIGH", 28, 1, 50000);
        const int coverageAlarmLow = GetEnvIntClamped("HVE_COVERAGE_ALARM_LOW", 8, 0, 50000);
        const int coverageAlarmBoost = GetEnvIntClamped("HVE_COVERAGE_ALARM_BOOST", 18, 1, 128);
        const float coverageAlarmHoldSec = GetEnvFloatClamped("HVE_COVERAGE_ALARM_HOLD_SEC", 1.2f, 0.1f, 20.0f);
        const float coverageAlarmRampPerSec = GetEnvFloatClamped("HVE_COVERAGE_ALARM_RAMP", 10.0f, 0.1f, 120.0f);
        const float coverageAlarmDecayPerSec = GetEnvFloatClamped("HVE_COVERAGE_ALARM_DECAY", 6.0f, 0.1f, 120.0f);
        const bool stutterAlertsEnabled = GetEnvBool("HVE_STUTTER_ALERTS", true);
        const float stutterAlertMinFps = GetEnvFloatClamped("HVE_ALERT_MIN_FPS", 45.0f, 10.0f, 240.0f);
        const int stutterAlertFixedSatSec = GetEnvIntClamped("HVE_ALERT_FIXEDSAT_SEC", 1, 0, 120);
        const int fixedSatDebtSteps = GetEnvIntClamped("HVE_FIXEDSAT_DEBT_STEPS", 2, 1, 8);
        const int stutterAlertPendingOldTicks = GetEnvIntClamped("HVE_ALERT_PENDING_OLD_TICKS", 90, 1, 200000);
        const int stutterAlertOverdueChunks = GetEnvIntClamped("HVE_ALERT_OVERDUE_CHUNKS", 8, 1, 100000);
        const int stutterAlertInFlightGen = GetEnvIntClamped("HVE_ALERT_INFLIGHT_GEN", 28, 1, 100000);
        const int stutterAlertLatencyWarn = GetEnvIntClamped("HVE_ALERT_LATENCY_WARN", 35, 1, 100);
        const int stutterAlertFixedSatMinLatency = GetEnvIntClamped("HVE_ALERT_FIXEDSAT_MIN_LATENCY", 28, 1, 100);
        const float stutterAlertCooldownSec = GetEnvFloatClamped("HVE_ALERT_COOLDOWN_SEC", 0.8f, 0.1f, 30.0f);
        const bool controlObsEnabled = GetEnvBool("HVE_CONTROL_OBS", true);
        const bool controlMitigationEnabled = GetEnvBool("HVE_CONTROL_MITIGATION", false);
        const float controlResponseThreshold = GetEnvFloatClamped("HVE_CONTROL_RESPONSE_THRESHOLD", 0.62f, 0.10f, 1.50f);
        const float controlLagWarnMs = GetEnvFloatClamped("HVE_CONTROL_LAG_WARN_MS", 28.0f, 4.0f, 300.0f);
        const float controlDelayIntentMinSec = GetEnvFloatClamped("HVE_CONTROL_DELAY_INTENT_MIN_SEC", 0.10f, 0.0f, 2.0f);
        const int controlDelayReportMinMs = GetEnvIntClamped("HVE_CONTROL_DELAY_REPORT_MIN_MS", 120, 0, 4000);
        const bool fastInputPriorityEnabled = GetEnvBool("HVE_FAST_INPUT_PRIORITY", true);
        const float fastInputTimesliceFactor = GetEnvFloatClamped("HVE_FAST_INPUT_TIMESLICE_FACTOR", 0.08f, 0.00f, 0.50f);
        const float fastInputHoldSec = GetEnvFloatClamped("HVE_FAST_INPUT_HOLD_SEC", 0.45f, 0.05f, 4.0f);
        const float fastInputIntentThreshold = GetEnvFloatClamped("HVE_FAST_INPUT_INTENT_THRESHOLD", 0.18f, 0.01f, 1.0f);
        const float fastInputLagWarnMs = GetEnvFloatClamped("HVE_FAST_INPUT_LAG_WARN_MS", 22.0f, 4.0f, 300.0f);
        const float fastInputResponseMin = GetEnvFloatClamped("HVE_FAST_INPUT_RESPONSE_MIN", 0.78f, 0.10f, 1.20f);
        const int fastInputMaxStreamSteps = GetEnvIntClamped("HVE_FAST_INPUT_MAX_STREAM_STEPS", 2, 1, 8);
        const int fastInputStreamClamp = GetEnvIntClamped("HVE_FAST_INPUT_STREAM_CLAMP", 18, 8, streamDistanceCap);
        const int fastInputUploadClamp = GetEnvIntClamped("HVE_FAST_INPUT_UPLOAD_CLAMP", 96, 8, uploadPumpCap);
        const float farfieldSuppressHoldSec = GetEnvFloatClamped("HVE_FARFIELD_SUPPRESS_HOLD_SEC", 1.15f, 0.05f, 8.0f);
        const float spikeMinorMs = GetEnvFloatClamped("HVE_SPIKE_MINOR_MS", 2.2f, 0.4f, 25.0f);
        const float spikeMajorMs = GetEnvFloatClamped("HVE_SPIKE_MAJOR_MS", 7.5f, 1.0f, 80.0f);
        const float spikeZScore = GetEnvFloatClamped("HVE_SPIKE_Z", 2.4f, 0.8f, 8.0f);
        const float spikeHoldSec = GetEnvFloatClamped("HVE_SPIKE_HOLD_SEC", 3.0f, 0.2f, 20.0f);
        const float spikeRecoverSec = GetEnvFloatClamped("HVE_SPIKE_RECOVER_SEC", 5.5f, 0.5f, 40.0f);
        double traceLastSnapshot = -1.0;
        std::array<float, 240> spikeWindow{};
        int spikeWindowCount = 0;
        int spikeWindowHead = 0;
        int spikeMinorCount = 0;
        int spikeMajorCount = 0;
        int spikeLevel = 0; // 0..3
        double spikeStateUntil = -1.0;
        int coverageMisses = 0;
        bool coverageMetricLive = false;
        int coverageBoostApplied = 0;
        bool coverageAlarmActive = false;
        float coverageBoostState = 0.0f;
        double coverageAlarmHoldUntil = -1.0;
        double lastSpikeEventTime = -1.0;
        float spikeLastOverBaselineMs = 0.0f;
        float lowEndEmaFastMs = lowEndTargetMs;
        float lowEndEmaSlowMs = lowEndTargetMs;
        int lowEndLevel = 0;
        double lowEndLastDownshift = -1.0;
        double lowEndLastUpshift = -1.0;
        int lowEndDownshiftEvents = 0;
        int lowEndUpshiftEvents = 0;
        int lowEndDownshiftEventsSec = 0;
        int lowEndUpshiftEventsSec = 0;
        std::string lowEndLastReason = "init";
        int stutterLatencyScore = 0;
        bool stutterAlertActive = false;
        std::string stutterAlertCause = "none";
        float controlMoveResponseEma = 1.0f;
        float controlLagMsEma = 0.0f;
        float controlRotDegPerSecEma = 0.0f;
        float controlIntentEma = 0.0f;
        float controlHoldForwardSec = 0.0f;
        float controlHoldBackSec = 0.0f;
        float controlHoldLeftSec = 0.0f;
        float controlHoldRightSec = 0.0f;
        float controlHoldLmbSec = 0.0f;
        float controlHoldRmbSec = 0.0f;
        float controlIntentActiveSec = 0.0f;
        bool controlAwaitMove = false;
        double controlAwaitMoveStart = -1.0;
        int controlMoveDelayMsLast = 0;
        int controlMoveDelayMsMaxSec = 0;
        int controlPlaceOpsCounter = 0;
        int controlBreakOpsCounter = 0;
        int controlQualityScoreSec = 100;
        int controlLagMsSec = 0;
        int controlMoveDelayMsSec = 0;
        int controlPlaceOpsSec = 0;
        int controlBreakOpsSec = 0;
        float controlDistForwardMetersCounter = 0.0f;
        float controlDistBackMetersCounter = 0.0f;
        float controlDistLeftMetersCounter = 0.0f;
        float controlDistRightMetersCounter = 0.0f;
        float controlDistForwardMetersSec = 0.0f;
        float controlDistBackMetersSec = 0.0f;
        float controlDistLeftMetersSec = 0.0f;
        float controlDistRightMetersSec = 0.0f;
        float controlRotDegPerSecSec = 0.0f;
        float controlWASDHoldSec = 0.0f;
        float controlLmbHoldSec = 0.0f;
        float controlRmbHoldSec = 0.0f;
        float lastFrameYaw = camera.Yaw;
        float lastFramePitch = camera.Pitch;
        double fastInputPriorityUntil = -1.0;
        std::size_t streamPendingNotReady = 0;
        std::size_t streamWatchdogEligible = 0;
        std::size_t streamWatchdogOverdue = 0;
        int streamPendingOldestTicks = 0;
        int streamOverdueOldestTicks = 0;
        float impostorQueuePressureLive = 0.0f;
        float impostorBlendNearStartLive = 0.0f;
        float impostorBlendNearEndLive = 0.0f;
        float impostorQueuePushMulLive = 1.0f;
        float impostorQueuePressureAvgLive = 0.0f;
        float impostorQueuePressureAccum = 0.0f;
        int impostorQueuePressureSamples = 0;
        double impostorAutoTuneLast = -1.0;
        double stutterAlertCooldownUntil = -1.0;

        bool lastF1 = false;
        bool lastF2 = false;
        bool lastF3 = false;
        bool lastF4 = false;
        bool lastF5 = false;
        bool lastF6 = false;
        bool lastF7 = false;
        bool lastF8 = false;

        bool lastF9 = false;
        bool lastF10 = false;
        bool lastF11 = false;

        bool last1 = false;
        bool last2 = false;
        bool last3 = false;
        bool last4 = false;
        bool last5 = false;
        bool last6 = false;
        bool last7 = false;
        bool last8 = false;
        bool last9 = false;
        bool last0 = false;
        bool lastMinus = false;
        bool lastEqual = false;
        bool lastImportPrev = false;
        bool lastImportNext = false;
        bool lastSpace = false;
        static std::vector<ItemStack> hotbarSlots(12);
        static std::vector<ItemStack> mainInventorySlots(27);
        static ItemStack mouseCursorStack{};
        static int hotbarIndex = 0;
        static bool hotbarInit = false;
        if (!hotbarInit) {
            for (int i = 0; i < 12; ++i) {
                hotbarSlots[(size_t)i] = ItemStack{ (uint16_t)(i + 1), kItemStackMaxCount };
            }
            for (int i = 0; i < 27; ++i) {
                if (i < 9) {
                    mainInventorySlots[(size_t)i] = ItemStack{ (uint16_t)(i + 20), kItemStackMaxCount };
                } else {
                    mainInventorySlots[(size_t)i] = ItemStack{};
                }
            }
            hotbarIndex = std::clamp((int)selectedBlockId - 1, 0, 11);
            hotbarSlots[(size_t)hotbarIndex] = ItemStack{ selectedBlockId, kItemStackMaxCount };
            hotbarInit = true;
        }

        auto getHotbarBlockId = [&](int idx) -> uint8_t {
            if (idx < 0 || idx >= (int)hotbarSlots.size()) return selectedBlockId;
            const ItemStack& slot = hotbarSlots[(size_t)idx];
            if (IsStackEmpty(slot)) return selectedBlockId;
            return (uint8_t)slot.id;
        };

        auto setHotbarStack = [&](int idx, uint16_t id, uint8_t count) {
            if (idx < 0 || idx >= (int)hotbarSlots.size()) return;
            hotbarSlots[(size_t)idx] = ItemStack{ id, count };
            NormalizeStack(hotbarSlots[(size_t)idx]);
        };

        bool walkMode = false;
        double spawnNoClipUntil = -1.0;
        double lastSpaceTapTime = -1.0;
        double lastWalkSampleTime = -1.0;
        int walkSurfaceY = 0;
        const double walkSampleInterval = (double)GetEnvFloatClamped("HVE_WALK_SAMPLE_SEC", 0.08f, 0.02f, 0.30f);
        const float walkSnapLerp = GetEnvFloatClamped("HVE_WALK_SNAP_LERP", 16.0f, 2.0f, 40.0f);
        const float walkBodyHeight = GetEnvFloatClamped("HVE_WALK_BODY_HEIGHT", 1.8f, 1.5f, 3.0f);
        const float walkStepUp = GetEnvFloatClamped("HVE_WALK_STEP_UP", 0.60f, 0.0f, 1.25f);
        const float walkCollisionRadius = GetEnvFloatClamped("HVE_WALK_COLLISION_RADIUS", 0.28f, 0.10f, 0.50f);
        const float walkCollisionSkin = GetEnvFloatClamped("HVE_WALK_COLLISION_SKIN", 0.03f, 0.0f, 0.08f);
        const float walkJumpSpeed = GetEnvFloatClamped("HVE_WALK_JUMP_SPEED", 6.3f, 1.0f, 20.0f);
        const float walkJumpStepUp = std::max(walkStepUp, GetEnvFloatClamped("HVE_WALK_JUMP_STEP_UP", 1.05f, 0.0f, 1.25f));
        const float walkStepDownAssist = GetEnvFloatClamped("HVE_WALK_STEP_DOWN_ASSIST", 1.10f, 0.0f, 2.0f);
        const float walkGravity = GetEnvFloatClamped("HVE_WALK_GRAVITY", 22.0f, 5.0f, 80.0f);
        const float walkGroundProbe = GetEnvFloatClamped("HVE_WALK_GROUND_PROBE", 0.08f, 0.01f, 0.40f);
        const float walkDownLerp = GetEnvFloatClamped("HVE_WALK_DOWN_LERP", 8.5f, 1.0f, 40.0f);
        const double walkCoyoteSec = (double)GetEnvFloatClamped("HVE_WALK_COYOTE_SEC", 0.12f, 0.0f, 0.40f);
        const double walkJumpBufferSec = (double)GetEnvFloatClamped("HVE_WALK_JUMP_BUFFER_SEC", 0.12f, 0.0f, 0.40f);
        float walkVerticalVel = 0.0f;
        bool walkGrounded = false;
        double lastGroundedTime = -1.0;
        double jumpBufferedUntil = -1.0;

        Input::SetCursorMode(mouseCaptured);
        glfwSwapInterval(vsync ? 1 : 0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Crosshair overlay resources (created only if enabled)
        GLuint crossProg = 0;
        GLuint crossVao = 0;
        GLuint crossVbo = 0;
        const float crossOuterWidth = GetEnvFloatClamped("HVE_CROSSHAIR_OUTER_WIDTH", 4.0f, 1.0f, 8.0f);
        const float crossInnerWidth = GetEnvFloatClamped("HVE_CROSSHAIR_INNER_WIDTH", 2.2f, 1.0f, 6.0f);
        const float crossAlpha = GetEnvFloatClamped("HVE_CROSSHAIR_ALPHA", 0.96f, 0.2f, 1.0f);
        if (crosshairEnabled) {
            // =========================================================================
            // INVENTION: AAA Procedural Sci-Fi Crosshair (No-VBO, Fragment Driven)
            // Replaces legacy jagged GL_LINES with a perfectly smooth, animated 
            // circular UI element using implicit circle formulas.
            // =========================================================================
            const char* vsSrc = R"GLSL(
                #version 460 core
                uniform float u_Aspect;
                out vec2 vUV;
                void main(){ 
                    float size = 0.020;
                    vec2 pos[4] = vec2[](
                        vec2(-1,-1), vec2(1,-1),
                        vec2(-1, 1), vec2(1, 1)
                    );
                    int idx[6] = int[](0,1,2, 2,1,3);
                    vec2 p = pos[idx[gl_VertexID]];
                    vUV = p;
                    gl_Position = vec4(p.x * size, p.y * size * u_Aspect, 0.0, 1.0);
                }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                in vec2 vUV;
                uniform float u_Time;
                out vec4 FragColor;
                void main(){ 
                    float dist = length(vUV);
                    float dotPart = 1.0 - smoothstep(0.12, 0.20, dist); // Center dot
                    float ringPart = smoothstep(0.60, 0.75, dist) * (1.0 - smoothstep(0.85, 0.95, dist)); // Outer ring
                    
                    float pulse = sin(u_Time * 4.0) * 0.5 + 0.5;
                    float alpha = max(dotPart, ringPart * (0.4 + pulse * 0.3));
                    if (alpha < 0.01) discard;

                    vec3 color = mix(vec3(1.0), vec3(0.0, 0.8, 1.0), dist); // White center, cyan edge
                    FragColor = vec4(color, alpha * 0.8);
                }
            )GLSL";
            crossProg = CreateProgram(vsSrc, fsSrc);
            if (crossProg != 0) {
                // Empty VAO for mathematically generated vertices
                glGenVertexArrays(1, &crossVao);
                glBindVertexArray(crossVao);
                glBindVertexArray(0);
            } else {
                crosshairEnabled = false;
            }
        }

        // Hotbar HUD (simple colored slots)
        GLuint hotbarProg = 0;
        GLuint hotbarVao = 0;
        GLuint hotbarVbo = 0;
        size_t hotbarVboCapacityFloats = 0;
        if (hotbarEnabled) {
            const char* vsSrc = R"GLSL(
                #version 460 core
                layout(location=0) in vec2 aPos;
                layout(location=1) in vec4 aColor;
                layout(location=2) in vec2 aUv;
                layout(location=3) in float aUseTex;
                out vec4 vColor;
                out vec2 vUv;
                out float vUseTex;
                void main(){ vColor = aColor; vUv = aUv; vUseTex = aUseTex; gl_Position = vec4(aPos, 0.0, 1.0); }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                in vec4 vColor;
                in vec2 vUv;
                in float vUseTex;
                uniform sampler2D u_BlockAtlas;
                uniform float u_FuturisticUI;
                uniform float u_Time;
                uniform vec2 u_Resolution;
                out vec4 FragColor;
                void main(){
                    vec4 base;
                    if (vUseTex > 0.5) {
                        vec4 tex = texture(u_BlockAtlas, vUv);
                        base = tex * vColor;
                    } else {
                        base = vColor;
                    }

                    if (u_FuturisticUI > 0.5) {
                        vec2 res = max(u_Resolution, vec2(1.0));
                        vec2 suv = gl_FragCoord.xy / res;
                        float pulse = 0.5 + 0.5 * sin(u_Time * 4.4);
                        float resScale = clamp(min(res.x, res.y) / 1080.0, 0.85, 1.35);
                        float edge = smoothstep(0.78, 1.00, max(abs(suv.x - 0.5) * 2.0, abs(suv.y - 0.5) * 2.0));
                        float topGlow = smoothstep(0.0, 0.65, 1.0 - suv.y);
                        vec3 blueA = vec3(0.08, 0.16, 0.36);
                        vec3 blueB = vec3(0.10, 0.44, 0.78);
                        vec3 blueC = vec3(0.08, 0.74, 1.00);
                        float grad = clamp(suv.x * 0.65 + suv.y * 0.35, 0.0, 1.0);
                        vec3 holo = mix(mix(blueA, blueB, grad), blueC, 0.20 + 0.14 * pulse);
                        float holoMix = (vUseTex > 0.5) ? (0.16 + 0.07 * pulse) : (0.34 + 0.12 * pulse);
                        base.rgb = mix(base.rgb, base.rgb * holo, holoMix * resScale);
                        base.rgb += holo * (edge * (0.10 + 0.12 * pulse) * resScale);
                        base.rgb += vec3(0.04, 0.09, 0.16) * topGlow * 0.30;
                        base.rgb *= (0.98 + 0.04 * resScale);
                    }

                    FragColor = base;
                }
            )GLSL";
            hotbarProg = CreateProgram(vsSrc, fsSrc);
            if (hotbarProg != 0) {
                glGenVertexArrays(1, &hotbarVao);
                glGenBuffers(1, &hotbarVbo);
                glBindVertexArray(hotbarVao);
                glBindBuffer(GL_ARRAY_BUFFER, hotbarVbo);

                const int slots = 9;
                const int vertsPerQuad = 6;
                const int quadsPerSlot = 4;
                const int floatsPerVert = 9; // pos2 + color4 + uv2 + useTex1
                hotbarVboCapacityFloats = (std::size_t)slots * quadsPerSlot * vertsPerQuad * floatsPerVert;
                glBufferData(GL_ARRAY_BUFFER, hotbarVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(2 * sizeof(float)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));

                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            } else {
                hotbarEnabled = false;
            }
        }

        // Editor selection highlight (world-space)
        GLuint editorSelProg = 0;
        GLuint editorSelVao = 0;
        GLuint editorSelVbo = 0;
        size_t editorSelVboCapacityFloats = 0;
        GLuint ghostSelVao = 0;
        GLuint ghostSelVbo = 0;
        size_t ghostSelVboCapacityFloats = 0;
        {
            const char* vsSrc = R"GLSL(
                #version 460 core
                layout(location=0) in vec3 aPos;
                uniform mat4 u_ViewProjection;
                void main(){ gl_Position = u_ViewProjection * vec4(aPos, 1.0); }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                uniform vec4 u_Color;
                out vec4 FragColor;
                void main(){ FragColor = u_Color; }
            )GLSL";
            editorSelProg = CreateProgram(vsSrc, fsSrc);
            if (editorSelProg != 0) {
                glGenVertexArrays(1, &editorSelVao);
                glGenBuffers(1, &editorSelVbo);
                glBindVertexArray(editorSelVao);
                glBindBuffer(GL_ARRAY_BUFFER, editorSelVbo);
                editorSelVboCapacityFloats = 512;
                glBufferData(GL_ARRAY_BUFFER, editorSelVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

                glGenVertexArrays(1, &ghostSelVao);
                glGenBuffers(1, &ghostSelVbo);
                glBindVertexArray(ghostSelVao);
                glBindBuffer(GL_ARRAY_BUFFER, ghostSelVbo);
                ghostSelVboCapacityFloats = 4096;
                glBufferData(GL_ARRAY_BUFFER, ghostSelVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }

        // Gradient sky background
        const bool skyEnabled = GetEnvBool("HVE_SKY_GRADIENT", true);
        GLuint skyProg = 0;
        GLuint skyVao = 0;
        if (skyEnabled) {
            const std::string skyVs = ReadTextFileOrEmpty("assets/shaders/world_sky.vert");
            const std::string skyFs = ReadTextFileOrEmpty("assets/shaders/world_sky.frag");
            if (!skyVs.empty() && !skyFs.empty()) {
                skyProg = CreateProgram(skyVs.c_str(), skyFs.c_str());
            }
            if (skyProg != 0) {
                glGenVertexArrays(1, &skyVao);
            } else {
                std::cerr << "[Sky] Sky shader disabled (compile/link failed). Using blue clear color fallback." << std::endl;
            }
        }

        // Far-field horizon terrain (multi-level clipmap-like nested grids)
        const bool horizonTerrainEnabled = horizonMode && GetEnvBool("HVE_HORIZON_TERRAIN", true);
        const int horizonGridCells = GetEnvIntClamped("HVE_HORIZON_GRID_CELLS", 96, 24, 192);
        const int horizonLevelCount = GetEnvIntClamped("HVE_HORIZON_LEVELS", 3, 1, 6);
        const float horizonCellSize = GetEnvFloatClamped("HVE_HORIZON_CELL_SIZE", 32.0f, 8.0f, 128.0f);
        const float horizonLevelScale = GetEnvFloatClamped("HVE_HORIZON_LEVEL_SCALE", 2.0f, 1.25f, 4.0f);
        const float horizonYOffset = GetEnvFloatClamped("HVE_HORIZON_Y_OFFSET", -2.0f, -64.0f, 64.0f);
        const float horizonUpdateStep = GetEnvFloatClamped("HVE_HORIZON_UPDATE_STEP", 16.0f, 4.0f, 128.0f);
        const float horizonUpdateMinSec = GetEnvFloatClamped("HVE_HORIZON_UPDATE_MIN_SEC", 0.22f, 0.03f, 2.0f);
        const int horizonPointsPerFrame = GetEnvIntClamped("HVE_HORIZON_POINTS_PER_FRAME", 4096, 256, 65536);
        const bool horizonAdaptiveBudget = GetEnvBool("HVE_HORIZON_ADAPTIVE", true);
        const bool impostorRingEnabled = horizonMode && GetEnvBool("HVE_IMPOSTOR_RING", true);
        const int impostorRingSegments = GetEnvIntClamped("HVE_IMPOSTOR_SEGMENTS", 192, 48, 1024);
        const int impostorRingBands = GetEnvIntClamped("HVE_IMPOSTOR_BANDS", 6, 2, 24);
        const float impostorInnerRadius = GetEnvFloatClamped("HVE_IMPOSTOR_INNER_RADIUS", 2800.0f, 256.0f, 50000.0f);
        const float impostorOuterRadius = GetEnvFloatClamped("HVE_IMPOSTOR_OUTER_RADIUS", 16000.0f, 512.0f, 140000.0f);
        const float impostorBaseHeight = GetEnvFloatClamped("HVE_IMPOSTOR_BASE_HEIGHT", -18.0f, -256.0f, 512.0f);
        const float impostorHeightAmp = GetEnvFloatClamped("HVE_IMPOSTOR_HEIGHT_AMP", 92.0f, 0.0f, 1200.0f);
        const float impostorNoiseScale = GetEnvFloatClamped("HVE_IMPOSTOR_NOISE_SCALE", 0.0014f, 0.00005f, 0.05f);
        const bool impostorAdaptiveBudget = GetEnvBool("HVE_IMPOSTOR_ADAPTIVE", true);
        const float impostorBlendNearStart = GetEnvFloatClamped("HVE_IMPOSTOR_BLEND_NEAR_START", 2200.0f, 64.0f, 120000.0f);
        const float impostorBlendNearEnd = GetEnvFloatClamped("HVE_IMPOSTOR_BLEND_NEAR_END", 3400.0f, 96.0f, 140000.0f);
        const float impostorBlendFarStart = GetEnvFloatClamped("HVE_IMPOSTOR_BLEND_FAR_START", 0.88f, 0.10f, 0.995f);
        const float impostorBlendMinAlpha = GetEnvFloatClamped("HVE_IMPOSTOR_BLEND_MIN_ALPHA", 0.03f, 0.0f, 0.5f);
        const bool impostorDynamicBlend = GetEnvBool("HVE_IMPOSTOR_DYNAMIC_BLEND", true);
        const float impostorDynamicNearScale = GetEnvFloatClamped("HVE_IMPOSTOR_DYNAMIC_NEAR_SCALE", 0.85f, 0.25f, 3.0f);
        const float impostorDynamicNearWidth = GetEnvFloatClamped("HVE_IMPOSTOR_DYNAMIC_NEAR_WIDTH", 1200.0f, 64.0f, 12000.0f);
        const float impostorDynamicMinStart = GetEnvFloatClamped("HVE_IMPOSTOR_DYNAMIC_MIN_START", 600.0f, 64.0f, 60000.0f);
        const bool impostorQueuePressureBlend = GetEnvBool("HVE_IMPOSTOR_QUEUE_PRESSURE_BLEND", true);
        const float impostorQueuePressurePush = GetEnvFloatClamped("HVE_IMPOSTOR_QUEUE_PRESSURE_PUSH", 1600.0f, 0.0f, 24000.0f);
        const float impostorQueuePressureLagMs = GetEnvFloatClamped("HVE_IMPOSTOR_QUEUE_PRESSURE_LAG_MS", 36.0f, 8.0f, 400.0f);
        const bool impostorAutoTunePush = GetEnvBool("HVE_IMPOSTOR_AUTOTUNE_PUSH", true);
        const float impostorAutoTuneTarget = GetEnvFloatClamped("HVE_IMPOSTOR_AUTOTUNE_TARGET", 0.34f, 0.05f, 0.95f);
        const float impostorAutoTuneIntervalSec = GetEnvFloatClamped("HVE_IMPOSTOR_AUTOTUNE_INTERVAL_SEC", 30.0f, 5.0f, 180.0f);
        const int impostorAutoTuneMinSamples = GetEnvIntClamped("HVE_IMPOSTOR_AUTOTUNE_MIN_SAMPLES", 8, 1, 256);
        const float impostorAutoTuneGain = GetEnvFloatClamped("HVE_IMPOSTOR_AUTOTUNE_GAIN", 0.70f, 0.05f, 3.00f);
        const float impostorAutoTuneMulMin = GetEnvFloatClamped("HVE_IMPOSTOR_AUTOTUNE_MUL_MIN", 0.55f, 0.10f, 4.00f);
        const float impostorAutoTuneMulMax = GetEnvFloatClamped("HVE_IMPOSTOR_AUTOTUNE_MUL_MAX", 1.80f, 0.20f, 6.00f);
        const int impostorLowpcMinBands = GetEnvIntClamped("HVE_IMPOSTOR_LOWPC_MIN_BANDS", 2, 1, 12);
        const int impostorLowpcMaxCadence = GetEnvIntClamped("HVE_IMPOSTOR_LOWPC_MAX_CADENCE", 4, 1, 8);
        const bool impostorControlProtect = GetEnvBool("HVE_IMPOSTOR_CONTROL_PROTECT", true);

        struct HorizonLevel {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLuint ebo = 0;
            std::vector<float> verts;
            std::vector<unsigned int> indices;
            float centerX = std::numeric_limits<float>::infinity();
            float centerZ = std::numeric_limits<float>::infinity();
            int updateCursor = 0;
            float cellSize = 1.0f;
            float snapStep = 1.0f;
            bool ready = false;
        };

        GLuint horizonProg = 0;
        std::vector<HorizonLevel> horizonLevels;

        GLuint impostorProg = 0;
        GLuint impostorVao = 0;
        GLuint impostorVbo = 0;
        GLuint impostorEbo = 0;
        GLsizei impostorIndexCount = 0;

        if (horizonTerrainEnabled) {
            const char* hVs = R"GLSL(
                #version 460 core
                layout(location=0) in vec3 aPos;
                uniform mat4 u_ViewProjection;
                out vec3 vWorldPos;
                void main() {
                    vWorldPos = aPos;
                    gl_Position = u_ViewProjection * vec4(aPos, 1.0);
                }
            )GLSL";

            const char* hFs = R"GLSL(
                #version 460 core
                in vec3 vWorldPos;
                uniform vec3 u_ViewPos;
                uniform vec3 u_SunDir;
                uniform vec3 u_FogColor;
                uniform float u_FogDensity;
                uniform float u_BlendNearStart;
                uniform float u_BlendNearEnd;
                uniform float u_BlendFarStart;
                uniform float u_BlendMinAlpha;
                out vec4 FragColor;

                void main() {
                    vec3 dpx = dFdx(vWorldPos);
                    vec3 dpy = dFdy(vWorldPos);
                    vec3 N = normalize(cross(dpy, dpx));

                    float slope = 1.0 - clamp(abs(N.y), 0.0, 1.0);
                    vec3 grass = vec3(0.12, 0.34, 0.14);
                    vec3 rock = vec3(0.34, 0.34, 0.36);
                    vec3 albedo = mix(grass, rock, smoothstep(0.22, 0.68, slope));

                    vec3 L = normalize(u_SunDir);
                    float ndl = max(dot(N, L), 0.0);
                    vec3 lit = albedo * (0.34 + ndl * 0.78);

                    float dist = distance(u_ViewPos, vWorldPos);
                    float fog = 1.0 - exp(-max(0.00001, u_FogDensity) * dist);
                    vec3 col = mix(lit, u_FogColor, clamp(fog, 0.0, 1.0));
                    FragColor = vec4(col, 1.0);
                }
            )GLSL";

            horizonProg = CreateProgram(hVs, hFs);
            if (horizonProg != 0) {
                const int vertsPerAxis = horizonGridCells + 1;
                const std::size_t pointCount = (std::size_t)vertsPerAxis * (std::size_t)vertsPerAxis;

                horizonLevels.resize((std::size_t)horizonLevelCount);
                for (int li = 0; li < horizonLevelCount; ++li) {
                    HorizonLevel& level = horizonLevels[(std::size_t)li];
                    level.cellSize = horizonCellSize * std::pow(horizonLevelScale, (float)li);
                    level.snapStep = horizonUpdateStep * std::pow(horizonLevelScale, (float)li);
                    level.verts.resize(pointCount * 3u, 0.0f);
                    level.indices.reserve((std::size_t)horizonGridCells * (std::size_t)horizonGridCells * 6u);

                    for (int z = 0; z < horizonGridCells; ++z) {
                        for (int x = 0; x < horizonGridCells; ++x) {
                            const unsigned int i0 = (unsigned int)(z * vertsPerAxis + x);
                            const unsigned int i1 = i0 + 1u;
                            const unsigned int i2 = i0 + (unsigned int)vertsPerAxis;
                            const unsigned int i3 = i2 + 1u;
                            level.indices.push_back(i0);
                            level.indices.push_back(i2);
                            level.indices.push_back(i1);
                            level.indices.push_back(i1);
                            level.indices.push_back(i2);
                            level.indices.push_back(i3);
                        }
                    }

                    glGenVertexArrays(1, &level.vao);
                    glGenBuffers(1, &level.vbo);
                    glGenBuffers(1, &level.ebo);

                    glBindVertexArray(level.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, level.vbo);
                    glBufferData(GL_ARRAY_BUFFER, level.verts.size() * sizeof(float), level.verts.data(), GL_DYNAMIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, level.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, level.indices.size() * sizeof(unsigned int), level.indices.data(), GL_STATIC_DRAW);
                }

                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            } else {
                std::cerr << "[Horizon] Shader disabled (compile/link failed)." << std::endl;
            }
        }

        if (impostorRingEnabled) {
            const char* impVs = R"GLSL(
                #version 460 core
                layout(location=0) in vec3 aLocal;
                uniform mat4 u_ViewProjection;
                uniform vec3 u_ViewPos;
                uniform float u_BaseHeight;
                uniform float u_HeightAmp;
                uniform float u_NoiseScale;
                uniform float u_Time;
                out vec3 vWorldPos;
                out float vRingT;

                float layeredNoise(vec2 p, float t) {
                    float n0 = sin(p.x * 1.0 + t * 0.08) * cos(p.y * 0.9 - t * 0.05);
                    float n1 = sin(p.x * 2.6 - t * 0.04) * cos(p.y * 2.1 + t * 0.03);
                    float n2 = sin((p.x + p.y) * 4.8 + t * 0.02);
                    return n0 * 0.62 + n1 * 0.28 + n2 * 0.10;
                }

                void main() {
                    vec2 worldXZ = u_ViewPos.xz + vec2(aLocal.x, aLocal.z);
                    float ringT = clamp(aLocal.y, 0.0, 1.0);
                    float n = layeredNoise(worldXZ * u_NoiseScale, u_Time);
                    float amp = u_HeightAmp * mix(1.0, 0.45, ringT);
                    float y = u_BaseHeight + n * amp;

                    vWorldPos = vec3(worldXZ.x, y, worldXZ.y);
                    vRingT = ringT;
                    gl_Position = u_ViewProjection * vec4(vWorldPos, 1.0);
                }
            )GLSL";

            const char* impFs = R"GLSL(
                #version 460 core
                in vec3 vWorldPos;
                in float vRingT;
                uniform vec3 u_ViewPos;
                uniform vec3 u_SunDir;
                uniform vec3 u_FogColor;
                uniform float u_FogDensity;
                uniform float u_BlendNearStart;
                uniform float u_BlendNearEnd;
                uniform float u_BlendFarStart;
                uniform float u_BlendMinAlpha;
                out vec4 FragColor;

                void main() {
                    vec3 dpx = dFdx(vWorldPos);
                    vec3 dpy = dFdy(vWorldPos);
                    vec3 N = normalize(cross(dpy, dpx));
                    if (!all(greaterThan(abs(N), vec3(0.00001)))) {
                        N = vec3(0.0, 1.0, 0.0);
                    }

                    float slope = 1.0 - clamp(abs(N.y), 0.0, 1.0);
                    vec3 low = vec3(0.10, 0.27, 0.14);
                    vec3 high = vec3(0.30, 0.31, 0.34);
                    vec3 albedo = mix(low, high, smoothstep(0.18, 0.76, slope));
                    albedo = mix(albedo, albedo * 0.82, smoothstep(0.65, 1.0, vRingT));

                    vec3 L = normalize(u_SunDir);
                    float ndl = max(dot(N, L), 0.0);
                    vec3 lit = albedo * (0.32 + ndl * 0.62);

                    float dist = distance(u_ViewPos, vWorldPos);
                    float farBias = mix(1.25, 2.00, vRingT);
                    float fog = 1.0 - exp(-max(0.00001, u_FogDensity) * farBias * dist);
                    vec3 col = mix(lit, u_FogColor, clamp(fog, 0.0, 1.0));

                    float nearFade = smoothstep(u_BlendNearStart, u_BlendNearEnd, dist);
                    float farFade = 1.0 - smoothstep(u_BlendFarStart, 1.0, vRingT);
                    float alpha = max(u_BlendMinAlpha, nearFade * farFade);
                    if (alpha <= u_BlendMinAlpha + 0.0005) {
                        discard;
                    }

                    FragColor = vec4(col, alpha);
                }
            )GLSL";

            impostorProg = CreateProgram(impVs, impFs);
            if (impostorProg != 0) {
                const float innerR = std::max(64.0f, impostorInnerRadius);
                const float outerR = std::max(innerR + 32.0f, impostorOuterRadius);
                const int segs = std::max(16, impostorRingSegments);
                const int bands = std::max(1, impostorRingBands);
                const float twoPi = 6.2831853071795864769f;

                std::vector<float> ringVerts;
                std::vector<unsigned int> ringIndices;
                ringVerts.reserve((std::size_t)(bands + 1) * (std::size_t)(segs + 1) * 3u);
                ringIndices.reserve((std::size_t)bands * (std::size_t)segs * 6u);

                for (int b = 0; b <= bands; ++b) {
                    const float t = (bands > 0) ? ((float)b / (float)bands) : 0.0f;
                    const float radius = innerR + (outerR - innerR) * t;
                    for (int s = 0; s <= segs; ++s) {
                        const float a = twoPi * ((float)s / (float)segs);
                        ringVerts.push_back(std::cos(a) * radius);
                        ringVerts.push_back(t);
                        ringVerts.push_back(std::sin(a) * radius);
                    }
                }

                const int stride = segs + 1;
                for (int b = 0; b < bands; ++b) {
                    for (int s = 0; s < segs; ++s) {
                        const unsigned int i0 = (unsigned int)(b * stride + s);
                        const unsigned int i1 = i0 + 1u;
                        const unsigned int i2 = i0 + (unsigned int)stride;
                        const unsigned int i3 = i2 + 1u;
                        ringIndices.push_back(i0);
                        ringIndices.push_back(i2);
                        ringIndices.push_back(i1);
                        ringIndices.push_back(i1);
                        ringIndices.push_back(i2);
                        ringIndices.push_back(i3);
                    }
                }

                impostorIndexCount = (GLsizei)ringIndices.size();

                glGenVertexArrays(1, &impostorVao);
                glGenBuffers(1, &impostorVbo);
                glGenBuffers(1, &impostorEbo);

                glBindVertexArray(impostorVao);
                glBindBuffer(GL_ARRAY_BUFFER, impostorVbo);
                glBufferData(GL_ARRAY_BUFFER, ringVerts.size() * sizeof(float), ringVerts.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impostorEbo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, ringIndices.size() * sizeof(unsigned int), ringIndices.data(), GL_STATIC_DRAW);

                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            } else {
                std::cerr << "[Impostor] Shader disabled (compile/link failed)." << std::endl;
            }
        }

        // Inventory UI (E): simple grid overlay; click selects a block.
        GLuint invProg = 0;
        GLuint invVao = 0;
        GLuint invVbo = 0;
        size_t invVboCapacityFloats = 0;
        {
            const char* vsSrc = R"GLSL(
                #version 460 core
                layout(location=0) in vec2 aPos;
                layout(location=1) in vec4 aColor;
                layout(location=2) in vec2 aUv;
                layout(location=3) in float aUseTex;
                out vec4 vColor;
                out vec2 vUv;
                out float vUseTex;
                void main(){ vColor = aColor; vUv = aUv; vUseTex = aUseTex; gl_Position = vec4(aPos, 0.0, 1.0); }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                in vec4 vColor;
                in vec2 vUv;
                in float vUseTex;
                uniform sampler2D u_BlockAtlas;
                uniform float u_FuturisticUI;
                uniform float u_Time;
                uniform vec2 u_Resolution;
                out vec4 FragColor;
                void main(){
                    vec4 base;
                    if (vUseTex > 0.5) {
                        vec4 tex = texture(u_BlockAtlas, vUv);
                        base = tex * vColor;
                    } else {
                        base = vColor;
                    }

                    if (u_FuturisticUI > 0.5) {
                        vec2 res = max(u_Resolution, vec2(1.0));
                        vec2 suv = gl_FragCoord.xy / res;
                        float pulse = 0.5 + 0.5 * sin(u_Time * 3.8);
                        float resScale = clamp(min(res.x, res.y) / 1080.0, 0.85, 1.35);
                        float edge = smoothstep(0.80, 1.00, max(abs(suv.x - 0.5) * 2.0, abs(suv.y - 0.5) * 2.0));
                        float topGlow = smoothstep(0.0, 0.62, 1.0 - suv.y);
                        vec3 holo = mix(vec3(0.08, 0.16, 0.34), vec3(0.08, 0.62, 0.98), clamp(suv.x * 0.70 + suv.y * 0.30, 0.0, 1.0));
                        float holoMix = (vUseTex > 0.5) ? (0.16 + 0.07 * pulse) : (0.30 + 0.12 * pulse);
                        base.rgb = mix(base.rgb, base.rgb * holo, holoMix * resScale);
                        base.rgb += holo * (edge * (0.10 + 0.11 * pulse) * resScale);
                        base.rgb += vec3(0.03, 0.08, 0.14) * topGlow * 0.26;
                        base.rgb *= (0.98 + 0.04 * resScale);
                    }

                    FragColor = base;
                }
            )GLSL";
            invProg = CreateProgram(vsSrc, fsSrc);
            if (invProg != 0) {
                glGenVertexArrays(1, &invVao);
                glGenBuffers(1, &invVbo);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);

                // Shared UI buffer (inventory + menus + text as quads).
                // Keep this reasonably large to avoid resizing/stutters.
                const int maxQuads = 8192;
                const int vertsPerQuad = 6;
                const int floatsPerVert = 9; // pos2 + color4 + uv2 + useTex1
                invVboCapacityFloats = (size_t)maxQuads * vertsPerQuad * floatsPerVert;
                glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(2 * sizeof(float)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));

                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }

        auto applyUiProgramUniforms = [&](GLuint program) {
            if (program == 0) return;
            const GLint holoLoc = glGetUniformLocation(program, "u_FuturisticUI");
            if (holoLoc >= 0) glUniform1f(holoLoc, futuristicUi ? 1.0f : 0.0f);

            const GLint timeLoc = glGetUniformLocation(program, "u_Time");
            if (timeLoc >= 0) glUniform1f(timeLoc, (float)glfwGetTime());

            const GLint resLoc = glGetUniformLocation(program, "u_Resolution");
            if (resLoc >= 0) glUniform2f(resLoc, (float)Window::GetWidth(), (float)Window::GetHeight());
        };

        // Aim outline + placement preview (minimal building UX; no HUD).
        bool aimOutlineEnabled = GetEnvBool("HVE_AIM_OUTLINE", true);
        // Removed simpleBuild override for aim outline
        GLuint aimProg = 0;
        GLuint aimVao = 0;
        GLuint aimVbo = 0;
        GLint aimLocVP = -1;
        GLint aimLocPos = -1;
        GLint aimLocColor = -1;
        if (aimOutlineEnabled) {
            // =========================================================================
            // INVENTION: AAA Procedural Glowing Target Highlighter (No-VBO)
            // Replaces the old static GL_LINES 1px thin outline with a pulsating, 
            // sci-fi translucent neon box that renders 36 triangles mathematically!
            // =========================================================================
            const char* vsSrc = R"GLSL(
                #version 460 core
                uniform mat4 u_ViewProjection;
                uniform vec3 u_WorldPos;
                out vec3 vLocalPos;
                
                const vec3 corners[8] = vec3[](
                    vec3(0,0,0), vec3(1,0,0), vec3(1,0,1), vec3(0,0,1),
                    vec3(0,1,0), vec3(1,1,0), vec3(1,1,1), vec3(0,1,1)
                );
                const int indices[36] = int[](
                    0,1,2, 2,3,0, 
                    4,7,6, 6,5,4, 
                    0,4,5, 5,1,0, 
                    1,5,6, 6,2,1, 
                    2,6,7, 7,3,2, 
                    3,7,4, 4,0,3
                );

                void main(){
                    vec3 pos = corners[indices[gl_VertexID]];
                    vLocalPos = pos - 0.5;
                    // Slightly expand to avoid z-fighting
                    pos = vLocalPos * 1.01 + 0.5;
                    gl_Position = u_ViewProjection * vec4(pos + u_WorldPos, 1.0);
                }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                uniform vec3 u_Color;
                uniform float u_Time;
                in vec3 vLocalPos;
                out vec4 FragColor;
                void main(){
                    vec3 d = abs(vLocalPos);
                    float eX = smoothstep(0.46, 0.505, d.y) * smoothstep(0.46, 0.505, d.z);
                    float eY = smoothstep(0.46, 0.505, d.x) * smoothstep(0.46, 0.505, d.z);
                    float eZ = smoothstep(0.46, 0.505, d.x) * smoothstep(0.46, 0.505, d.y);
                    float edge = clamp(eX + eY + eZ, 0.0, 1.0);
                    
                    float pulse = sin(u_Time * 5.0) * 0.5 + 0.5;
                    float innerGlow = 0.05 + pulse * 0.05;
                    float alpha = edge * 0.9 + innerGlow;
                    
                    vec3 finalCol = mix(u_Color * 0.5, u_Color * 2.0, edge);
                    FragColor = vec4(finalCol, alpha);
                }
            )GLSL";
            aimProg = CreateProgram(vsSrc, fsSrc);
            if (aimProg != 0) {
                aimLocVP = glGetUniformLocation(aimProg, "u_ViewProjection");
                aimLocPos = glGetUniformLocation(aimProg, "u_WorldPos");
                aimLocColor = glGetUniformLocation(aimProg, "u_Color");
                
                // We generate an empty VAO since we use gl_VertexID mathematically
                glGenVertexArrays(1, &aimVao);
                glBindVertexArray(aimVao);
                glBindVertexArray(0);
            } else {
                aimOutlineEnabled = false;
            }
        }

        // Placement face highlight (subtle side marker on the target block).
        bool placeFaceEnabled = GetEnvBool("HVE_PLACE_FACE", false); // Disabled by default in favor of new sci-fi highlight
        const float placeFaceAlpha = GetEnvFloatClamped("HVE_PLACE_FACE_ALPHA", 0.20f, 0.05f, 0.50f);
        const float placeRetargetDelay = GetEnvFloatClamped("HVE_PLACE_RETARGET_DELAY", 0.0f, 0.0f, 0.8f);
        GLuint faceProg = 0;
        GLuint faceVao = 0;
        GLuint faceVbo = 0;
        GLint faceLocVP = -1;
        GLint faceLocColor = -1;
        if (placeFaceEnabled) {
            const char* vsSrc = R"GLSL(
                #version 460 core
                layout(location=0) in vec3 aPos;
                uniform mat4 u_ViewProjection;
                void main(){ gl_Position = u_ViewProjection * vec4(aPos, 1.0); }
            )GLSL";
            const char* fsSrc = R"GLSL(
                #version 460 core
                uniform vec3 u_Color;
                uniform float u_Alpha;
                out vec4 FragColor;
                void main(){ FragColor = vec4(u_Color, u_Alpha); }
            )GLSL";
            faceProg = CreateProgram(vsSrc, fsSrc);
            if (faceProg != 0) {
                faceLocVP = glGetUniformLocation(faceProg, "u_ViewProjection");
                faceLocColor = glGetUniformLocation(faceProg, "u_Color");
                glGenVertexArrays(1, &faceVao);
                glGenBuffers(1, &faceVbo);
                glBindVertexArray(faceVao);
                glBindBuffer(GL_ARRAY_BUFFER, faceVbo);
                glBufferData(GL_ARRAY_BUFFER, 6 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            } else {
                placeFaceEnabled = false;
            }
        }

        double lastRaycastLogTime = -1.0;
        double lastBreakTime = -1.0;
        double lastPlaceTime = -1.0;
        double breakPressStartTime = -1.0;
        double placePressStartTime = -1.0;
        double lastAimHitTime = -1.0;
        Game::World::RaycastHit lastAimHit{};
        bool hasLastAimHit = false;
        glm::ivec3 lastPlaceTarget{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        bool hasLastPlaceTarget = false;
        glm::ivec3 holdPlaceAnchor{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        glm::ivec3 holdPlaceLast{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        int holdPlaceAxis = -1; // -1 unset, 0 X, 1 Y, 2 Z
        bool holdPlaceActive = false;
        glm::ivec3 lastFaceTargetBlock{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        glm::ivec3 lastFaceTargetPrev{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        bool faceCacheValid = false;
        glm::ivec3 lastRaycastBlock{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        uint8_t lastRaycastBlockId = 0xFF;

        const int preloadTargetLayers = GetEnvIntClamped("HVE_PRELOAD_TARGET_LAYERS", 1, 1, 32);
        const bool preloadTargetCircular = GetEnvBool("HVE_PRELOAD_TARGET_CIRCULAR", true);
        int preloadTargetChunks = 0;
        if (preloadRadiusChunks > 0) {
            const int fullGridSpan = (2 * preloadRadiusChunks + 1);
            const int fullGridArea = fullGridSpan * fullGridSpan;
            const int effectiveLayers = std::max(1, std::min(std::min(heightChunks, preloadHeightChunks), preloadTargetLayers));
            int targetArea = fullGridArea;
            if (preloadTargetCircular) {
                const float r = (float)preloadRadiusChunks;
                targetArea = std::max(1, (int)std::lround(3.14159265f * r * r));
            }
            preloadTargetChunks = std::clamp(targetArea * effectiveLayers, 1, fullGridArea * std::max(1, std::min(heightChunks, preloadHeightChunks)));
        }
        bool preloadDone = (preloadRadiusChunks <= 0);
        double preloadStartTime = (double)glfwGetTime();
        double starvationRescueUntil = -1.0;
        int startupBlockLastChunks = 0;
        double startupBlockLastProgressTime = preloadStartTime;
        bool startupPreloadGateReleasedLatched = false;
        const bool startupLoadingOverlay = GetEnvBool("HVE_STARTUP_LOADING_OVERLAY", false);
        bool loadingOverlayDismissedByInput = false;

        if (worldStarted && !menuOpen && !preloadDone && startupLoadingOverlay && !loadingOverlayDismissedByInput) {
            showLoadingOverlay = true;
            menuScreen = MenuScreen::Loading;
            mouseCaptured = false;
            Input::SetCursorMode(false);
        }

        bool cameraAutoPlaced = false;
        bool instantHugeQueued = false;
        int megaPreloadRadiusCurrent = instantHugeRadius;
        int megaPreloadVerticalCurrent = instantHugeVertical;
        double megaPreloadLastPulse = -1.0;
        const double autoPlaceStartTime = glfwGetTime();
        const double autoPlaceFallbackSec = hardSafeMode ? 0.35 : 2.0;
        const bool emergencyTerrainBootstrap = GetEnvBool("HVE_EMERGENCY_TERRAIN_BOOTSTRAP", true);
        const double emergencyTerrainDelaySec = hardSafeMode ? 1.2 : 3.0;
        bool emergencyTerrainDone = false;

        auto tryFindSafeSpawn = [&](const glm::ivec2& anchorXZ, glm::vec3& outPos) -> bool {
            const int maxY = heightChunks * Game::World::CHUNK_SIZE - 1;
            const int bodyCells = std::max(2, (int)std::ceil(walkBodyHeight));
            const int searchRadius = GetEnvIntClamped("HVE_SPAWN_SEARCH_RADIUS", 48, 4, 160);
            const int flatRadius = GetEnvIntClamped("HVE_SPAWN_FLAT_RADIUS", 2, 1, 4);
            const int flatMaxDelta = GetEnvIntClamped("HVE_SPAWN_FLAT_MAX_DELTA", 0, 0, 8);
            const bool requireGrass = GetEnvBool("HVE_SPAWN_REQUIRE_GRASS", true);
            const int lateralClearanceRadius = GetEnvIntClamped("HVE_SPAWN_CLEARANCE_RADIUS", 2, 0, 2);
            const bool buildSpawnPad = GetEnvBool("HVE_SPAWN_BUILD_PAD", true);
            const int spawnPadRadius = GetEnvIntClamped("HVE_SPAWN_PAD_RADIUS", 2, 0, 3);

            auto getBlockId = [&](int x, int y, int z) -> uint8_t {
                if (y < 0 || y > maxY) return 0;
                uint8_t b = chunkManager.GetBlockWorld(glm::ivec3(x, y, z));
                if (b == 0) {
                    b = Game::World::Generation::GetBlockAtWorld(x, y, z);
                }
                return b;
            };

            auto isSolidForSpawn = [&](uint8_t b) -> bool {
                // Water is passable for spawn/collision checks.
                return b != 0 && b != 5;
            };

            auto columnTopSolid = [&](int x, int z) -> int {
                for (int y = maxY; y >= 0; --y) {
                    if (isSolidForSpawn(getBlockId(x, y, z))) return y;
                }
                return -1;
            };

            auto hasClearance = [&](int x, int groundY, int z) -> bool {
                for (int k = 1; k <= (bodyCells + 1); ++k) {
                    const uint8_t b = getBlockId(x, groundY + k, z);
                    if (b != 0) return false;
                }
                return true;
            };

            auto hasLateralClearance = [&](int x, int groundY, int z) -> bool {
                for (int dz = -lateralClearanceRadius; dz <= lateralClearanceRadius; ++dz) {
                    for (int dx = -lateralClearanceRadius; dx <= lateralClearanceRadius; ++dx) {
                        for (int k = 1; k <= bodyCells; ++k) {
                            const uint8_t b = getBlockId(x + dx, groundY + k, z + dz);
                            if (isSolidForSpawn(b)) return false;
                        }
                    }
                }
                return true;
            };

            auto flatPenalty = [&](int x, int z, int groundY) -> int {
                int penalty = 0;
                for (int dz = -flatRadius; dz <= flatRadius; ++dz) {
                    for (int dx = -flatRadius; dx <= flatRadius; ++dx) {
                        const int gy = columnTopSolid(x + dx, z + dz);
                        if (gy < 0) return std::numeric_limits<int>::max() / 4;
                        const int d = std::abs(gy - groundY);
                        if (d > flatMaxDelta) return std::numeric_limits<int>::max() / 4;
                        if (requireGrass) {
                            const uint8_t top = getBlockId(x + dx, gy, z + dz);
                            if (top != 2) return std::numeric_limits<int>::max() / 4;
                        }
                        penalty += d;
                    }
                }
                return penalty;
            };

            auto buildPadAt = [&](int centerX, int groundY, int centerZ) {
                if (!buildSpawnPad) return;
                chunkManager.BeginBulkEdit();
                for (int dz = -spawnPadRadius; dz <= spawnPadRadius; ++dz) {
                    for (int dx = -spawnPadRadius; dx <= spawnPadRadius; ++dx) {
                        const int x = centerX + dx;
                        const int z = centerZ + dz;
                        chunkManager.SetBlockWorld(glm::ivec3(x, groundY - 1, z), 1); // dirt support
                        chunkManager.SetBlockWorld(glm::ivec3(x, groundY, z), 2);     // grass top
                        for (int k = 1; k <= (bodyCells + 2); ++k) {
                            chunkManager.SetBlockWorld(glm::ivec3(x, groundY + k, z), 0); // clear air
                        }
                    }
                }
                chunkManager.EndBulkEdit();
            };

            const int anchorGround = std::max(0, columnTopSolid(anchorXZ.x, anchorXZ.y));

            int bestX = anchorXZ.x;
            int bestZ = anchorXZ.y;
            int bestGround = -1;
            int bestScore = std::numeric_limits<int>::max();

            for (int r = 0; r <= searchRadius; ++r) {
                const int x0 = anchorXZ.x - r;
                const int x1 = anchorXZ.x + r;
                const int z0 = anchorXZ.y - r;
                const int z1 = anchorXZ.y + r;

                for (int x = x0; x <= x1; ++x) {
                    for (int z = z0; z <= z1; ++z) {
                        if (r > 0 && x > x0 && x < x1 && z > z0 && z < z1) continue;

                        const int groundY = columnTopSolid(x, z);
                        if (groundY < 0 || groundY >= maxY - 2) continue;
                        if (requireGrass && getBlockId(x, groundY, z) != 2) continue;
                        if (!hasClearance(x, groundY, z)) continue;
                        if (!hasLateralClearance(x, groundY, z)) continue;

                        const int flat = flatPenalty(x, z, groundY);
                        if (flat >= std::numeric_limits<int>::max() / 8) continue;

                        const int dx = x - anchorXZ.x;
                        const int dz = z - anchorXZ.y;
                        const int dist2 = dx * dx + dz * dz;
                        const int heightPenalty = std::abs(groundY - anchorGround);
                        const int score = dist2 * 2 + flat * 12 + heightPenalty * 3;
                        if (score < bestScore) {
                            bestScore = score;
                            bestX = x;
                            bestZ = z;
                            bestGround = groundY;
                        }
                    }
                }

                if (bestGround >= 0 && r >= 2) break;
            }

            if (bestGround < 0) return false;

            buildPadAt(bestX, bestGround, bestZ);

            outPos = glm::vec3((float)bestX + 0.5f, (float)bestGround + walkEyeHeight + 0.12f, (float)bestZ + 0.5f);
            return true;
        };

        auto spawnCollidesAt = [&](const glm::vec3& pos) -> bool {
            const int maxY = heightChunks * Game::World::CHUNK_SIZE - 1;
            auto getSolidAt = [&](int x, int y, int z) -> bool {
                if (y < 0 || y > maxY) return false;
                uint8_t b = chunkManager.GetBlockWorld(glm::ivec3(x, y, z));
                if (b == 0) {
                    b = Game::World::Generation::GetBlockAtWorld(x, y, z);
                }
                return (b != 0 && b != 5);
            };

            const float feetY = pos.y - walkEyeHeight;
            const int minY = std::max(0, (int)std::floor(feetY + 0.01f));
            const int maxBodyY = std::min(maxY, (int)std::floor(feetY + walkBodyHeight - 0.05f));
            if (minY > maxBodyY) return false;

            const float probeR = std::max(0.05f, walkCollisionRadius - walkCollisionSkin);
            const std::array<glm::vec2, 9> samples = {
                glm::vec2(0.0f, 0.0f),
                glm::vec2( probeR, 0.0f),
                glm::vec2(-probeR, 0.0f),
                glm::vec2(0.0f,  probeR),
                glm::vec2(0.0f, -probeR),
                glm::vec2( probeR,  probeR),
                glm::vec2( probeR, -probeR),
                glm::vec2(-probeR,  probeR),
                glm::vec2(-probeR, -probeR)
            };

            for (const auto& s : samples) {
                const int sx = (int)std::floor(pos.x + s.x);
                const int sz = (int)std::floor(pos.z + s.y);
                for (int y = minY; y <= maxBodyY; ++y) {
                    if (getSolidAt(sx, y, sz)) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto enforceSpawnBubble = [&](const glm::vec3& pos) {
            const int bodyCells = std::max(2, (int)std::ceil(walkBodyHeight));
            const int cx = (int)std::floor(pos.x);
            const int cz = (int)std::floor(pos.z);
            const int groundY = (int)std::floor(pos.y - walkEyeHeight - 0.02f);
            chunkManager.BeginBulkEdit();
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    chunkManager.SetBlockWorld(glm::ivec3(cx + dx, groundY, cz + dz), 2);
                    chunkManager.SetBlockWorld(glm::ivec3(cx + dx, groundY - 1, cz + dz), 1);
                    for (int k = 1; k <= bodyCells + 2; ++k) {
                        chunkManager.SetBlockWorld(glm::ivec3(cx + dx, groundY + k, cz + dz), 0);
                    }
                }
            }
            chunkManager.EndBulkEdit();
        };

        auto resolveSpawnUnstuck = [&](glm::vec3& pos) -> bool {
            if (!spawnCollidesAt(pos)) return true;

            const std::array<glm::ivec2, 13> offsets = {
                glm::ivec2{ 0, 0},
                glm::ivec2{ 1, 0}, glm::ivec2{-1, 0}, glm::ivec2{ 0, 1}, glm::ivec2{ 0,-1},
                glm::ivec2{ 1, 1}, glm::ivec2{ 1,-1}, glm::ivec2{-1, 1}, glm::ivec2{-1,-1},
                glm::ivec2{ 2, 0}, glm::ivec2{-2, 0}, glm::ivec2{ 0, 2}, glm::ivec2{ 0,-2}
            };

            for (int lift = 0; lift <= 6; ++lift) {
                for (const auto& o : offsets) {
                    glm::vec3 candidate = pos;
                    candidate.x += (float)o.x;
                    candidate.z += (float)o.y;
                    candidate.y += (float)lift;
                    if (!spawnCollidesAt(candidate)) {
                        pos = candidate;
                        return true;
                    }
                }
            }

            enforceSpawnBubble(pos);
            if (!spawnCollidesAt(pos)) return true;

            for (int lift = 1; lift <= 8; ++lift) {
                glm::vec3 up = pos;
                up.y += (float)lift;
                if (!spawnCollidesAt(up)) {
                    pos = up;
                    return true;
                }
            }

            return false;
        };

        auto ensureWorldsDir = [&]() {
            std::error_code ec;
            std::filesystem::create_directories("worlds", ec);
        };

        auto scanWorlds = [&]() {
            ensureWorldsDir();
            std::vector<std::string> out;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator("worlds", ec)) {
                if (ec) break;
                if (!e.is_regular_file()) continue;
                const auto p = e.path();
                if (p.extension() == ".hvew") {
                    out.push_back(p.generic_string());
                }
            }
            std::sort(out.begin(), out.end());
            worldList = std::move(out);
            if (selectedWorldIndex < 0) selectedWorldIndex = 0;
            if (selectedWorldIndex >= (int)worldList.size()) selectedWorldIndex = (int)worldList.size() - 1;
            if (selectedWorldIndex < 0) selectedWorldIndex = 0;
        };

        auto persistSessionState = [&]() {
            ini["quality"] = std::to_string(qualityLevel);
            ini["world_file"] = worldFile;
            SaveIniKeyValue(settingsPath, ini);
        };

        auto saveCurrentWorld = [&](const char* sourceTag) -> bool {
            ensureWorldsDir();
            if (worldFile.empty()) {
                worldFile = "worlds/hve_world.hvew";
            }
            if (loadingWorld || chunkManager.IsAsyncLoadInProgress()) {
                menuStatusText = "Save blocked: world is loading";
                return false;
            }

            std::error_code ec;
            const std::filesystem::path p(worldFile);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path(), ec);
            }

            const bool ok = chunkManager.SaveWorldToFile(worldFile.c_str());
            menuStatusText = ok ? "World saved" : "Save FAILED";
            std::cout << "[" << sourceTag << "] "
                      << (ok ? "World saved: " : "World save FAILED: ")
                      << worldFile << std::endl;

            if (ok) {
                persistSessionState();
                scanWorlds();
            }
            return ok;
        };

        auto beginLoadCurrentWorldAsync = [&](const char* sourceTag) -> bool {
            if (worldFile.empty()) {
                menuStatusText = "Load failed: empty world path";
                return false;
            }
            if (loadingWorld || chunkManager.IsAsyncLoadInProgress()) {
                menuStatusText = "Load already in progress";
                return false;
            }

            std::error_code ec;
            if (!std::filesystem::exists(worldFile, ec)) {
                menuStatusText = "Load failed: world file missing";
                return false;
            }

            const bool ok = chunkManager.BeginLoadWorldFromFileAsync(worldFile.c_str(), worldRenderer);
            std::cout << "[" << sourceTag << "] "
                      << (ok ? "World loading: " : "World load FAILED: ")
                      << worldFile << std::endl;

            if (ok) {
                loadingWorld = true;
                menuScreen = MenuScreen::Loading;
                menuOpen = true;
                showLoadingOverlay = true;
                mouseCaptured = false;
                Input::SetCursorMode(false);
                menuStatusText.clear();
                persistSessionState();
            } else {
                menuStatusText = "Load failed";
            }
            return ok;
        };

        const bool autosaveEnabled = GetEnvBool("HVE_AUTOSAVE", true);
        const double autosaveSec = (double)GetEnvFloatClamped("HVE_AUTOSAVE_SEC", 180.0f, 15.0f, 3600.0f);
        double lastAutosaveTime = (double)glfwGetTime();

        auto addAmuletLog = [&](const std::string& line) {
            amuletLog.push_back(line);
            if (amuletLog.size() > 5) {
                amuletLog.erase(amuletLog.begin());
            }
        };

        auto launchAmuletBridge = [&]() {
            const bool saved = chunkManager.SaveWorldToFile(worldFile.c_str());
            if (!saved) {
                amuletStatusText = "Amulet launch failed: world save failed";
                addAmuletLog("Save failed");
                std::cout << amuletStatusText << std::endl;
                return;
            }

            amuletBridgeActive = true;
            amuletReturnState.store(0);
            amuletStartTime = glfwGetTime();
            amuletPayloadSent = false;
            amuletSaveDetected = false;
            amuletReloadQueued = false;
            amuletReloadDone = false;
            amuletStatusText = "Starting Amulet...";
            addAmuletLog("Launching Amulet");

            menuOpen = true;
            menuScreen = MenuScreen::Amulet;
            mouseCaptured = false;
            Input::SetCursorMode(false);

            const uint16_t returnPort = (uint16_t)GetEnvIntClamped("HVE_AMULET_RETURN_PORT", 49633, 1024, 65535);
            const int returnTimeoutSec = GetEnvIntClamped("HVE_AMULET_RETURN_TIMEOUT_SEC", 3600, 10, 86400);
            std::thread([returnPort, returnTimeoutSec, &amuletReturnState]() {
                ListenForAmuletReturn(returnPort, amuletReturnState, returnTimeoutSec);
            }).detach();

            const char* cmdEnv = std::getenv("HVE_AMULET_CMD");
            std::string cmd;
            if (cmdEnv && *cmdEnv) {
                cmd = cmdEnv;
            } else {
                const std::filesystem::path venvPy = std::filesystem::path(".venv") / "Scripts" / "python.exe";
                if (std::filesystem::exists(venvPy)) {
                    std::string venvCmd = venvPy.string();
                    if (venvCmd.find(' ') != std::string::npos) {
                        venvCmd = "\"" + venvCmd + "\"";
                    }
                    cmd = venvCmd + " scripts/amulet_bridge/amulet_bridge_launcher.py";
                } else {
                    cmd = "python scripts/amulet_bridge/amulet_bridge_launcher.py";
                }
            }
            const char* hostEnv = std::getenv("HVE_AMULET_HOST");
            const std::string host = (hostEnv && *hostEnv) ? std::string(hostEnv) : std::string("127.0.0.1");
            const uint16_t port = (uint16_t)GetEnvIntClamped("HVE_AMULET_PORT", 49632, 1024, 65535);
            const int connectTimeoutMs = GetEnvIntClamped("HVE_AMULET_CONNECT_TIMEOUT_MS", 3000, 100, 30000);
            const int connectRetryMs = GetEnvIntClamped("HVE_AMULET_CONNECT_RETRY_MS", 120, 10, 1000);
            const char* dimEnv = std::getenv("HVE_AMULET_DIM");
            const std::string dimension = (dimEnv && *dimEnv) ? std::string(dimEnv) : std::string("minecraft:overworld");

            std::error_code absEc;
            const std::string worldPath = std::filesystem::absolute(worldFile, absEc).generic_string();
            const std::string safeWorldPath = absEc ? worldFile : worldPath;

            std::ostringstream payload;
            payload.setf(std::ios::fixed);
            payload.precision(6);
            payload << "{"
                    << "\"world_path\":\"" << EscapeJsonString(safeWorldPath) << "\","
                    << "\"player\":{"
                    << "\"x\":" << camera.Position.x << ","
                    << "\"y\":" << camera.Position.y << ","
                    << "\"z\":" << camera.Position.z << "},"
                    << "\"camera\":{"
                    << "\"pitch\":" << camera.Pitch << ","
                    << "\"yaw\":" << camera.Yaw << "},"
                    << "\"dimension\":\"" << EscapeJsonString(dimension) << "\","
                    << "\"return_port\":" << returnPort
                    << "}";

            std::string err;
            if (!StartDetachedProcess(cmd, err)) {
                amuletBridgeActive = false;
                amuletStatusText = "Amulet launch failed: " + err;
                addAmuletLog("Launch failed");
                std::cout << amuletStatusText << std::endl;
                menuOpen = false;
                mouseCaptured = true;
                Input::SetCursorMode(true);
                return;
            }

            if (!SendTcpPayload(host, port, payload.str(), connectTimeoutMs, connectRetryMs, err)) {
                amuletBridgeActive = false;
                amuletStatusText = "Amulet IPC failed: " + err;
                addAmuletLog("IPC failed");
                std::cout << amuletStatusText << std::endl;
                menuOpen = false;
                mouseCaptured = true;
                Input::SetCursorMode(true);
                return;
            }

            amuletPayloadSent = true;
            amuletStatusText = "Amulet running. Waiting for save...";
            addAmuletLog("Payload sent");
        };

        bool wasIconified = false;

        bool traceW = false;
        bool traceA = false;
        bool traceS = false;
        bool traceD = false;
        bool traceSpace = false;
        bool traceShift = false;
        bool traceEsc = false;
        bool traceTab = false;
        std::uint64_t impostorFrameCounter = 0;
        float fpsCurrent = 0.0f;
        int lastFbWidth = -1;
        int lastFbHeight = -1;
        double resizeGuardUntil = -1.0;

        auto logInputEdge = [&](const char* keyName, bool pressed, bool& lastState) {
            if (!traceInput) {
                lastState = pressed;
                return;
            }
            if (pressed != lastState) {
                RunLog::Info(std::string("INPUT ") + keyName + "=" + (pressed ? "down" : "up"));
                lastState = pressed;
            }
        };

        auto updateHorizonTerrain = [&](const glm::vec3& camPos, double now, int pointsBudgetPerLevel, bool force) {
            if (!horizonTerrainEnabled || horizonProg == 0 || horizonLevels.empty()) return;
            const int vertsPerAxis = horizonGridCells + 1;
            const int totalPoints = vertsPerAxis * vertsPerAxis;
            if (totalPoints <= 0) return;

            const int baseBudget = std::max(64, pointsBudgetPerLevel);

            for (std::size_t li = 0; li < horizonLevels.size(); ++li) {
                HorizonLevel& level = horizonLevels[li];
                if (level.vao == 0 || level.vbo == 0 || level.verts.empty()) continue;

                const float snapStep = std::max(0.001f, level.snapStep);
                const float snappedX = std::floor(camPos.x / snapStep) * snapStep;
                const float snappedZ = std::floor(camPos.z / snapStep) * snapStep;
                const bool centerChanged = !std::isfinite(level.centerX) || !std::isfinite(level.centerZ) ||
                                           std::abs(snappedX - level.centerX) > 0.001f ||
                                           std::abs(snappedZ - level.centerZ) > 0.001f;

                if (centerChanged) {
                    level.centerX = snappedX;
                    level.centerZ = snappedZ;
                    level.updateCursor = 0;
                    level.ready = false;
                }

                if (!force && !centerChanged && level.ready) continue;
                if (!force && horizonUpdateMinSec > 0.0f) {
                    // stagger deeper levels by requiring movement and natural cadence
                    // (near levels refresh quickly because snapStep is smaller).
                    const double frac = (double)(li + 1) * 0.25;
                    if (std::fmod(now, (double)horizonUpdateMinSec + frac) < (double)horizonUpdateMinSec * 0.25) {
                        // keep cadence smooth without burst updates
                    }
                }

                const int levelBudget = force ? totalPoints : std::min(totalPoints, baseBudget / std::max(1, (int)li + 1));
                if (levelBudget <= 0) continue;

                const float halfSpan = 0.5f * (float)horizonGridCells * level.cellSize;
                const int start = level.updateCursor;
                const int count = std::min(levelBudget, totalPoints);

                for (int i = 0; i < count; ++i) {
                    const int idx = (start + i) % totalPoints;
                    const int gx = idx % vertsPerAxis;
                    const int gz = idx / vertsPerAxis;
                    const float x = level.centerX - halfSpan + (float)gx * level.cellSize;
                    const float z = level.centerZ - halfSpan + (float)gz * level.cellSize;
                    const int sx = (int)std::floor(x);
                    const int sz = (int)std::floor(z);
                    const float y = (float)Game::World::Generation::GetBaseSurfaceYAtWorld(sx, sz) + horizonYOffset;

                    const std::size_t w = (std::size_t)idx * 3u;
                    level.verts[w + 0] = x;
                    level.verts[w + 1] = y;
                    level.verts[w + 2] = z;
                }

                glBindBuffer(GL_ARRAY_BUFFER, level.vbo);
                if (start + count <= totalPoints) {
                    const std::size_t off = (std::size_t)start * 3u;
                    glBufferSubData(GL_ARRAY_BUFFER,
                                    (GLintptr)(off * sizeof(float)),
                                    (GLsizeiptr)((std::size_t)count * 3u * sizeof(float)),
                                    level.verts.data() + off);
                } else {
                    const int firstCount = totalPoints - start;
                    const int secondCount = count - firstCount;
                    const std::size_t offA = (std::size_t)start * 3u;
                    glBufferSubData(GL_ARRAY_BUFFER,
                                    (GLintptr)(offA * sizeof(float)),
                                    (GLsizeiptr)((std::size_t)firstCount * 3u * sizeof(float)),
                                    level.verts.data() + offA);
                    glBufferSubData(GL_ARRAY_BUFFER,
                                    0,
                                    (GLsizeiptr)((std::size_t)secondCount * 3u * sizeof(float)),
                                    level.verts.data());
                }
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                level.updateCursor = (start + count) % totalPoints;
                if (level.updateCursor == 0) {
                    level.ready = true;
                }
            }
        };

        while (!Window::ShouldClose()) {
            // Poll events first so mouse/keyboard state is current this frame.
            Window::Update();
            Telemetry::PumpReplay(glfwGetTime());

            GLFWwindow* nativeWindow = Window::GetNativeWindow();
            const bool iconified = (nativeWindow != nullptr) && (glfwGetWindowAttrib(nativeWindow, GLFW_ICONIFIED) == GLFW_TRUE);
            if (iconified) {
                // Keep app responsive while minimized: no heavy streaming/upload/render work.
                gameTime.BeginFrame(glfwGetTime());
                wasIconified = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            if (wasIconified) {
                // Drop one frame worth of timing so restore doesn't trigger a catch-up spike.
                gameTime.BeginFrame(glfwGetTime());
                wasIconified = false;
            }

            const double frameStartMs = glfwGetTime() * 1000.0;
            const double frameNow = glfwGetTime();

            int fbWidth = 0;
            int fbHeight = 0;
            if (nativeWindow) {
                glfwGetFramebufferSize(nativeWindow, &fbWidth, &fbHeight);
            }
            if (fbWidth <= 0 || fbHeight <= 0) {
                gameTime.BeginFrame(frameNow);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            if (showLoading) {
                loadingProgress += 0.012f;
                if (loadingProgress >= 1.0f) {
                    loadingProgress = 1.0f;
                    showLoading = false;
                    worldStarted = true;
                    menuOpen = false;
                    showLoadingOverlay = true;
                    menuScreen = MenuScreen::Loading;
                    preloadDone = false;
                    preloadStartTime = frameNow;
                    mouseCaptured = false;
                    Input::SetCursorMode(false);
                    const int bootHeightChunks = std::min(heightChunks, std::max(8, preloadHeightChunks));
                    chunkManager.UpdateStreaming(camera.Position, streamDistanceChunks, bootHeightChunks);
                }

                loadingStatus = "Generiere " + Game::World::g_TerrainPresets[std::clamp(Game::World::g_CurrentPresetIndex, 0, 7)].name + "... " + std::to_string((int)(loadingProgress * 100.0f)) + "%";
                Window::SetTitle(loadingStatus);

                Renderer::SetClearColor({0.01f, 0.03f, 0.07f, 1.0f});
                Renderer::Clear();
                Window::SwapBuffers();
                continue;
            }

            if (fbWidth != lastFbWidth || fbHeight != lastFbHeight) {
                lastFbWidth = fbWidth;
                lastFbHeight = fbHeight;
                resizeGuardUntil = frameNow + 0.25;
                if (traceRuntime) {
                    RunLog::Info("WINDOW resize fb=" + std::to_string(fbWidth) + "x" + std::to_string(fbHeight));
                }
            }
            const bool resizeGuardActive = frameNow < resizeGuardUntil;

            const double rawFrameDelta = gameTime.BeginFrame(frameNow);

            const bool immediateMoveKeyDown =
                Input::IsKeyPressed(keyForward) ||
                Input::IsKeyPressed(keyBack) ||
                Input::IsKeyPressed(keyLeft) ||
                Input::IsKeyPressed(keyRight) ||
                Input::IsKeyPressed(keyUp) ||
                Input::IsKeyPressed(keyDown);
            const int baseFixedStepCap = (fixedStepRecoveryFrames > 0) ? recoveryFixedSteps : maxFixedStepsPerFrame;
            const int fixedStepCapThisFrame = immediateMoveKeyDown
                ? std::min(baseFixedStepCap, fixedStepsWhileMoving)
                : baseFixedStepCap;
            int fixedStepCount = 0;
            while (gameTime.ConsumeStep() && fixedStepCount < fixedStepCapThisFrame) {
                fixedStepCount++;
            }
            const double fixedStepDebtThreshold = gameTime.GetFixedDelta() * (double)std::max(1, fixedSatDebtSteps);
            const bool fixedStepSaturated =
                (fixedStepCount >= fixedStepCapThisFrame) &&
                (gameTime.GetAccumulator() >= fixedStepDebtThreshold);
            if (fixedStepSaturated) {
                fixedStepSaturatedFrames++;
                if (recoveryHoldFrames > 0) {
                    fixedStepRecoveryFrames = recoveryHoldFrames;
                }
            } else if (fixedStepRecoveryFrames > 0) {
                --fixedStepRecoveryFrames;
            }

            const double accumulator = gameTime.GetAccumulator();
            const double alpha = gameTime.GetAlpha();

            float deltaTime = (fixedStepCount > 0)
                ? (float)(gameTime.GetFixedDelta() * (double)fixedStepCount)
                : (float)rawFrameDelta;
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            float inputDeltaTime = (float)rawFrameDelta;
            if (inputDeltaTime < 0.0f) inputDeltaTime = 0.0f;
            if (inputDeltaTime > 0.05f) inputDeltaTime = 0.05f;
            const float frameMsForSpike = deltaTime * 1000.0f;
            if (spikeGuardEnabled) {
                int samples = std::max(1, spikeWindowCount);
                double sum = 0.0;
                for (int i = 0; i < samples; ++i) {
                    sum += spikeWindow[(size_t)i];
                }
                const double mean = sum / (double)samples;
                double var = 0.0;
                for (int i = 0; i < samples; ++i) {
                    const double d = (double)spikeWindow[(size_t)i] - mean;
                    var += d * d;
                }
                const double sigma = std::sqrt(var / (double)samples);
                const double overBase = (double)frameMsForSpike - mean;
                const bool enoughHistory = spikeWindowCount >= 48;
                const bool zTriggered = enoughHistory && sigma > 0.001 && ((overBase / sigma) >= (double)spikeZScore);
                const bool minorTriggered = enoughHistory && overBase >= (double)spikeMinorMs;
                const bool majorTriggered = enoughHistory && overBase >= (double)spikeMajorMs;

                if (zTriggered || minorTriggered) {
                    spikeMinorCount++;
                    spikeLastOverBaselineMs = (float)std::max(0.0, overBase);
                    if (majorTriggered) spikeMajorCount++;
                    lastSpikeEventTime = frameNow;

                    if (spikeMinorCount >= 3 || spikeMajorCount >= 1) {
                        spikeLevel = std::min(3, spikeLevel + (majorTriggered ? 2 : 1));
                        spikeStateUntil = frameNow + (double)spikeHoldSec;
                        spikeMinorCount = 0;
                        spikeMajorCount = 0;

                        if (spikeLogEnabled) {
                            std::ostringstream ss;
                            ss.setf(std::ios::fixed);
                            ss.precision(2);
                            ss << "SPIKE level=" << spikeLevel
                               << " frameMs=" << frameMsForSpike
                               << " baselineMs=" << mean
                               << " overMs=" << overBase
                               << " sigma=" << sigma
                               << " chunks=" << worldRenderer.GetChunkCount()
                               << " inFlightGen=" << chunkManager.GetInFlightGenerate()
                               << " inFlightRemesh=" << chunkManager.GetInFlightRemesh()
                               << " deferred=" << chunkManager.GetDeferredRemeshCount();
                            AppendSpikeLog(ss.str());
                            RunLog::Warn(ss.str());
                        }
                    }
                }

                if (spikeLevel > 0 && frameNow > spikeStateUntil) {
                    if (lastSpikeEventTime > 0.0 && (frameNow - lastSpikeEventTime) >= (double)spikeRecoverSec) {
                        spikeLevel = std::max(0, spikeLevel - 1);
                        spikeStateUntil = frameNow + (double)spikeHoldSec;
                    }
                }

                spikeWindow[(size_t)spikeWindowHead] = frameMsForSpike;
                spikeWindowHead = (spikeWindowHead + 1) % (int)spikeWindow.size();
                spikeWindowCount = std::min((int)spikeWindow.size(), spikeWindowCount + 1);
            }
            const double elapsedMs = glfwGetTime() * 1000.0 - frameStartMs;
            double budgetLeft = eliteFrameBudgetMs - elapsedMs;
            if (budgetLeft > 0.0 && !resizeGuardActive) {
                const bool fastInputTimesliceNow = fastInputPriorityEnabled && (
                    immediateMoveKeyDown
                    || Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)
                    || Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)
                );
                const double timesliceFactor = fastInputTimesliceNow
                    ? (double)fastInputTimesliceFactor
                    : 0.25;
                timeSlicedJobs.RunForBudgetMs(budgetLeft * std::clamp(timesliceFactor, 0.0, 0.50));
            }

            const float frameMs = deltaTime * 1000.0f;
            const float fpsNow = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
            fpsCurrent = fpsNow;
            if (horizonMode) {
                Engine::QualityManager::Update(fpsNow, camera.Position);
            }
            if (stableFpsEma <= 0.0f) stableFpsEma = fpsNow;
            stableFpsEma += (fpsNow - stableFpsEma) * 0.12f;
            maxSpikeWindowMs = std::max(maxSpikeWindowMs, frameMs);

            // FPS + stats title update (once per second)
            fpsAccumTime += (double)deltaTime;
            fpsFrames += 1;
            const double now = frameNow;

            logInputEdge("W", Input::IsKeyPressed(keyForward), traceW);
            logInputEdge("A", Input::IsKeyPressed(keyLeft), traceA);
            logInputEdge("S", Input::IsKeyPressed(keyBack), traceS);
            logInputEdge("D", Input::IsKeyPressed(keyRight), traceD);
            logInputEdge("SPACE", Input::IsKeyPressed(GLFW_KEY_SPACE), traceSpace);
            logInputEdge("LSHIFT", Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT), traceShift);
            logInputEdge("ESC", Input::IsKeyPressed(GLFW_KEY_ESCAPE), traceEsc);
            logInputEdge("TAB", Input::IsKeyPressed(GLFW_KEY_TAB), traceTab);

            static const double startTime = (double)glfwGetTime();
            const bool inputArmed = (now - startTime) >= (double)inputGraceSec;

            const bool asyncLoading = chunkManager.IsAsyncLoadInProgress();

            if (!worldStarted) {
                instantHugeQueued = false;
                megaPreloadRadiusCurrent = instantHugeRadius;
                megaPreloadVerticalCurrent = instantHugeVertical;
                megaPreloadLastPulse = -1.0;
                startupPreloadGateReleasedLatched = false;
                startupBlockLastChunks = 0;
                startupBlockLastProgressTime = now;
            }

            if (instantHugeSight && worldStarted && !loadingWorld && !asyncLoading && !instantHugeQueued) {
                if (instantHugeReposition && !cameraAutoPlaced) {
                    camera.Position = glm::vec3(512.0f, 128.0f, 512.0f);
                    camera.updateCameraVectors();
                }
                chunkManager.PreloadLargeArea(camera.Position, instantHugeRadius, instantHugeVertical);
                instantHugeQueued = true;
                preloadDone = false;
                preloadStartTime = now;
                megaPreloadLastPulse = now;
            }

            if (megaPreloadContinuous && worldStarted && !loadingWorld && !asyncLoading && !preloadDone && showLoadingOverlay) {
                if (megaPreloadLastPulse < 0.0 || (now - megaPreloadLastPulse) >= (double)megaPreloadPulseSec) {
                    chunkManager.PreloadLargeArea(camera.Position, megaPreloadRadiusCurrent, megaPreloadVerticalCurrent);
                    megaPreloadRadiusCurrent = std::min(megaPreloadRadiusMax, megaPreloadRadiusCurrent + megaPreloadRadiusStep);
                    megaPreloadVerticalCurrent = std::min(megaPreloadVerticalMax, megaPreloadVerticalCurrent + megaPreloadVerticalStep);
                    megaPreloadLastPulse = now;
                    if (traceRuntime) {
                        RunLog::Info("PRELOAD pulse radius=" + std::to_string(megaPreloadRadiusCurrent) +
                                     " vertical=" + std::to_string(megaPreloadVerticalCurrent));
                    }
                }
            }
            bool menuVisible = menuOpen || showLoadingOverlay;
            bool menuBlocksInput = menuVisible;
            bool builderPanelEnabled = visualBuildEnabled && !editorEnabled;

            // Preload near terrain before enabling movement (prevents "generation popping" at start).
            if (!preloadDone) {
                const int have = worldRenderer.GetChunkCount();
                const double elapsed = now - preloadStartTime;
                if ((preloadTargetChunks > 0 && have >= preloadTargetChunks) || (preloadMaxSec > 0.0f && elapsed >= (double)preloadMaxSec)) {
                    preloadDone = true;
                }
            }

            const int startupBlockTargetChunks = (preloadTargetChunks > 0)
                ? std::clamp(startupBlockMinChunks, 0, preloadTargetChunks)
                : 0;
            const int startupBlockHave = worldRenderer.GetChunkCount();
            const double startupBlockElapsed = now - preloadStartTime;
            if (startupBlockHave > startupBlockLastChunks) {
                startupBlockLastChunks = startupBlockHave;
                startupBlockLastProgressTime = now;
            }
            const bool startupBlockStalled = (now - startupBlockLastProgressTime) >= (double)startupBlockStallSec;
            const bool startupPreloadGateActive = startupForcePreload
                && worldStarted
                && !loadingWorld
                && !asyncLoading
                && !preloadDone
                && (startupBlockTargetChunks > 0);
            const bool startupPreloadGateReleasedRaw = !startupPreloadGateActive
                || (startupBlockHave >= startupBlockTargetChunks)
                || (startupBlockMaxSec > 0.0f && startupBlockElapsed >= (double)startupBlockMaxSec)
                || startupBlockStalled;
            if (startupPreloadGateReleasedRaw) {
                startupPreloadGateReleasedLatched = true;
            }
            const bool startupPreloadGateReleased = startupPreloadGateReleasedLatched || startupPreloadGateReleasedRaw;
            const bool startupPreloadGateHold = startupPreloadGateActive && !startupPreloadGateReleased;

            if (worldStarted && !loadingWorld && !asyncLoading && !preloadDone && (startupLoadingOverlay || startupPreloadGateHold) && !loadingOverlayDismissedByInput) {
                showLoadingOverlay = true;
                menuOpen = false;
                menuScreen = MenuScreen::Loading;
                if (mouseCaptured) {
                    mouseCaptured = false;
                    Input::SetCursorMode(false);
                }
            } else if (showLoadingOverlay && !loadingWorld && !asyncLoading && (preloadDone || (!startupLoadingOverlay && !startupPreloadGateHold))) {
                showLoadingOverlay = false;
                menuScreen = MenuScreen::Main;
                if (worldStarted && !menuOpen && !mouseCaptured) {
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                }
            }

            if (loadingInputBreakout && startupPreloadGateReleased && worldStarted && showLoadingOverlay && !loadingWorld && !asyncLoading && inputArmed) {
                const bool breakoutInput =
                    Input::IsKeyPressed(keyForward) || Input::IsKeyPressed(keyBack) ||
                    Input::IsKeyPressed(keyLeft) || Input::IsKeyPressed(keyRight) ||
                    Input::IsKeyPressed(keyUp) || Input::IsKeyPressed(keyDown) ||
                    Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) ||
                    Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
                if (breakoutInput) {
                    showLoadingOverlay = false;
                    menuOpen = false;
                    preloadBlocksInput = false;
                    loadingOverlayDismissedByInput = true;
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                    if (traceRuntime) {
                        RunLog::Info("INPUT breakout: loading overlay dismissed by gameplay input");
                    }
                }
            }

            if (startupPreloadGateReleased && worldStarted && showLoadingOverlay && !loadingWorld && !asyncLoading && loadingOverlayMaxSec > 0.0f) {
                const double overlayElapsed = now - preloadStartTime;
                if (overlayElapsed >= (double)loadingOverlayMaxSec) {
                    showLoadingOverlay = false;
                    menuOpen = false;
                    preloadBlocksInput = false;
                    loadingOverlayDismissedByInput = true;
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                    if (traceRuntime) {
                        RunLog::Info("INPUT breakout: loading overlay timeout fallback");
                    }
                }
            }

            // Defensive startup: if menu was closed without starting the world, auto-start streaming.
            if (!worldStarted && !menuVisible && !loadingWorld && !asyncLoading) {
                worldStarted = true;
                preloadDone = (preloadRadiusChunks <= 0);
                preloadStartTime = now;
                cameraAutoPlaced = false;
            }

            const bool wantsStartByInput = inputArmed && menuOpen && !showLoadingOverlay && !loadingWorld && !asyncLoading &&
                (Input::IsKeyPressed(keyForward) ||
                 Input::IsKeyPressed(keyBack) ||
                 Input::IsKeyPressed(keyLeft) ||
                 Input::IsKeyPressed(keyRight) ||
                 Input::IsKeyPressed(GLFW_KEY_SPACE) ||
                 Input::IsMouseButtonPressed(0) ||
                 Input::IsMouseButtonPressed(1));

            if (!worldStarted && wantsStartByInput) {
                worldStarted = true;
                menuOpen = false;
                showLoadingOverlay = false;
                preloadDone = (preloadRadiusChunks <= 0);
                preloadStartTime = now;
                cameraAutoPlaced = false;
                mouseCaptured = true;
                Input::SetCursorMode(true);
                if (traceRuntime) {
                    RunLog::Info("AUTO_START triggered by gameplay input");
                }
            }

            // Re-evaluate visibility flags after possible state changes above.
            menuVisible = menuOpen || showLoadingOverlay;
            menuBlocksInput = menuVisible;
            builderPanelEnabled = visualBuildEnabled && !editorEnabled;

            const bool startupInputUnlocked = startupInputHardUnlock
                && worldStarted
                && ((now - startTime) >= (double)startupInputUnlockSec);

            if (startupInputUnlocked) {
                if (menuOpen || showLoadingOverlay) {
                    menuOpen = false;
                    showLoadingOverlay = false;
                    menuVisible = false;
                    menuBlocksInput = false;
                }
                preloadBlocksInput = false;
                if (!mouseCaptured) {
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                }
            }

            // Hard-sync cursor lock state every frame (fixes cursor escaping window on focus/resize transitions).
            if (strictMouseCapture && nativeWindow != nullptr) {
                const bool windowFocused = glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) == GLFW_TRUE;
                const bool shouldCaptureNow = worldStarted && !menuVisible && !inventoryOpen && windowFocused;
                const int cursorMode = glfwGetInputMode(nativeWindow, GLFW_CURSOR);
                const bool cursorLocked = (cursorMode == GLFW_CURSOR_DISABLED);

                if (shouldCaptureNow) {
                    if (!mouseCaptured || !cursorLocked) {
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                        if (traceRuntime) RunLog::Info("MOUSE recaptured");
                    }
                } else if (!windowFocused && (mouseCaptured || cursorLocked)) {
                    mouseCaptured = false;
                    Input::SetCursorMode(false);
                    if (traceRuntime) RunLog::Info("MOUSE released (focus lost)");
                }
            }

            const bool gameplayArmed = inputArmed && worldStarted && (!menuBlocksInput || startupInputUnlocked) && ((!preloadBlocksInput || preloadDone) || startupInputUnlocked);
            const bool fastInputDemandNow = gameplayArmed && (
                immediateMoveKeyDown
                || Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)
                || Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)
            );
            const bool fastInputLaggingNow = (controlLagMsEma >= fastInputLagWarnMs) || (controlMoveResponseEma <= fastInputResponseMin);
            if (fastInputPriorityEnabled && (fastInputDemandNow || (controlIntentEma >= fastInputIntentThreshold && fastInputLaggingNow))) {
                fastInputPriorityUntil = std::max(fastInputPriorityUntil, now + (double)fastInputHoldSec);
            }
            const bool fastInputPriorityActive = fastInputPriorityEnabled && gameplayArmed && (fastInputPriorityUntil > now);

            if (mouseCaptured != lastMouseCapturedState) {
                Input::ResetRuntimeInputState(true);
                if (mouseCaptured) {
                    captureGuardUntil = now + 0.15;
                    if (traceRuntime) RunLog::Info("INPUT capture guard active");
                } else {
                    captureGuardUntil = -1.0;
                    if (traceRuntime) RunLog::Info("INPUT capture released");
                }
                lastMouseCapturedState = mouseCaptured;
            }
            const bool captureGuardActive = mouseCaptured && (captureGuardUntil > now);

            if (erosionComputeEnabled && erosionComputeRuntimeReady && erosionComputeShader && erosionComputeShader->GetID() != 0 &&
                erosionHeightTexA != 0 && erosionHeightTexB != 0 &&
                erosionSedTexA != 0 && erosionSedTexB != 0 &&
                worldStarted && !menuVisible && preloadDone &&
                (lastErosionDispatch < 0.0 || (now - lastErosionDispatch) >= (double)erosionDispatchSec)) {

                const int originX = (int)std::floor(camera.Position.x) - erosionWorldSpan / 2;
                const int originZ = (int)std::floor(camera.Position.z) - erosionWorldSpan / 2;
                for (int z = 0; z < erosionGrid; ++z) {
                    for (int x = 0; x < erosionGrid; ++x) {
                        const int worldX = originX + x * erosionSampleStep;
                        const int worldZ = originZ + z * erosionSampleStep;
                        erosionBase[(size_t)z * (size_t)erosionGrid + (size_t)x] = (float)Game::World::Generation::GetBaseSurfaceYAtWorld(worldX, worldZ);
                    }
                }

                std::fill(erosionSed.begin(), erosionSed.end(), 0.0f);

                glBindTexture(GL_TEXTURE_2D, erosionHeightTexA);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, erosionGrid, erosionGrid, GL_RED, GL_FLOAT, erosionBase.data());
                glBindTexture(GL_TEXTURE_2D, erosionSedTexA);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, erosionGrid, erosionGrid, GL_RED, GL_FLOAT, erosionSed.data());

                GLuint heightIn = erosionHeightTexA;
                GLuint heightOut = erosionHeightTexB;
                GLuint sedIn = erosionSedTexA;
                GLuint sedOut = erosionSedTexB;

                erosionComputeShader->Use();
                glUniform2i(glGetUniformLocation(erosionComputeShader->GetID(), "u_Dimensions"), erosionGrid, erosionGrid);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_DeltaTime"), 0.016f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_ErosionRate"), 0.12f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_DepositionRate"), 0.38f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_CapacityFactor"), 4.2f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_ThermalRate"), 0.045f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_TalusHighDeg"), 45.0f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_TalusTargetDeg"), 32.0f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_CellSize"), (float)erosionSampleStep);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_WindSpeed"), 0.16f);
                glUniform1f(glGetUniformLocation(erosionComputeShader->GetID(), "u_WindStrength"), 0.03f);

                const GLuint groupsX = (GLuint)((erosionGrid + 15) / 16);
                const GLuint groupsY = (GLuint)((erosionGrid + 15) / 16);
                for (int pass = 0; pass < erosionPasses; ++pass) {
                    pBindImageTexture(0, heightIn, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
                    pBindImageTexture(1, sedIn, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
                    pBindImageTexture(2, heightOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
                    pBindImageTexture(3, sedOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
                    pBindImageTexture(4, erosionVegTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);

                    pDispatchCompute(groupsX, groupsY, 1);
                    // glFinish() caused freeze; use barrier instead
                    if (pMemoryBarrier) {
                        pMemoryBarrier(0x00000020); // GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                    }

                    std::swap(heightIn, heightOut);
                    std::swap(sedIn, sedOut);
                }

                glBindTexture(GL_TEXTURE_2D, heightIn);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, erosionOut.data());
                glBindTexture(GL_TEXTURE_2D, 0);

                for (std::size_t i = 0; i < erosionOut.size(); ++i) {
                    const float d = erosionOut[i] - erosionBase[i];
                    erosionDelta[i] = std::clamp(d, -32.0f, 32.0f);
                }

                Game::World::Generation::SetErosionHeightDeltaPatch(
                    originX,
                    originZ,
                    erosionGrid,
                    erosionGrid,
                    erosionSampleStep,
                    erosionDelta,
                    erosionStrength
                );

                lastErosionDispatch = now;
            }

            if (autosaveEnabled && worldStarted && !menuVisible && !loadingWorld && !asyncLoading) {
                if ((now - lastAutosaveTime) >= autosaveSec) {
                    if (saveCurrentWorld("AutoSave")) {
                        lastAutosaveTime = now;
                    } else {
                        // Backoff on failures to avoid trying every frame.
                        lastAutosaveTime = now - autosaveSec + 20.0;
                    }
                }
            }

            if (showStatsInTitle && now - lastFpsUpdate >= 1.0) {
                const double fpsSec = (fpsAccumTime > 0.0) ? (double)fpsFrames / fpsAccumTime : 0.0;
                fpsAccumTime = 0.0;
                fpsFrames = 0;
                lastFpsUpdate = now;
                lowEndDownshiftEventsSec = lowEndDownshiftEvents;
                lowEndUpshiftEventsSec = lowEndUpshiftEvents;
                fixedStepSaturatedFramesSec = fixedStepSaturatedFrames;
                lowEndDownshiftEvents = 0;
                lowEndUpshiftEvents = 0;
                fixedStepSaturatedFrames = 0;
                const Engine::EliteTelemetrySnapshot elite = Telemetry::GetEliteMetrics();
                coverageMetricLive = coverageMetricEnabled
                    && worldStarted
                    && !showLoadingOverlay
                    && (worldRenderer.GetChunkCount() >= 24);
                if (coverageMetricLive) {
                    coverageMisses = (int)chunkManager.EstimateSurfaceCoverageMisses(
                        camera.Position,
                        coverageRadiusChunks,
                        coverageStrideChunks,
                        heightChunks
                    );
                } else {
                    coverageMisses = 0;
                }

                const std::size_t inFlightGen = chunkManager.GetInFlightGenerate();
                const std::size_t inFlightRemesh = chunkManager.GetInFlightRemesh();
                const std::size_t completedCount = chunkManager.GetCompletedCount();
                const auto streamHealth = chunkManager.GetStreamHealthStats(stutterAlertPendingOldTicks);
                streamPendingNotReady = streamHealth.pendingNotReady;
                streamWatchdogEligible = streamHealth.watchdogEligible;
                streamWatchdogOverdue = streamHealth.watchdogOverdue;
                streamPendingOldestTicks = (int)std::min<std::uint64_t>(streamHealth.oldestPendingTicks, 1000000ull);
                streamOverdueOldestTicks = (int)std::min<std::uint64_t>(streamHealth.oldestOverdueTicks, 1000000ull);

                const float fpsPenalty = std::clamp((stutterAlertMinFps - (float)fpsSec) / std::max(1.0f, stutterAlertMinFps), 0.0f, 2.0f) * 40.0f;
                const float fixedPenalty = (stutterAlertFixedSatSec <= 0)
                    ? 0.0f
                    : std::clamp((float)fixedStepSaturatedFramesSec / (float)std::max(1, stutterAlertFixedSatSec), 0.0f, 2.0f) * 15.0f;
                const float pendingPenalty = std::clamp((float)streamPendingOldestTicks / (float)std::max(1, stutterAlertPendingOldTicks), 0.0f, 2.0f) * 20.0f;
                const float overduePenalty = std::clamp((float)streamWatchdogOverdue / (float)std::max(1, stutterAlertOverdueChunks), 0.0f, 2.0f) * 15.0f;
                const float inFlightPenalty = std::clamp((float)inFlightGen / (float)std::max(1, stutterAlertInFlightGen), 0.0f, 2.0f) * 10.0f;
                const float coveragePenalty = (coverageAlarmEnabled && coverageAlarmActive) ? 8.0f : 0.0f;
                const float latencyRaw = fpsPenalty + fixedPenalty + pendingPenalty + overduePenalty + inFlightPenalty + coveragePenalty;
                stutterLatencyScore = std::clamp((int)std::lround(latencyRaw), 0, 100);
                controlQualityScoreSec = std::clamp((int)std::lround(std::clamp(controlMoveResponseEma, 0.0f, 1.2f) * 100.0f), 0, 120);
                controlLagMsSec = std::clamp((int)std::lround(std::max(controlLagMsEma, 0.0f)), 0, 500);
                controlPlaceOpsSec = controlPlaceOpsCounter;
                controlBreakOpsSec = controlBreakOpsCounter;
                controlRotDegPerSecSec = std::max(0.0f, controlRotDegPerSecEma);
                controlWASDHoldSec = std::max(std::max(controlHoldForwardSec, controlHoldBackSec), std::max(controlHoldLeftSec, controlHoldRightSec));
                controlLmbHoldSec = controlHoldLmbSec;
                controlRmbHoldSec = controlHoldRmbSec;
                controlMoveDelayMsSec = std::max(controlMoveDelayMsMaxSec, controlMoveDelayMsLast);
                if (controlMoveDelayMsSec < controlDelayReportMinMs) {
                    controlMoveDelayMsSec = 0;
                }
                controlMoveDelayMsMaxSec = 0;
                controlPlaceOpsCounter = 0;
                controlBreakOpsCounter = 0;
                controlDistForwardMetersSec = controlDistForwardMetersCounter;
                controlDistBackMetersSec = controlDistBackMetersCounter;
                controlDistLeftMetersSec = controlDistLeftMetersCounter;
                controlDistRightMetersSec = controlDistRightMetersCounter;
                controlDistForwardMetersCounter = 0.0f;
                controlDistBackMetersCounter = 0.0f;
                controlDistLeftMetersCounter = 0.0f;
                controlDistRightMetersCounter = 0.0f;

                const bool fpsLow = fpsSec < stutterAlertMinFps;
                const bool pendingOld = streamPendingOldestTicks >= stutterAlertPendingOldTicks;
                const bool overdueBad = (int)streamWatchdogOverdue >= stutterAlertOverdueChunks;
                const bool inFlightBad = (int)inFlightGen >= stutterAlertInFlightGen && completedCount == 0;
                const bool stutterAlertLive = worldStarted && !showLoadingOverlay && worldRenderer.GetChunkCount() >= 24;
                const bool controlBad = controlObsEnabled
                    && controlIntentEma > 0.25f
                    && (controlLagMsSec >= (int)std::lround(controlLagWarnMs)
                        || (controlQualityScoreSec < (int)std::lround(controlResponseThreshold * 100.0f)));
                const int fixedSatLatencyGate = std::max(stutterAlertLatencyWarn, stutterAlertFixedSatMinLatency);
                const bool fixedSatSevereByLatency = (stutterLatencyScore >= fixedSatLatencyGate) && (fpsLow || pendingOld || overdueBad || inFlightBad);
                const bool fixedSatNoThroughput = completedCount == 0 && inFlightGen > 0;
                const bool fixedSatBad =
                    (stutterAlertFixedSatSec > 0)
                    && (fixedStepSaturatedFramesSec >= stutterAlertFixedSatSec)
                    && (fpsLow || pendingOld || overdueBad || inFlightBad || fixedSatSevereByLatency || fixedSatNoThroughput);

                float topPenalty = fpsPenalty;
                stutterAlertCause = "FPS_LOW";
                if (fixedPenalty > topPenalty && fixedSatBad) {
                    topPenalty = fixedPenalty;
                    stutterAlertCause = "FIXED_STEP_OVERLOAD";
                }
                if (pendingPenalty > topPenalty) {
                    topPenalty = pendingPenalty;
                    stutterAlertCause = "STREAM_PENDING_OLD";
                }
                if (overduePenalty > topPenalty) {
                    topPenalty = overduePenalty;
                    stutterAlertCause = "WATCHDOG_OVERDUE";
                }
                if (inFlightPenalty > topPenalty) {
                    topPenalty = inFlightPenalty;
                    stutterAlertCause = "GEN_QUEUE_PRESSURE";
                }
                if (coveragePenalty > topPenalty) {
                    stutterAlertCause = "COVERAGE_ALARM";
                }
                if (controlBad && !fpsLow && !pendingOld && !overdueBad && !inFlightBad) {
                    stutterAlertCause = "CONTROL_RESPONSE_LAG";
                }

                const bool latencyWarnBad = (stutterLatencyScore >= stutterAlertLatencyWarn)
                    && (fpsLow || pendingOld || overdueBad || inFlightBad || controlBad);
                stutterAlertActive = stutterAlertsEnabled
                    && stutterAlertLive
                    && (fpsLow || fixedSatBad || pendingOld || overdueBad || inFlightBad || controlBad || latencyWarnBad);

                if (stutterAlertsEnabled && stutterAlertLive && stutterAlertActive && (stutterAlertCooldownUntil < 0.0 || now >= stutterAlertCooldownUntil)) {
                    std::ostringstream warn;
                    warn.setf(std::ios::fixed);
                    warn.precision(1);
                    warn << "ALERT cause=" << stutterAlertCause
                         << " latency=" << stutterLatencyScore
                        << " fps=" << fpsSec
                         << " fixedSat=" << fixedStepSaturatedFramesSec
                         << " pendOld=" << streamPendingOldestTicks
                         << " overdue=" << streamWatchdogOverdue
                         << " inFlightGen=" << inFlightGen
                         << " completed=" << completedCount
                        << " ctrlLagMs=" << controlLagMsSec
                        << " ctrlQ=" << controlQualityScoreSec
                         << " covAlarm=" << (coverageAlarmActive ? 1 : 0);
                    RunLog::Warn(warn.str());
                    stutterAlertCooldownUntil = now + stutterAlertCooldownSec;
                }

                if (traceRuntime && (traceLastSnapshot < 0.0 || (now - traceLastSnapshot) >= 1.0)) {
                    std::ostringstream trace;
                    trace.setf(std::ios::fixed);
                    trace.precision(1);
                    trace << "SNAP fps=" << fpsSec
                          << " chunks=" << worldRenderer.GetChunkCount()
                          << " tris=" << worldRenderer.GetLastTriangleCount()
                          << " draws=" << worldRenderer.GetLastDrawCount()
                          << " inFlightGen=" << inFlightGen
                          << " inFlightRemesh=" << inFlightRemesh
                          << " completed=" << completedCount
                          << " staleGen=" << chunkManager.GetStaleGenerateDropCount()
                          << " staleRemesh=" << chunkManager.GetStaleRemeshDropCount()
                          << " surfMiss=" << coverageMisses
                          << " covBoost=" << coverageBoostApplied
                          << " covAlarm=" << (coverageAlarmActive ? 1 : 0)
                          << " pend=" << streamPendingNotReady
                          << " pendOld=" << streamPendingOldestTicks
                          << " wdog=" << streamWatchdogEligible
                          << " overdue=" << streamWatchdogOverdue
                          << " overOld=" << streamOverdueOldestTicks
                          << " fixedSat=" << fixedStepSaturatedFramesSec
                          << " recFrames=" << fixedStepRecoveryFrames
                          << " latency=" << stutterLatencyScore
                          << " ctrlLagMs=" << controlLagMsSec
                          << " ctrlQ=" << controlQualityScoreSec
                          << " ctrlRot=" << controlRotDegPerSecSec
                          << " ctrlHoldWASD=" << controlWASDHoldSec
                          << " ctrlLMB=" << controlLmbHoldSec
                          << " ctrlRMB=" << controlRmbHoldSec
                          << " ctrlDelay=" << controlMoveDelayMsSec
                          << " distW=" << controlDistForwardMetersSec
                          << " distA=" << controlDistLeftMetersSec
                          << " distS=" << controlDistBackMetersSec
                          << " distD=" << controlDistRightMetersSec
                          << " placeS=" << controlPlaceOpsSec
                          << " breakS=" << controlBreakOpsSec
                          << " impQP=" << impostorQueuePressureLive
                          << " impQAvg=" << impostorQueuePressureAvgLive
                          << " impMul=" << impostorQueuePushMulLive
                          << " impNear=" << impostorBlendNearStartLive
                          << " impNearEnd=" << impostorBlendNearEndLive
                          << " alert=" << (stutterAlertActive ? 1 : 0)
                          << " cause=" << stutterAlertCause
                          << " menuOpen=" << (menuOpen ? 1 : 0)
                          << " loadingOverlay=" << (showLoadingOverlay ? 1 : 0)
                          << " worldStarted=" << (worldStarted ? 1 : 0)
                          << " mouseCaptured=" << (mouseCaptured ? 1 : 0);
                    RunLog::Info(trace.str());
                    RunLog::Flush();
                    traceLastSnapshot = now;
                }

                std::ostringstream ss;
                ss.setf(std::ios::fixed);
                ss.precision(1);
                ss << "High Performance Voxel Engine";
                ss << " | FPS " << fpsSec;
                ss << " | Chunks " << worldRenderer.GetChunkCount();
                ss << " | TSI " << elite.toasterStabilityIndex << "%";
                ss << " | Tris " << worldRenderer.GetLastTriangleCount();
                ss << " | Draws " << worldRenderer.GetLastDrawCount();
                ss << " | HG " << chunkManager.GetStaleGenerateDropCount() << "/" << chunkManager.GetStaleRemeshDropCount();
                if (coverageMetricEnabled) {
                    ss << " | SurfMiss " << coverageMisses;
                    if (coverageAlarmEnabled) {
                        ss << " | CBA " << coverageBoostApplied;
                    }
                }
                ss << " | FSat " << fixedStepSaturatedFramesSec;
                ss << " | LatR " << stutterLatencyScore;
                if (controlObsEnabled) {
                    ss << " | CtrlQ " << controlQualityScoreSec;
                    ss << " | CLag " << controlLagMsSec << "ms";
                }
                ss << " | ImpQ " << std::fixed << std::setprecision(2) << impostorQueuePressureLive;
                ss << " | ImpAvg " << std::fixed << std::setprecision(2) << impostorQueuePressureAvgLive;
                ss << " | ImpMul " << std::fixed << std::setprecision(2) << impostorQueuePushMulLive;
                if (stutterAlertActive) {
                    ss << " | ALERT " << stutterAlertCause;
                }
                ss << " | Block " << int(selectedBlockId);
                ss << " (" << GetBlockDisplayName((int)selectedBlockId) << ")";
                ss << " | MCPage " << g_MinecraftImportPage << "/" << GetMinecraftImportPageMax();
                if (lowEndController) {
                    ss << " | LQ " << lowEndLevel << "@" << std::fixed << std::setprecision(1) << lowEndEmaFastMs << "ms";
                }
                if (spikeGuardEnabled && spikeLevel > 0) {
                    ss << " | SpikeGuard L" << spikeLevel << " (" << std::fixed << std::setprecision(1) << spikeLastOverBaselineMs << "ms)";
                }
                if (!preloadDone) {
                    ss << " | Loading " << worldRenderer.GetChunkCount() << "/" << preloadTargetChunks;
                    if (startupForcePreload && startupBlockMinChunks > 0) {
                        const int startupGateTarget = (preloadTargetChunks > 0)
                            ? std::clamp(startupBlockMinChunks, 0, preloadTargetChunks)
                            : startupBlockMinChunks;
                        ss << " | StartGate " << std::min((int)worldRenderer.GetChunkCount(), startupGateTarget)
                           << "/" << startupGateTarget;
                    }
                }
                if (wireframe) ss << " | WF";
                if (vsync) ss << " | VSync";
                if (!mouseCaptured) ss << " | Mouse:free";
                Window::SetTitle(ss.str());

                Telemetry::RecordLiveState(now, camera.Position, camera.Yaw, camera.Pitch,
                                           (int)worldRenderer.GetChunkCount(),
                                           (int)worldRenderer.GetLastTriangleCount(),
                                           (int)worldRenderer.GetLastDrawCount(),
                                           (float)fpsSec);
            }

            // Clear color is a fallback background. If the sky shader fails to compile on a driver,
            // don't leave the world with the default green/teal clear.
            if (skyProg != 0) {
                Renderer::SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
            } else {
                Renderer::SetClearColor({0.35f, 0.72f, 1.00f, 1.0f});
            }
            Renderer::Clear();

            const int curW = std::max(1, fbWidth);
            const int curH = std::max(1, fbHeight);
            
            // Ensure viewport is correct every frame (fixes maximize resizing issues)
            glViewport(0, 0, curW, curH);

            if (curW != lastWindowWidth || curH != lastWindowHeight) {
                lastWindowWidth = curW;
                lastWindowHeight = curH;
                projection = glm::perspective(glm::radians(fovDeg), (float)curW / (float)curH, 0.1f, farClip);
            }

            // Input Processing
            const bool escDown = Input::IsKeyPressed(GLFW_KEY_ESCAPE);
            if (inputArmed && escDown && !lastEsc) {
                if (amuletBridgeActive) {
                    // Keep the Amulet bridge modal until the save handshake completes.
                } else if (editorEnabled && worldStarted && !menuVisible && !inventoryOpen) {
                    mouseCaptured = !mouseCaptured;
                    Input::SetCursorMode(mouseCaptured);
                    std::cout << (mouseCaptured ? "Mouse captured" : "Mouse released") << std::endl;
                } else if (menuVisible && menuScreen == MenuScreen::Controls && rebindActive) {
                    rebindActive = false;
                    rebindAction = -1;
                } else {
                if (inventoryOpen) {
                    inventoryOpen = false;
                    if (inventoryRestoreCapture) {
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                    }
                } else if (menuVisible) {
                    if (menuScreen != MenuScreen::Main) {
                        // Back out of sub-screens first.
                        if (menuScreen == MenuScreen::Controls) {
                            menuScreen = MenuScreen::Settings;
                        } else {
                            menuScreen = MenuScreen::Main;
                        }
                    } else {
                        if (worldStarted && !asyncLoading) {
                            menuOpen = false;
                            mouseCaptured = true;
                            Input::SetCursorMode(true);
                        } else {
                            Window::Close();
                        }
                    }
                } else {
                    // Open menu instead of closing the window (unless disabled).
                    if (startMenuEnabled || worldStarted) {
                        menuOpen = true;
                        menuScreen = MenuScreen::Main;
                        mouseCaptured = false;
                        Input::SetCursorMode(false);
                    } else if (mouseCaptured) {
                        mouseCaptured = false;
                        Input::SetCursorMode(false);
                        std::cout << "Mouse released" << std::endl;
                    } else {
                        Window::Close();
                    }
                }
                }
            }
            lastEsc = escDown;

            const bool tabDown = Input::IsKeyPressed(GLFW_KEY_TAB);
            if (inputArmed && tabDown && !lastTab && !inventoryOpen && !menuVisible) {
                mouseCaptured = !mouseCaptured;
                Input::SetCursorMode(mouseCaptured);
                std::cout << (mouseCaptured ? "Mouse captured" : "Mouse released") << std::endl;
            }
            lastTab = tabDown;

            const bool eDown = Input::IsKeyPressed(keyInventory);
            if (inputArmed && eDown && !lastE && !menuVisible && worldStarted) {
                inventoryOpen = !inventoryOpen;
                if (inventoryOpen) {
                    inventoryRestoreCapture = mouseCaptured;
                    mouseCaptured = false;
                    Input::SetCursorMode(false);
                } else {
                    if (inventoryRestoreCapture) {
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                    }
                }
            }
            lastE = eDown;

            const bool f8Down = Input::IsKeyPressed(GLFW_KEY_F8);
            if (amuletEnabled && inputArmed && f8Down && !lastF8 && !menuVisible && worldStarted && !amuletBridgeActive) {
                launchAmuletBridge();
            }
            lastF8 = f8Down;

            if (amuletEnabled && amuletBridgeActive) {
                const int state = amuletReturnState.load();
                if (state == 1) {
                    amuletBridgeActive = false;
                    amuletSaveDetected = true;
                    amuletStatusText = "Save complete. Reloading world...";
                    addAmuletLog("Save detected");

                    const bool ok = chunkManager.BeginLoadWorldFromFileAsync(worldFile.c_str(), worldRenderer);
                    if (ok) {
                        amuletReloadQueued = true;
                        addAmuletLog("Reload queued");
                        loadingWorld = true;
                        menuScreen = MenuScreen::Loading;
                        menuOpen = true;
                        showLoadingOverlay = true;
                        mouseCaptured = false;
                        Input::SetCursorMode(false);
                    } else {
                        menuScreen = MenuScreen::Main;
                        menuOpen = true;
                        showLoadingOverlay = false;
                        menuStatusText = "Reload failed";
                        addAmuletLog("Reload failed");
                    }
                } else if (state == 2) {
                    amuletBridgeActive = false;
                    menuScreen = MenuScreen::Main;
                    menuOpen = true;
                    showLoadingOverlay = false;
                    menuStatusText = "Amulet handshake failed";
                    addAmuletLog("Handshake failed");
                }
            }

            const bool menuInput = menuVisible && inputArmed;
            if (!amuletEnabled && menuScreen == MenuScreen::Amulet) {
                menuScreen = MenuScreen::Main;
            }
            const bool upDown = menuInput && Input::IsKeyPressed(GLFW_KEY_UP);
            const bool downDown = menuInput && Input::IsKeyPressed(GLFW_KEY_DOWN);
            const bool leftDown = menuInput && Input::IsKeyPressed(GLFW_KEY_LEFT);
            const bool rightDown = menuInput && Input::IsKeyPressed(GLFW_KEY_RIGHT);
            const bool enterDown = menuInput && Input::IsKeyPressed(GLFW_KEY_ENTER);

            const bool menuUp = upDown && !lastMenuUp;
            const bool menuDown = downDown && !lastMenuDown;
            const bool menuLeft = leftDown && !lastMenuLeft;
            const bool menuRight = rightDown && !lastMenuRight;
            const bool menuEnter = enterDown && !lastMenuEnter;

            if (menuInput && !rebindActive) {
                if (menuScreen == MenuScreen::Main) {
                    const int count = 7;
                    if (menuDown) menuMainIndex = (menuMainIndex + 1) % count;
                    if (menuUp) menuMainIndex = (menuMainIndex + count - 1) % count;
                } else if (menuScreen == MenuScreen::Load) {
                    if (worldList.empty()) {
                        scanWorlds();
                    }
                    const int rows = std::min((int)worldList.size(), 5);
                    const int count = rows + 2; // load + back
                    if (count > 0) {
                        if (menuDown) menuLoadIndex = (menuLoadIndex + 1) % count;
                        if (menuUp) menuLoadIndex = (menuLoadIndex + count - 1) % count;
                    }
                } else if (menuScreen == MenuScreen::Settings) {
                    const int count = 11;
                    if (menuDown) menuSettingsIndex = (menuSettingsIndex + 1) % count;
                    if (menuUp) menuSettingsIndex = (menuSettingsIndex + count - 1) % count;
                } else if (menuScreen == MenuScreen::Controls) {
                    const int count = 10;
                    if (menuDown) menuControlsIndex = (menuControlsIndex + 1) % count;
                    if (menuUp) menuControlsIndex = (menuControlsIndex + count - 1) % count;
                } else if (menuScreen == MenuScreen::Terrain) {
                    const int count = 10; // 8 presets + apply + back
                    if (menuDown) menuTerrainIndex = (menuTerrainIndex + 1) % count;
                    if (menuUp) menuTerrainIndex = (menuTerrainIndex + count - 1) % count;
                }
            }

            lastMenuUp = upDown;
            lastMenuDown = downDown;
            lastMenuLeft = leftDown;
            lastMenuRight = rightDown;
            lastMenuEnter = enterDown;

            const bool gDown = Input::IsKeyPressed(GLFW_KEY_G);
            if (!simpleBuild && inputArmed && gDown && !lastG) {
                precisionBuild = !precisionBuild;
                precisionAnchor = false;
                std::cout << (precisionBuild ? "Precision build: ON" : "Precision build: OFF") << std::endl;
            }
            lastG = gDown;

            static bool lastV = false;
            const bool vDown = Input::IsKeyPressed(GLFW_KEY_V);
            if (!simpleBuild && inputArmed && vDown && !lastV && !inventoryOpen) {
                if (buildMode == BuildMode::Plane) buildMode = BuildMode::Line;
                else if (buildMode == BuildMode::Line) buildMode = BuildMode::Box;
                else buildMode = BuildMode::Plane;
                precisionAnchor = false;
                const char* modeName = (buildMode == BuildMode::Plane) ? "Plane" : (buildMode == BuildMode::Line ? "Line" : "Box");
                std::cout << "Build mode: " << modeName << std::endl;
            }
            lastV = vDown;

            static bool lastM = false;
            const bool mDown = Input::IsKeyPressed(GLFW_KEY_M);
            if (!simpleBuild && inputArmed && mDown && !lastM && !inventoryOpen) {
                mirrorBuild = !mirrorBuild;
                if (mirrorBuild) {
                    mirrorOrigin = glm::ivec3((int)std::floor(camera.Position.x), (int)std::floor(camera.Position.y), (int)std::floor(camera.Position.z));
                }
                std::cout << (mirrorBuild ? "Mirror: ON" : "Mirror: OFF") << std::endl;
            }
            lastM = mDown;

            static bool lastN = false;
            const bool nDown = Input::IsKeyPressed(GLFW_KEY_N);
            if (!simpleBuild && inputArmed && nDown && !lastN && !inventoryOpen) {
                mirrorAxis = (mirrorAxis + 1) % 3;
                const char* axisName = (mirrorAxis == 0) ? "X" : (mirrorAxis == 1 ? "Z" : "Y");
                std::cout << "Mirror axis: " << axisName << std::endl;
            }
            lastN = nDown;

            static bool lastP = false;
            const bool pDown = Input::IsKeyPressed(GLFW_KEY_P);
            if (!simpleBuild && inputArmed && pDown && !lastP && !inventoryOpen) {
                planeLock = !planeLock;
                if (planeLock) {
                    planeLockY = (int)std::floor(camera.Position.y);
                    std::cout << "Plane lock: ON (Y=" << planeLockY << ")" << std::endl;
                } else {
                    std::cout << "Plane lock: OFF" << std::endl;
                }
            }
            lastP = pDown;

            const bool f1Down = Input::IsKeyPressed(GLFW_KEY_F1);
            if (f1Down && !lastF1) PrintControls();
            lastF1 = f1Down;

            const bool f2Down = Input::IsKeyPressed(GLFW_KEY_F2);
            if (f2Down && !lastF2) {
                showStatsInTitle = !showStatsInTitle;
                if (!showStatsInTitle) Window::SetTitle("High Performance Voxel Engine");
            }
            lastF2 = f2Down;

            const bool f3Down = Input::IsKeyPressed(keyWireframe);
            if (f3Down && !lastF3) {
                wireframe = !wireframe;
                glLineWidth(1.0f);
                std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << std::endl;
                if (wireframe) {
                    std::cout << "  Caps -> stream=" << wireframeStreamCapCfg
                              << " upload=" << wireframeUploadCapCfg
                              << " height=" << wireframeHeightCapCfg
                              << " cull=" << wireframeCullChunksCfg
                              << std::endl;
                }
                ini["wireframe"] = wireframe ? "1" : "0";
                SaveIniKeyValue(settingsPath, ini);
            }
            lastF3 = f3Down;

            const bool f4Down = Input::IsKeyPressed(GLFW_KEY_F4);
            if (f4Down && !lastF4) {
                vsync = !vsync;
                glfwSwapInterval(vsync ? 1 : 0);
            }
            lastF4 = f4Down;

            const bool f5Down = Input::IsKeyPressed(GLFW_KEY_F5);
            if (f5Down && !lastF5) {
                crosshairEnabled = !crosshairEnabled;
            }
            lastF5 = f5Down;

            const bool f6Down = Input::IsKeyPressed(GLFW_KEY_F6);
            if (f6Down && !lastF6) {
                dumpFrameEnabled = !dumpFrameEnabled;
            }
            lastF6 = f6Down;

            const bool spaceDown = Input::IsKeyPressed(GLFW_KEY_SPACE);
            if (inputArmed && spaceDown && !lastSpace) {
                const double t = glfwGetTime();
                const bool spawnNoClipActive = (spawnNoClipUntil > 0.0) && (t < spawnNoClipUntil);
                if (walkMode) {
                    if (lastSpaceTapTime >= 0.0 && (t - lastSpaceTapTime) <= 0.28) {
                        walkMode = false;
                        lastSpaceTapTime = -1.0;
                        camera.Velocity.y = 0.0f;
                        walkVerticalVel = 0.0f;
                        walkGrounded = false;
                        jumpBufferedUntil = -1.0;
                        std::cout << "Fly mode" << std::endl;
                    } else {
                        jumpBufferedUntil = t + walkJumpBufferSec;
                        const bool canCoyote = (lastGroundedTime >= 0.0 && (t - lastGroundedTime) <= walkCoyoteSec);
                        if (walkGrounded || canCoyote) {
                            walkVerticalVel = walkJumpSpeed;
                            walkGrounded = false;
                            jumpBufferedUntil = -1.0;
                        }
                        lastSpaceTapTime = t;
                    }
                } else {
                    if (!spawnNoClipActive && lastSpaceTapTime >= 0.0 && (t - lastSpaceTapTime) <= 0.28) {
                        walkMode = true;
                        lastSpaceTapTime = -1.0;
                        camera.Velocity.y = 0.0f;
                        walkVerticalVel = 0.0f;
                        walkGrounded = false;
                        jumpBufferedUntil = -1.0;
                        std::cout << "Walk mode" << std::endl;
                    } else {
                        lastSpaceTapTime = t;
                    }
                }
            }
            lastSpace = spaceDown;

            const bool f7Down = Input::IsKeyPressed(GLFW_KEY_F7);
            if (!menuBlocksInput && f7Down && !lastF7) {
                qualityLevel = (qualityLevel + 1) % 3;
                applyQuality();

                ini["quality"] = std::to_string(qualityLevel);
                ini["world_file"] = worldFile;
                SaveIniKeyValue(settingsPath, ini);

                std::cout << "Quality: " << (qualityLevel == 0 ? "LOW" : (qualityLevel == 1 ? "MED" : "HIGH"))
                          << " (view=" << viewDistanceChunks << ", margin=" << streamMarginChunks << ", budget=" << uploadBudget << ")"
                          << std::endl;
            }
            lastF7 = f7Down;

            const bool f9Down = Input::IsKeyPressed(GLFW_KEY_F9);
            if (!menuBlocksInput && inputArmed && f9Down && !lastF9) {
                saveCurrentWorld("F9");
            }
            lastF9 = f9Down;

            const bool f10Down = Input::IsKeyPressed(GLFW_KEY_F10);
            if (!menuBlocksInput && inputArmed && f10Down && !lastF10) {
                beginLoadCurrentWorldAsync("F10");
            }
            lastF10 = f10Down;

            const bool f11Down = Input::IsKeyPressed(GLFW_KEY_F11);
            if (inputArmed && f11Down && !lastF11) {
                perfHudEnabled = !perfHudEnabled;
                std::cout << "Perf HUD: " << (perfHudEnabled ? "ON" : "OFF") << std::endl;
            }
            lastF11 = f11Down;

            if (amuletEnabled && amuletBridgeActive) {
                const int state = amuletReturnState.load();
                if (state == 1) {
                    amuletBridgeActive = false;
                    amuletStatusText = "Amulet save complete. Reloading world...";

                    const bool ok = chunkManager.BeginLoadWorldFromFileAsync(worldFile.c_str(), worldRenderer);
                    if (ok) {
                        loadingWorld = true;
                        menuScreen = MenuScreen::Loading;
                        menuOpen = true;
                        showLoadingOverlay = true;
                        mouseCaptured = false;
                        Input::SetCursorMode(false);
                        menuStatusText.clear();
                    } else {
                        std::cout << "Amulet reload failed" << std::endl;
                        menuOpen = false;
                        showLoadingOverlay = false;
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                    }
                } else if (state == 2) {
                    amuletBridgeActive = false;
                    std::cout << "Amulet IPC return failed" << std::endl;
                    menuOpen = false;
                    showLoadingOverlay = false;
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                }
            }

            // Block selection: number keys and scroll wheel
            const uint8_t prevSelectedBlockId = selectedBlockId;
            if (!inventoryOpen && !menuBlocksInput) {
                const bool importPrevDown = Input::IsKeyPressed(keyImportPagePrev);
                const bool importNextDown = Input::IsKeyPressed(keyImportPageNext);
                if ((importPrevDown && !lastImportPrev) || (importNextDown && !lastImportNext)) {
                    const int delta = importNextDown ? 1 : -1;
                    const int oldPage = g_MinecraftImportPage;
                    g_MinecraftImportPage = ClampMinecraftImportPage(g_MinecraftImportPage + delta);
                    if (g_MinecraftImportPage != oldPage) {
                        runtimeMaxBlockId = GetRuntimeMaxBlockId();
                        selectedBlockId = (uint8_t)std::clamp((int)selectedBlockId, 1, runtimeMaxBlockId);
                        patternBlockId = selectedBlockId;
                        ini["import_page"] = std::to_string(g_MinecraftImportPage);
                        SaveIniKeyValue(settingsPath, ini);
                        std::cout << "Minecraft import page: " << g_MinecraftImportPage
                                  << "/" << GetMinecraftImportPageMax() << std::endl;
                    }
                }
                lastImportPrev = importPrevDown;
                lastImportNext = importNextDown;

                const bool oneDown = Input::IsKeyPressed(GLFW_KEY_1);
                if (oneDown && !last1) {
                    hotbarIndex = 0;
                    selectedBlockId = getHotbarBlockId(0);
                }
                last1 = oneDown;
                const bool twoDown = Input::IsKeyPressed(GLFW_KEY_2);
                if (twoDown && !last2) {
                    hotbarIndex = 1;
                    selectedBlockId = getHotbarBlockId(1);
                }
                last2 = twoDown;
                const bool threeDown = Input::IsKeyPressed(GLFW_KEY_3);
                if (threeDown && !last3) {
                    hotbarIndex = 2;
                    selectedBlockId = getHotbarBlockId(2);
                }
                last3 = threeDown;
                const bool fourDown = Input::IsKeyPressed(GLFW_KEY_4);
                if (fourDown && !last4) {
                    hotbarIndex = 3;
                    selectedBlockId = getHotbarBlockId(3);
                }
                last4 = fourDown;

                const bool fiveDown = Input::IsKeyPressed(GLFW_KEY_5);
                if (fiveDown && !last5) {
                    hotbarIndex = 4;
                    selectedBlockId = getHotbarBlockId(4);
                }
                last5 = fiveDown;

                const bool sixDown = Input::IsKeyPressed(GLFW_KEY_6);
                if (sixDown && !last6) {
                    hotbarIndex = 5;
                    selectedBlockId = getHotbarBlockId(5);
                }
                last6 = sixDown;

                const bool sevenDown = Input::IsKeyPressed(GLFW_KEY_7);
                if (sevenDown && !last7) {
                    hotbarIndex = 6;
                    selectedBlockId = getHotbarBlockId(6);
                }
                last7 = sevenDown;

                const bool eightDown = Input::IsKeyPressed(GLFW_KEY_8);
                if (eightDown && !last8) {
                    hotbarIndex = 7;
                    selectedBlockId = getHotbarBlockId(7);
                }
                last8 = eightDown;

                const bool nineDown = Input::IsKeyPressed(GLFW_KEY_9);
                if (nineDown && !last9) {
                    hotbarIndex = 8;
                    selectedBlockId = getHotbarBlockId(8);
                }
                last9 = nineDown;

                const bool zeroDown = Input::IsKeyPressed(GLFW_KEY_0);
                if (zeroDown && !last0) {
                    hotbarIndex = 9;
                    selectedBlockId = getHotbarBlockId(9);
                }
                last0 = zeroDown;

                const bool minusDown = Input::IsKeyPressed(GLFW_KEY_MINUS);
                if (minusDown && !lastMinus) {
                    hotbarIndex = 10;
                    selectedBlockId = getHotbarBlockId(10);
                }
                lastMinus = minusDown;

                const bool equalDown = Input::IsKeyPressed(GLFW_KEY_EQUAL);
                if (equalDown && !lastEqual) {
                    hotbarIndex = 11;
                    selectedBlockId = getHotbarBlockId(11);
                }
                lastEqual = equalDown;

                const float scrollY = Input::GetScrollDeltaY();
                if (scrollY != 0.0f) {
                    hotbarIndex = (hotbarIndex + (scrollY > 0.0f ? 1 : -1) + 12) % 12;
                    selectedBlockId = getHotbarBlockId(hotbarIndex);
                }
            }

            if (selectedBlockId != prevSelectedBlockId) {
                Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
            }
            
            bool hasMoveInput = false;
            bool keyForwardDownFrame = false;
            bool keyBackDownFrame = false;
            bool keyLeftDownFrame = false;
            bool keyRightDownFrame = false;
            bool keyUpDownFrame = false;
            bool keyDownDownFrame = false;
            if (gameplayArmed && !captureGuardActive && !inventoryOpen) {
                keyForwardDownFrame = Input::IsKeyPressed(keyForward);
                keyBackDownFrame = Input::IsKeyPressed(keyBack);
                keyLeftDownFrame = Input::IsKeyPressed(keyLeft);
                keyRightDownFrame = Input::IsKeyPressed(keyRight);
                keyUpDownFrame = Input::IsKeyPressed(keyUp);
                keyDownDownFrame = Input::IsKeyPressed(keyDown);
                if (keyForwardDownFrame) { camera.ProcessKeyboard(0, inputDeltaTime); hasMoveInput = true; }
                if (keyBackDownFrame) { camera.ProcessKeyboard(1, inputDeltaTime); hasMoveInput = true; }
                if (keyLeftDownFrame) { camera.ProcessKeyboard(2, inputDeltaTime); hasMoveInput = true; }
                if (keyRightDownFrame) { camera.ProcessKeyboard(3, inputDeltaTime); hasMoveInput = true; }
                if (!walkMode) {
                    if (keyUpDownFrame) { camera.ProcessKeyboard(4, inputDeltaTime); hasMoveInput = true; }
                    if (keyDownDownFrame) { camera.ProcessKeyboard(5, inputDeltaTime); hasMoveInput = true; }
                }
            }

            if (!walkMode && !hasMoveInput) {
                camera.Velocity = glm::vec3(0.0f);
            }

            const glm::vec3 prePhysicsPos = camera.Position;

            // Apply camera physics (inertia/momentum)
            camera.UpdatePhysics(inputDeltaTime);

            {
                const glm::vec3 step = camera.Position - prePhysicsPos;
                const float stepLen = glm::length(step);
                if (std::isfinite(stepLen) && stepLen > maxCameraStep) {
                    camera.Position = prePhysicsPos;
                    camera.Velocity = glm::vec3(0.0f);
                    if (traceRuntime) {
                        RunLog::Info("CAMERA step clamped len=" + std::to_string(stepLen));
                    }
                }
            }

            // Auto-stabilize flight when no movement input (Minecraft-like hover feel).
            if (!walkMode && !hasMoveInput) {
                camera.Velocity = glm::vec3(0.0f);
            }

            // Flight brake for precision stops
            if (!walkMode && Input::IsKeyPressed(keyBrake)) {
                camera.Velocity *= 0.10f;
            }

            if (walkMode) {
                const double t = glfwGetTime();
                const int maxY = heightChunks * Game::World::CHUNK_SIZE - 1;

                auto getSolidAt = [&](int x, int y, int z) -> bool {
                    if (y < 0 || y > maxY) return false;
                    uint8_t b = chunkManager.GetBlockWorld(glm::ivec3(x, y, z));
                    if (b == 0) {
                        b = Game::World::Generation::GetBlockAtWorld(x, y, z);
                    }
                    return (b != 0 && b != 5);
                };

                auto collidesAt = [&](const glm::vec3& pos) -> bool {
                    const float feetY = pos.y - walkEyeHeight;
                    const int minY = std::max(0, (int)std::floor(feetY + 0.01f));
                    const int maxBodyY = std::min(maxY, (int)std::floor(feetY + walkBodyHeight - 0.05f));
                    if (minY > maxBodyY) return false;

                    const float probeR = std::max(0.05f, walkCollisionRadius - walkCollisionSkin);

                    const std::array<glm::vec2, 9> samples = {
                        glm::vec2(0.0f, 0.0f),
                        glm::vec2( probeR, 0.0f),
                        glm::vec2(-probeR, 0.0f),
                        glm::vec2(0.0f,  probeR),
                        glm::vec2(0.0f, -probeR),
                        glm::vec2( probeR,  probeR),
                        glm::vec2( probeR, -probeR),
                        glm::vec2(-probeR,  probeR),
                        glm::vec2(-probeR, -probeR)
                    };

                    for (const auto& s : samples) {
                        const int sx = (int)std::floor(pos.x + s.x);
                        const int sz = (int)std::floor(pos.z + s.y);
                        for (int y = minY; y <= maxBodyY; ++y) {
                            if (getSolidAt(sx, y, sz)) {
                                return true;
                            }
                        }
                    }
                    return false;
                };

                glm::vec3 resolvedPos = prePhysicsPos;
                resolvedPos.y = prePhysicsPos.y;

                const glm::vec3 totalDelta = glm::vec3(camera.Position.x - prePhysicsPos.x, 0.0f, camera.Position.z - prePhysicsPos.z);
                const float moveLen = glm::length(glm::vec2(totalDelta.x, totalDelta.z));
                const int subSteps = std::clamp((int)std::ceil(moveLen / 0.20f), 1, 14);
                const glm::vec3 stepDelta = totalDelta / (float)subSteps;

                for (int i = 0; i < subSteps; ++i) {
                    glm::vec3 candidate = resolvedPos + stepDelta;

                    if (!collidesAt(candidate)) {
                        resolvedPos = candidate;
                        if (walkVerticalVel <= 0.05f && walkStepDownAssist > 0.0f) {
                            glm::vec3 bestDown = resolvedPos;
                            const float downStep = 0.05f;
                            for (float drop = downStep; drop <= walkStepDownAssist + 0.001f; drop += downStep) {
                                glm::vec3 probe = resolvedPos;
                                probe.y -= drop;
                                if (collidesAt(probe)) break;
                                bestDown = probe;
                            }
                            resolvedPos.y = bestDown.y;
                        }
                        continue;
                    }

                    bool moved = false;
                    const bool jumpAssistActive = Input::IsKeyPressed(GLFW_KEY_SPACE) || walkVerticalVel > 0.10f;
                    const float maxStepTry = std::min(1.25f, jumpAssistActive ? walkJumpStepUp : walkStepUp);
                    for (float lift = 0.10f; !moved && lift <= maxStepTry + 0.001f; lift += 0.08f) {
                        glm::vec3 stepUpCandidate = candidate;
                        stepUpCandidate.y = resolvedPos.y + lift;
                        if (!collidesAt(stepUpCandidate)) {
                            resolvedPos = stepUpCandidate;
                            walkVerticalVel = std::max(0.0f, walkVerticalVel);
                            moved = true;
                        }
                    }

                    if (!moved) {
                        glm::vec3 slideX = resolvedPos;
                        slideX.x += stepDelta.x;
                        if (!collidesAt(slideX)) {
                            resolvedPos.x = slideX.x;
                            moved = true;
                        } else {
                            for (float lift = 0.10f; !moved && lift <= maxStepTry + 0.001f; lift += 0.08f) {
                                glm::vec3 slideXStep = slideX;
                                slideXStep.y = resolvedPos.y + lift;
                                if (!collidesAt(slideXStep)) {
                                    resolvedPos = slideXStep;
                                    walkVerticalVel = std::max(0.0f, walkVerticalVel);
                                    moved = true;
                                }
                            }
                            if (!moved) camera.Velocity.x = 0.0f;
                        }

                        glm::vec3 slideZ = resolvedPos;
                        slideZ.z += stepDelta.z;
                        if (!collidesAt(slideZ)) {
                            resolvedPos.z = slideZ.z;
                            moved = true;
                        } else {
                            for (float lift = 0.10f; !moved && lift <= maxStepTry + 0.001f; lift += 0.08f) {
                                glm::vec3 slideZStep = slideZ;
                                slideZStep.y = resolvedPos.y + lift;
                                if (!collidesAt(slideZStep)) {
                                    resolvedPos = slideZStep;
                                    walkVerticalVel = std::max(0.0f, walkVerticalVel);
                                    moved = true;
                                }
                            }
                            if (!moved) camera.Velocity.z = 0.0f;
                        }
                    }

                    if (moved && walkVerticalVel <= 0.05f && walkStepDownAssist > 0.0f) {
                        glm::vec3 bestDown = resolvedPos;
                        const float downStep = 0.05f;
                        for (float drop = downStep; drop <= walkStepDownAssist + 0.001f; drop += downStep) {
                            glm::vec3 probe = resolvedPos;
                            probe.y -= drop;
                            if (collidesAt(probe)) break;
                            bestDown = probe;
                        }
                        resolvedPos.y = bestDown.y;
                    }

                    if (!moved && walkVerticalVel <= 0.05f && walkStepDownAssist > 0.0f) {
                        glm::vec3 descendMove = resolvedPos + stepDelta;
                        const float maxNudge = std::min(walkStepDownAssist, 0.55f);
                        for (float drop = 0.06f; drop <= maxNudge + 0.001f; drop += 0.06f) {
                            glm::vec3 probe = descendMove;
                            probe.y -= drop;
                            if (!collidesAt(probe)) {
                                resolvedPos = probe;
                                moved = true;
                                break;
                            }
                        }
                    }
                }

                camera.Position.x = resolvedPos.x;
                camera.Position.z = resolvedPos.z;
                camera.Position.y = std::max(camera.Position.y, resolvedPos.y);

                const int wx = (int)std::floor(camera.Position.x);
                const int wz = (int)std::floor(camera.Position.z);
                auto getSolid = [&](int y) -> bool {
                    return getSolidAt(wx, y, wz);
                };

                {
                    const float feetY = camera.Position.y - walkEyeHeight;
                    const int startY = std::clamp((int)std::floor(feetY + walkStepUp), 0, maxY);
                    const int bodyCells = std::max(1, (int)std::ceil(walkBodyHeight));

                    int surfaceY = walkSurfaceY;
                    bool found = false;
                    for (int y = startY; y >= 0; --y) {
                        if (!getSolid(y)) continue;

                        const float candidateFeet = (float)y + 1.0f;
                        if (candidateFeet > feetY + walkStepUp + 0.001f) continue;

                        bool blocked = false;
                        for (int k = 0; k < bodyCells; ++k) {
                            if (getSolid(y + 1 + k)) {
                                blocked = true;
                                break;
                            }
                        }
                        if (blocked) continue;

                        surfaceY = y;
                        found = true;
                        break;
                    }

                    if (found) {
                        walkSurfaceY = surfaceY;
                    }
                    lastWalkSampleTime = t;
                }

                camera.Velocity.y = 0.0f;

                const float groundFeetY = (float)walkSurfaceY + 1.0f;
                float feetY = camera.Position.y - walkEyeHeight;
                if (feetY < groundFeetY) {
                    feetY = groundFeetY;
                    camera.Position.y = feetY + walkEyeHeight;
                    walkVerticalVel = std::max(0.0f, walkVerticalVel);
                }

                const bool nearGround = (feetY <= groundFeetY + walkGroundProbe);
                if (nearGround && walkVerticalVel <= 0.0f) {
                    walkGrounded = true;
                    lastGroundedTime = t;
                    walkVerticalVel = 0.0f;
                    const float targetY = groundFeetY + walkEyeHeight;
                    const float blend = std::clamp(inputDeltaTime * (camera.Position.y > targetY ? walkDownLerp : walkSnapLerp), 0.0f, 1.0f);
                    camera.Position.y = camera.Position.y + (targetY - camera.Position.y) * blend;
                } else {
                    walkGrounded = false;
                    walkVerticalVel -= walkGravity * inputDeltaTime;

                    float newY = camera.Position.y + walkVerticalVel * inputDeltaTime;
                    float newFeetY = newY - walkEyeHeight;

                    if (walkVerticalVel > 0.0f) {
                        const float headTop = newFeetY + walkBodyHeight;
                        const int headCell = (int)std::floor(headTop);
                        if (getSolid(headCell)) {
                            newFeetY = (float)headCell - walkBodyHeight - 0.001f;
                            newY = newFeetY + walkEyeHeight;
                            walkVerticalVel = 0.0f;
                        }
                    }

                    if (newFeetY <= groundFeetY) {
                        newFeetY = groundFeetY;
                        newY = newFeetY + walkEyeHeight;
                        walkVerticalVel = 0.0f;
                        walkGrounded = true;
                        lastGroundedTime = t;
                    }

                    camera.Position.y = newY;
                }

                if (walkMode && walkGrounded && jumpBufferedUntil >= t) {
                    walkVerticalVel = walkJumpSpeed;
                    walkGrounded = false;
                    jumpBufferedUntil = -1.0;
                } else if (jumpBufferedUntil >= 0.0 && jumpBufferedUntil < t) {
                    jumpBufferedUntil = -1.0;
                }

                camera.Velocity.y = 0.0f;
            } else {
                walkGrounded = false;
                walkVerticalVel = 0.0f;
                jumpBufferedUntil = -1.0;
            }

            // Speed mode + feel tuning
            if (walkMode) {
                camera.MovementSpeed = walkSpeed;
                camera.AccelMult = walkAccel;
                camera.Drag = walkDrag;
                camera.MaxSpeedMult = walkMaxSpeedMult;
            } else {
                // Precision flight is the default feel; boost temporarily overrides it.
                camera.MovementSpeed = flySpeed * flyPrecisionMult;
                camera.AccelMult = flyAccel;
                camera.Drag = std::max(flyDrag, flyPrecisionDrag);
                camera.MaxSpeedMult = flyMaxSpeedMult;
                if (flyBoostEnabled && (Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL))) {
                    camera.MovementSpeed = flySpeed * flyBoostMult;
                    camera.Drag = flyDrag;
                }
            }

            if (mouseCaptured && !captureGuardActive) {
                glm::vec2 mouseDelta = Input::GetMouseDelta();
                if (mouseDelta.x != 0 || mouseDelta.y != 0) {
                    const bool spike = (std::abs(mouseDelta.x) > mouseDeltaMax) || (std::abs(mouseDelta.y) > mouseDeltaMax);
                    if (!spike) {
                        camera.MouseSensitivity = mouseSens;
                        const float y = invertY ? -mouseDelta.y : mouseDelta.y;
                        camera.ProcessMouseMovement(mouseDelta.x, y);
                    } else if (traceRuntime) {
                        RunLog::Info("INPUT mouse spike ignored dx=" + std::to_string((int)mouseDelta.x) +
                                     " dy=" + std::to_string((int)mouseDelta.y));
                    }
                }
            }

            // Stream chunks around the camera/player and apply any finished jobs.
            int activeStreamDistance = (!preloadDone && preloadRadiusChunks > 0)
                ? std::min(streamDistanceChunks, preloadRadiusChunks)
                : streamDistanceChunks;
            int hudStreamViewDistance = activeStreamDistance;
            int hudHeightChunks = heightChunks;
            int unloadDistanceChunks = std::min(192, streamDistanceChunks + cacheMarginChunks);

            int activeUploadBudget = uploadBudget;
            if (adaptiveBudget) {
                static int adaptiveUpload = uploadBudget;
                const float frameMs = deltaTime * 1000.0f;
                const float speed = glm::length(camera.Velocity);
                int target = baseUploadBudget;
                const int maxBurst = std::min(128, baseUploadBudget * 2);
                if (frameMs < 16.5f && speed > flySpeed * 0.65f) {
                    target = std::min(maxBurst, baseUploadBudget + baseUploadBudget / 2);
                }
                if (frameMs > 24.0f) {
                    target = std::max(6, baseUploadBudget / 2);
                }
                if (frameMs > 33.0f) {
                    target = std::max(4, baseUploadBudget / 3);
                }
                if (target < adaptiveUpload) {
                    adaptiveUpload = std::max(target, adaptiveUpload - 2);
                } else if (target > adaptiveUpload) {
                    adaptiveUpload = std::min(target, adaptiveUpload + 1);
                }
                activeUploadBudget = adaptiveUpload;
            }

            bool chunkCatchupActive = false;
            int chunkCatchupStepBoostApplied = 0;
            if (chunkCatchupEnabled && worldStarted && !showLoadingOverlay) {
                const int currentChunks = (int)worldRenderer.GetChunkCount();
                const float frameMsNow = deltaTime * 1000.0f;
                const bool chunkLow = currentChunks < chunkCatchupTarget;
                const bool perfGood = (fpsNow >= chunkCatchupMinFps) && (frameMsNow <= chunkCatchupMaxFrameMs);
                const bool emergencyLow = currentChunks < std::max(32, startupWarmChunksTarget / 2);
                const bool emergencyPerfOk = frameMsNow <= (chunkCatchupMaxFrameMs + 8.0f);
                if (chunkLow && (perfGood || (emergencyLow && emergencyPerfOk))) {
                    chunkCatchupActive = true;
                    const int deficit = std::max(0, chunkCatchupTarget - currentChunks);
                    const float deficitRatio = std::clamp((float)deficit / (float)std::max(1, chunkCatchupDeficitSoft), 0.0f, 1.0f);
                    const int scaledStreamBoost = chunkCatchupStreamBonus + (int)std::lround((float)chunkCatchupStreamBonus * (1.8f * deficitRatio));
                    const int scaledUploadBoost = chunkCatchupUploadBoost + (int)std::lround((float)chunkCatchupUploadBoost * (2.2f * deficitRatio));
                    activeStreamDistance = std::min(streamDistanceChunks, activeStreamDistance + scaledStreamBoost);
                    activeUploadBudget = std::min(chunkCatchupUploadBurstMax, activeUploadBudget + scaledUploadBoost);
                    chunkCatchupStepBoostApplied = std::clamp(
                        chunkCatchupStepsBonus + (int)std::lround((float)chunkCatchupStepsBonus * (2.0f * deficitRatio)),
                        0,
                        std::max(0, chunkCatchupStreamBurstMax - 1)
                    );
                }
            }

            if (worldStarted && startupPerfProtectSec > 0.0f) {
                const float startupAgeSec = (float)std::max(0.0, now - startTime);
                if (startupAgeSec < startupPerfProtectSec) {
                    const float t = std::clamp(startupAgeSec / startupPerfProtectSec, 0.0f, 1.0f);
                    const int streamCap = (int)std::lround((float)startupProtectStream + ((float)streamDistanceChunks - (float)startupProtectStream) * t);
                    const int uploadFloor = (int)std::lround((float)startupProtectUploadMin + ((float)baseUploadBudget - (float)startupProtectUploadMin) * t);
                    activeStreamDistance = std::min(activeStreamDistance, std::max(8, streamCap));
                    activeUploadBudget = std::max(activeUploadBudget, std::max(4, uploadFloor));
                }
            }

            if (largeMode && !hardSafeMode) {
                const float frameMs = deltaTime * 1000.0f;
                const float speed = glm::length(camera.Velocity);
                const float targetMs = 1000.0f / std::max(1.0f, largeTargetFps);

                static float smoothFrameMs = 16.6f;
                static int adaptiveStreamLarge = 0;
                static int adaptiveUploadLarge = 0;

                smoothFrameMs += (frameMs - smoothFrameMs) * 0.08f;

                if (adaptiveStreamLarge <= 0) adaptiveStreamLarge = activeStreamDistance;
                if (adaptiveUploadLarge <= 0) adaptiveUploadLarge = activeUploadBudget;

                const float pressure = smoothFrameMs / std::max(1.0f, targetMs);
                int desiredStream = streamDistanceChunks;
                int desiredUpload = activeUploadBudget;

                if (pressure > 1.35f) {
                    desiredStream = std::max(largeMinStream, streamDistanceChunks - (int)std::round((pressure - 1.0f) * 24.0f));
                    desiredUpload = std::max(largeMinUpload, activeUploadBudget - (int)std::round((pressure - 1.0f) * 42.0f));
                } else if (pressure > 1.10f) {
                    desiredStream = std::max(largeMinStream, streamDistanceChunks - (int)std::round((pressure - 1.0f) * 14.0f));
                    desiredUpload = std::max(largeMinUpload, activeUploadBudget - (int)std::round((pressure - 1.0f) * 24.0f));
                } else if (pressure < 0.92f) {
                    desiredUpload = std::min(128, activeUploadBudget + largeUploadBoost);
                    if (speed > flySpeed * 0.45f) {
                        desiredStream = std::min(streamDistanceChunks, activeStreamDistance + 6);
                    }
                }

                if (desiredStream < adaptiveStreamLarge) {
                    adaptiveStreamLarge = std::max(desiredStream, adaptiveStreamLarge - 3);
                } else if (desiredStream > adaptiveStreamLarge) {
                    adaptiveStreamLarge = std::min(desiredStream, adaptiveStreamLarge + 1);
                }

                if (desiredUpload < adaptiveUploadLarge) {
                    adaptiveUploadLarge = std::max(desiredUpload, adaptiveUploadLarge - 4);
                } else if (desiredUpload > adaptiveUploadLarge) {
                    adaptiveUploadLarge = std::min(desiredUpload, adaptiveUploadLarge + 2);
                }

                activeStreamDistance = std::clamp(adaptiveStreamLarge, largeMinStream, streamDistanceChunks);
                activeUploadBudget = std::clamp(adaptiveUploadLarge, largeMinUpload, 128);
                unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(12, cacheMarginChunks));
            }

            if (horizonMode && !hardSafeMode) {
                activeStreamDistance = std::clamp(Engine::QualityManager::GetViewDistance(), 12, 192);
                activeUploadBudget = std::clamp(Engine::QualityManager::GetUploadPerFrame(), 8, 64);
                unloadDistanceChunks = std::min(320, activeStreamDistance + std::max(24, cacheMarginChunks));
            }

            if (wireframe) {
                activeStreamDistance = std::min(activeStreamDistance, wireframeStreamCapCfg);
                activeUploadBudget = std::min(activeUploadBudget, wireframeUploadCapCfg);
                unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(8, cacheMarginChunks));
            }

            if (fastInputPriorityActive) {
                const int streamCapFastInput = std::max(8, std::min(streamDistanceChunks, fastInputStreamClamp));
                activeStreamDistance = std::min(activeStreamDistance, streamCapFastInput);
                activeUploadBudget = std::min(activeUploadBudget, fastInputUploadClamp);
                unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(8, cacheMarginChunks));
            }

            if (lowEndController) {
                const float frameMsNow = deltaTime * 1000.0f;
                lowEndEmaFastMs += (frameMsNow - lowEndEmaFastMs) * lowEndFastEma;
                lowEndEmaSlowMs += (frameMsNow - lowEndEmaSlowMs) * lowEndSlowEma;

                const float pressureMs = std::max(lowEndEmaFastMs, lowEndEmaSlowMs);
                const bool overBudget = pressureMs > (lowEndTargetMs + lowEndHysteresisMs);
                const bool underBudget = pressureMs < (lowEndTargetMs - lowEndHysteresisMs * 1.4f);

                if (overBudget && (lowEndLastDownshift < 0.0 || (now - lowEndLastDownshift) >= 0.20)) {
                    const int prev = lowEndLevel;
                    lowEndLevel = std::min(lowEndMaxLevel, lowEndLevel + 1);
                    if (lowEndLevel > prev) {
                        lowEndDownshiftEvents++;
                        std::ostringstream rs;
                        rs.setf(std::ios::fixed);
                        rs.precision(1);
                        rs << "over-budget " << pressureMs << "ms>" << lowEndTargetMs;
                        lowEndLastReason = rs.str();
                    }
                    lowEndLastDownshift = now;
                } else if (underBudget && (lowEndLastUpshift < 0.0 || (now - lowEndLastUpshift) >= 0.90)) {
                    const int prev = lowEndLevel;
                    lowEndLevel = std::max(0, lowEndLevel - 1);
                    if (lowEndLevel < prev) {
                        lowEndUpshiftEvents++;
                        std::ostringstream rs;
                        rs.setf(std::ios::fixed);
                        rs.precision(1);
                        rs << "recovery " << pressureMs << "ms";
                        lowEndLastReason = rs.str();
                    }
                    lowEndLastUpshift = now;
                }

                if (lowEndLevel > 0) {
                    const int streamCut = lowEndLevel * 2;
                    const int uploadCut = lowEndLevel * 5;
                    const int minUpload = std::max(4, baseUploadBudget / 6);
                    activeStreamDistance = std::max(8, activeStreamDistance - streamCut);
                    activeUploadBudget = std::max(minUpload, activeUploadBudget - uploadCut);
                    unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(10, cacheMarginChunks));
                }
            }

            if (spikeGuardEnabled && spikeLevel > 0) {
                const float lvl = (float)spikeLevel;
                const int streamCut = (int)std::round(2.0f + lvl * 2.0f);
                const int uploadCut = (int)std::round(4.0f + lvl * 6.0f);
                const int minSafeUpload = std::max(4, baseUploadBudget / 6);
                activeStreamDistance = std::max(8, activeStreamDistance - streamCut);
                activeUploadBudget = std::max(minSafeUpload, activeUploadBudget - uploadCut);
                unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(8, cacheMarginChunks));
            }

            if (controlObsEnabled && controlMitigationEnabled) {
                const bool controlPressure = (controlIntentEma > 0.35f)
                    && (stableFpsEma < 50.0f)
                    && (controlLagMsEma >= (controlLagWarnMs * 1.5f))
                    && (controlMoveResponseEma < (controlResponseThreshold * 0.85f));
                if (controlPressure) {
                    const int minUpload = std::max(4, baseUploadBudget / 5);
                    activeStreamDistance = std::max(8, activeStreamDistance - 4);
                    activeUploadBudget = std::max(minUpload, activeUploadBudget - 8);
                    unloadDistanceChunks = std::min(192, activeStreamDistance + std::max(8, cacheMarginChunks));
                }
            }

            coverageAlarmActive = false;
            if (coverageAlarmEnabled && coverageMetricLive) {
                if (coverageMisses >= coverageAlarmHigh) {
                    coverageAlarmHoldUntil = now + (double)coverageAlarmHoldSec;
                }

                const bool holdActive = (coverageMisses > coverageAlarmLow) || (coverageAlarmHoldUntil > now);
                float targetBoost = 0.0f;
                if (holdActive) {
                    coverageAlarmActive = true;
                    if (coverageAlarmHigh > coverageAlarmLow) {
                        const float t = std::clamp(
                            (float)(coverageMisses - coverageAlarmLow) / (float)(coverageAlarmHigh - coverageAlarmLow),
                            0.0f,
                            1.0f
                        );
                        targetBoost = (float)coverageAlarmBoost * t;
                    } else {
                        targetBoost = (float)coverageAlarmBoost;
                    }
                }

                const float rampAlpha = std::clamp(deltaTime * coverageAlarmRampPerSec, 0.0f, 1.0f);
                const float decayAlpha = std::clamp(deltaTime * coverageAlarmDecayPerSec, 0.0f, 1.0f);
                if (targetBoost > coverageBoostState) {
                    coverageBoostState += (targetBoost - coverageBoostState) * rampAlpha;
                } else {
                    coverageBoostState += (targetBoost - coverageBoostState) * decayAlpha;
                }

                coverageBoostApplied = (int)std::round(coverageBoostState);
                if (coverageBoostApplied > 0) {
                    const int maxBudget = std::min(128, std::max(baseUploadBudget + coverageAlarmBoost, activeUploadBudget + coverageAlarmBoost));
                    activeUploadBudget = std::clamp(activeUploadBudget + coverageBoostApplied, 1, maxBudget);
                }
            } else {
                coverageBoostState = 0.0f;
                coverageBoostApplied = 0;
            }

            if (resizeGuardActive) {
                activeUploadBudget = std::min(activeUploadBudget, 2);
            }

            if (asyncLoading) {
                chunkManager.PumpAsyncLoad(worldRenderer, std::clamp(activeUploadBudget, 4, uploadPumpCap));
            }

            const bool allowStreamingDuringLoadingOverlay = showLoadingOverlay && !loadingWorld;
            const bool allowWorldStreaming = worldStarted && (!menuVisible || allowStreamingDuringLoadingOverlay);
            if (allowWorldStreaming) {
                int activeHeightChunks = (!preloadDone && preloadRadiusChunks > 0)
                    ? std::min(heightChunks, preloadHeightChunks)
                    : heightChunks;

                const float speedNow = glm::length(camera.Velocity);
                const bool highSpeedFlight = !walkMode && (speedNow > flySpeed * streamLookAheadSpeedMult);
                const float lookAheadSec = lowEndController ? std::min(streamLookAheadSec, 0.60f) : streamLookAheadSec;
                const int lookAheadMaxChunks = lowEndController ? std::min(streamLookAheadMaxChunks, 8) : streamLookAheadMaxChunks;
                const float lookAheadMaxWorld = (float)(lookAheadMaxChunks * Game::World::CHUNK_SIZE);
                const float lookAheadWorld = std::clamp(speedNow * lookAheadSec, 0.0f, lookAheadMaxWorld);

                glm::vec3 streamOrigin = camera.Position;
                if (lookAheadWorld > 0.01f) {
                    glm::vec3 streamDir = camera.Front;
                    streamDir.y = 0.0f;
                    const float dirLen = glm::length(streamDir);
                    if (dirLen > 0.0001f) {
                        streamDir /= dirLen;
                        streamOrigin += streamDir * lookAheadWorld;
                    }
                }

                if (highSpeedFlight) {
                    const int streamBoost = std::clamp((int)std::lround(lookAheadWorld / (float)Game::World::CHUNK_SIZE), 1, 12);
                    activeStreamDistance = std::min(streamDistanceChunks, activeStreamDistance + streamBoost);
                    const int uploadBoost = std::min(streamFlightUploadBoost, std::max(12, (int)std::lround(speedNow * 0.9f)));
                    activeUploadBudget = std::min(uploadPumpCap, activeUploadBudget + uploadBoost);
                }

                if (worldStarted && startupPerfProtectSec > 0.0f) {
                    const float startupAgeSec = (float)std::max(0.0, now - startTime);
                    if (startupAgeSec < startupPerfProtectSec) {
                        activeHeightChunks = std::min(activeHeightChunks, startupProtectHeight);
                    }
                }

                if ((horizonTerrainEnabled || impostorRingEnabled) && !hardSafeMode) {
                    activeHeightChunks = 12;
                }

                if (wireframe) {
                    activeHeightChunks = std::min(activeHeightChunks, wireframeHeightCapCfg);
                }

                int streamViewDistance = activeStreamDistance;
                const int streamStepsMax = GetEnvIntClamped("HVE_STREAM_STEPS_MAX", 2, 1, 16);
                const int streamStepsOverloadMax = GetEnvIntClamped("HVE_STREAM_STEPS_OVERLOAD_MAX", 1, 1, 16);
                const std::size_t prevTriCount = worldRenderer.GetLastTriangleCount();
                const std::size_t prevDrawCount = worldRenderer.GetLastDrawCount();
                const bool renderStarved = worldStarted
                    && (worldRenderer.GetChunkCount() >= 768)
                    && (prevTriCount == 0)
                    && (prevDrawCount == 0);
                const bool pendingBacklogEmergency = stutterAlertPendingOldTicks > 0
                    && streamPendingOldestTicks > (std::size_t)std::max(220, stutterAlertPendingOldTicks * 3);
                if (renderStarved || pendingBacklogEmergency) {
                    starvationRescueUntil = std::max(starvationRescueUntil, now + (double)starvationRescueHoldSec);
                }
                const bool starvationRescueActive = (starvationRescueUntil > now);
                // Fixed-step streaming: each simulation step schedules/updates chunk work.
                int streamStepsTarget = std::max(1, std::min(fixedStepCount, streamStepsMax));
                if (fixedStepSaturated) {
                    streamStepsTarget = std::max(1, std::min(streamStepsTarget, streamStepsOverloadMax));
                }
                if (worldRenderer.GetChunkCount() <= 96) {
                    streamStepsTarget = std::max(streamStepsTarget, 1);
                    streamViewDistance = std::max(streamViewDistance, 16);
                    activeHeightChunks = std::max(activeHeightChunks, 12);
                    activeUploadBudget = std::max(activeUploadBudget, 8);
                }

                if (wireframe) {
                    streamStepsTarget = 1;
                }

                if (worldStarted && startupPerfProtectSec > 0.0f) {
                    const float startupAgeSec = (float)std::max(0.0, now - startTime);
                    if (startupAgeSec < startupPerfProtectSec * 2.0f && worldRenderer.GetChunkCount() < (std::size_t)startupWarmChunksTarget) {
                        streamStepsTarget = std::max(streamStepsTarget, startupMinStreamSteps);
                        activeUploadBudget = std::max(activeUploadBudget, startupProtectUploadMin);
                        streamViewDistance = std::max(streamViewDistance, startupProtectStream);
                    }
                }

                if (chunkCatchupActive && !fixedStepSaturated) {
                    streamStepsTarget = std::min(streamStepsMax, streamStepsTarget + chunkCatchupStepBoostApplied);
                }

                const bool movementPressure = (controlIntentEma > 0.30f) || (glm::length(camera.Velocity) > 0.8f);
                if (movementPressure) {
                    streamStepsTarget = std::min(streamStepsTarget, streamStepsWhileMoving);
                }

                if (highSpeedFlight && !fixedStepSaturated) {
                    streamStepsTarget = std::min(streamStepsMax, streamStepsTarget + streamFlightStepsBonus);
                }

                if (renderStarved) {
                    streamOrigin = camera.Position;
                    streamViewDistance = std::max(10, std::min(streamViewDistance, 20));
                    activeHeightChunks = std::max(12, std::min(activeHeightChunks, 32));
                    activeUploadBudget = std::max(activeUploadBudget, std::min(uploadPumpCap, baseUploadBudget + std::max(96, streamFlightUploadBoost / 2)));
                    if (!fixedStepSaturated) {
                        streamStepsTarget = std::min(streamStepsMax, std::max(streamStepsTarget, std::min(streamStepsMax, streamStepsWhileMoving + 2)));
                    }
                }

                if (starvationRescueActive) {
                    streamOrigin = camera.Position;
                    streamViewDistance = std::max(streamViewDistance, starvationRescueMinStream);
                    activeHeightChunks = std::max(activeHeightChunks, std::min(heightChunks, starvationRescueMinHeight));
                    const int rescueUpload = baseUploadBudget + starvationRescueUploadBoost;
                    activeUploadBudget = std::max(activeUploadBudget, std::min(uploadPumpCap, rescueUpload));
                    if (!fixedStepSaturated) {
                        streamStepsTarget = std::min(streamStepsMax, std::max(streamStepsTarget, 2));
                    }
                }

                if (fastInputPriorityActive && !renderStarved && !starvationRescueActive) {
                    const int streamCapFastInput = std::max(8, std::min(streamDistanceChunks, fastInputStreamClamp));
                    streamViewDistance = std::min(streamViewDistance, streamCapFastInput);
                    streamStepsTarget = std::min(streamStepsTarget, fastInputMaxStreamSteps);
                }

                const int inFlightGenNow = (int)chunkManager.GetInFlightGenerate();
                if (inFlightGenNow > streamInFlightSoftCap) {
                    const int over = inFlightGenNow - streamInFlightSoftCap;
                    const int stepCut = std::clamp(1 + over / 256, 1, 6);
                    const int viewCut = std::clamp(2 + over / 512, 2, 24);
                    streamStepsTarget = std::max(1, streamStepsTarget - stepCut);
                    streamViewDistance = std::max(8, streamViewDistance - viewCut);
                }

                for (int streamSteps = 0; streamSteps < streamStepsTarget; ++streamSteps) {
                    chunkManager.UpdateStreaming(streamOrigin, streamViewDistance, activeHeightChunks);
                }
                if (starvationRescueActive && starvationExtraPumpPasses > 0) {
                    const int rescuePumpBudget = std::clamp(std::max(activeUploadBudget, starvationExtraPumpBudget), 1, uploadPumpCap);
                    for (int pass = 0; pass < starvationExtraPumpPasses; ++pass) {
                        chunkManager.PumpCompleted(worldRenderer, rescuePumpBudget);
                    }
                }
                static double lastUnloadTime = 0.0;
                if (!disableUnload && !resizeGuardActive && (now - lastUnloadTime) > 0.5) {
                    chunkManager.UnloadFarChunks(camera.Position, unloadDistanceChunks, worldRenderer);
                    lastUnloadTime = now;
                }
                hudStreamViewDistance = streamViewDistance;
                hudHeightChunks = activeHeightChunks;
            }
            chunkManager.PumpCompleted(worldRenderer, std::clamp(activeUploadBudget, 1, uploadPumpCap));

            if (terrainMetricReady) {
                terrainMetrics = terrainMetricsPending;
                terrainMetricReady = false;
                terrainMetricJobQueued = false;
                lastTerrainMetricSample = now;
            }

            if ((lastTerrainMetricSample < 0.0 || (now - lastTerrainMetricSample) >= (1.0 / (double)eliteMetricsHz)) &&
                worldStarted && !terrainMetricJobQueued) {
                const int sampleX = (int)std::floor(camera.Position.x);
                const int sampleZ = (int)std::floor(camera.Position.z);
                terrainMetricJobQueued = true;
                timeSlicedJobs.Enqueue([&, sampleX, sampleZ](double) {
                    terrainMetricsPending = Game::World::Generation::SampleTerrainMetrics(sampleX, sampleZ, 32, 2);
                    terrainMetricReady = true;
                    return true;
                });
            }

            if (lastEliteMetricUpdate < 0.0 || (now - lastEliteMetricUpdate) >= (1.0 / (double)eliteMetricsHz)) {
                const std::size_t chunks = worldRenderer.GetChunkCount();
                const std::size_t tris = worldRenderer.GetLastTriangleCount();
                const std::size_t draws = worldRenderer.GetLastDrawCount();
                const std::size_t inflightGen = chunkManager.GetInFlightGenerate();
                const std::size_t inflightRemesh = chunkManager.GetInFlightRemesh();
                const std::size_t deferred = chunkManager.GetDeferredRemeshCount();
                const int pendingTimeSliced = timeSlicedJobs.PendingCount();

                const float queuePressure = std::clamp((float)(inflightGen + inflightRemesh + deferred) / 240.0f, 0.0f, 1.0f);
                const float overBudget = std::max(0.0f, frameMs - eliteFrameBudgetMs * 0.92f);
                const float accumulatorPressure = std::clamp((float)(accumulator / gameTime.GetFixedDelta()), 0.0f, 1.5f);
                const float alphaPressure = std::clamp((float)alpha, 0.0f, 1.0f);
                const float slicedPressure = std::clamp((float)pendingTimeSliced / 32.0f, 0.0f, 1.0f);
                const float mainThreadBlockPct = std::clamp(
                    overBudget / (eliteFrameBudgetMs * 1.5f) + accumulatorPressure * 0.08f + alphaPressure * 0.03f + slicedPressure * 0.07f,
                    0.0f,
                    0.95f
                );

                const float drawPressure = std::clamp((float)draws / 2200.0f, 0.0f, 2.0f);
                const float triPressure = std::clamp((float)tris / 3500000.0f, 0.0f, 2.0f);
                const float gpuStallPct = std::clamp(drawPressure * 0.55f + triPressure * 0.45f - 0.25f, 0.0f, 0.95f);

                const double vertexBytes = (double)tris * 3.0 * 28.0;
                const double indexBytes = (double)tris * 3.0 * 4.0;
                const float estimatedVramMB = (float)((vertexBytes + indexBytes) / (1024.0 * 1024.0));

                const double cpuBytes = (double)chunks * 4096.0 + (vertexBytes * 0.35);
                const float estimatedCpuGB = (float)(cpuBytes / (1024.0 * 1024.0 * 1024.0));
                estimatedCpuRamPeakGB = std::max(estimatedCpuRamPeakGB, estimatedCpuGB);

                const float cacheMissRate = std::clamp(queuePressure * 0.18f + std::clamp(frameMs / 40.0f, 0.0f, 1.0f) * 0.06f, 0.0f, 0.35f);
                const std::uint64_t totalAccesses = std::max<std::uint64_t>(
                    1ull,
                    (std::uint64_t)chunks * 4096ull * 8ull + (std::uint64_t)tris * 3ull
                );
                const std::uint64_t l1Misses = (std::uint64_t)((double)totalAccesses * (double)cacheMissRate * 0.70);
                const std::uint64_t l2Misses = (std::uint64_t)((double)totalAccesses * (double)cacheMissRate * 0.30);

                Engine::EliteTelemetryInput eliteInput{};
                eliteInput.fpsStable = stableFpsEma;
                eliteInput.maxSpikeMs = maxSpikeWindowMs;
                eliteInput.fpsTarget = eliteTargetFps;
                eliteInput.viewDistanceChunks = (float)viewDistanceChunks;
                eliteInput.mainThreadBlockPct = mainThreadBlockPct;
                eliteInput.gpuStallPct = gpuStallPct;

                eliteInput.heightEntropy = terrainMetrics.heightEntropy;
                eliteInput.biomeVariance = terrainMetrics.biomeVariance * 10.0f;
                eliteInput.erosionDetail = terrainMetrics.erosionDetail * 6.0f;
                eliteInput.pbrMetallicRoughnessVariation = std::clamp(2.0f + (float)runtimeMaxBlockId / 24.0f, 0.0f, 12.0f);
                eliteInput.visualArtifactDensity = std::max(0.05f, terrainMetrics.artifactDensity);

                eliteInput.totalActiveVRAMMB = estimatedVramMB;
                eliteInput.cpuRAMPeakGB = estimatedCpuRamPeakGB;
                eliteInput.cacheMissRate = cacheMissRate;
                eliteInput.l1Misses = l1Misses;
                eliteInput.l2Misses = l2Misses;
                eliteInput.totalAccesses = totalAccesses;
                eliteInput.memoryUsagePct = std::clamp(estimatedCpuRamPeakGB / std::max(1.0, hardware.systemRamGB), 0.0, 1.0);
                eliteInput.loadTimeFactor = std::clamp((float)worldRenderer.GetChunkCount() / std::max(1.0f, (float)preloadTargetChunks), 0.0f, 1.0f);
                eliteInput.buildStability = std::clamp(1.0f - std::min(1.0f, (float)buildWatch.size() / 64.0f), 0.0f, 1.0f);
                eliteInput.activeChunks = (int)chunks;
                eliteInput.meshPoolMB = estimatedVramMB;
                eliteInput.availableRamGB = (float)hardware.systemRamGB;
                eliteInput.biomeEntropy = terrainMetrics.biomeVariance * 10.0f;

                Telemetry::UpdateEliteMetrics(eliteInput);
                lastEliteMetricUpdate = now;
                maxSpikeWindowMs = frameMs;
            }

            // Exit loading overlay once the first chunks are visible to keep huge worlds smooth.
            if (loadingWorld && showLoadingOverlay && worldRenderer.GetChunkCount() > 0) {
                showLoadingOverlay = false;
                menuOpen = false;
                worldStarted = true;
                mouseCaptured = true;
                Input::SetCursorMode(true);
            }

            // If an async load finished this frame, transition into gameplay (or show error).
            if (loadingWorld && chunkManager.IsAsyncLoadFinished()) {
                loadingWorld = false;
                const std::string status = chunkManager.GetAsyncLoadStatus();
                const bool failed = (status.rfind("Failed", 0) == 0);
                if (failed) {
                    menuOpen = true;
                    showLoadingOverlay = false;
                    menuScreen = MenuScreen::Load;
                    mouseCaptured = false;
                    Input::SetCursorMode(false);
                    menuStatusText = status;
                } else {
                    worldStarted = true;
                    showLoadingOverlay = false;
                    menuOpen = false;
                    menuScreen = MenuScreen::Main;
                    mouseCaptured = true;
                    Input::SetCursorMode(true);
                    preloadDone = (preloadRadiusChunks <= 0);
                    preloadStartTime = now;
                    cameraAutoPlaced = false;
                    if (amuletReloadQueued) {
                        amuletReloadDone = true;
                        amuletReloadQueued = false;
                            addAmuletLog("Reload done");
                    }
                }
            }

            if (buildWatchEnabled && !buildWatch.empty()) {
                const double tNow = glfwGetTime();
                for (auto it = buildWatch.begin(); it != buildWatch.end(); ) {
                    if ((tNow - it->time) < (double)buildWatchSec) {
                        ++it;
                        continue;
                    }

                    const glm::ivec3 cc = worldToChunk(it->pos);
                    std::uint64_t stamp = 0;
                    const bool hasStamp = chunkManager.GetChunkMeshStamp(cc, stamp);
                    const uint8_t cur = chunkManager.GetBlockWorld(it->pos);

                    if (cur != it->id) {
                        std::ostringstream ss;
                        ss << "[BuildWatch] Block changed/missing at "
                           << it->pos.x << "," << it->pos.y << "," << it->pos.z
                           << " expected=" << int(it->id) << " got=" << int(cur);
                        std::cout << ss.str() << std::endl;
                        AppendBuildWatchLog(ss.str());
                        it = buildWatch.erase(it);
                        continue;
                    }

                    if (!hasStamp || stamp <= it->meshStampAtPlace) {
                        std::ostringstream ss;
                        ss << "[BuildWatch] Mesh not updated after place at "
                           << it->pos.x << "," << it->pos.y << "," << it->pos.z
                           << " chunk=" << cc.x << "," << cc.y << "," << cc.z
                           << " stamp=" << stamp << " base=" << it->meshStampAtPlace;
                        std::cout << ss.str() << std::endl;
                        AppendBuildWatchLog(ss.str());
                        it = buildWatch.erase(it);
                        continue;
                    }

                    it = buildWatch.erase(it);
                }
            }

            // Auto-place camera to ensure we start with a clear view of terrain.
            if (!cameraAutoPlaced && worldRenderer.GetChunkCount() > 0) {
                const glm::ivec2 anchorXZ((int)std::floor(camera.Position.x), (int)std::floor(camera.Position.z));
                glm::vec3 safePos(0.0f);
                bool placed = tryFindSafeSpawn(anchorXZ, safePos);
                if (!placed && (now - autoPlaceStartTime) >= autoPlaceFallbackSec) {
                    const int sx = anchorXZ.x;
                    const int sz = anchorXZ.y;
                    const int surfaceY = Game::World::Generation::GetBaseSurfaceYAtWorld(sx, sz);
                    safePos = glm::vec3((float)sx + 0.5f, (float)surfaceY + walkEyeHeight + 0.25f, (float)sz + 0.5f);
                    placed = true;
                    if (traceRuntime) {
                        RunLog::Info("SPAWN fallback used at x=" + std::to_string(sx) +
                                     " z=" + std::to_string(sz) +
                                     " y=" + std::to_string(surfaceY));
                    }
                }
                if (placed) {
                    (void)resolveSpawnUnstuck(safePos);
                    camera.Position = safePos;
                    camera.Velocity = glm::vec3(0.0f);
                    walkMode = false;
                    walkGrounded = false;
                    walkVerticalVel = 0.0f;
                    jumpBufferedUntil = -1.0;
                    spawnNoClipUntil = now + (double)GetEnvFloatClamped("HVE_SPAWN_NOCLIP_SEC", 2.2f, 0.0f, 8.0f);
                    camera.Pitch = -22.0f;
                    camera.updateCameraVectors();
                    walkSurfaceY = (int)std::floor(camera.Position.y - walkEyeHeight - 0.02f);
                    cameraAutoPlaced = true;
                }
            }

            if (emergencyTerrainBootstrap && !emergencyTerrainDone && worldStarted && (now - autoPlaceStartTime) >= emergencyTerrainDelaySec) {
                const int cx = (int)std::floor(camera.Position.x);
                const int cz = (int)std::floor(camera.Position.z);
                const int surfaceY = Game::World::Generation::GetBaseSurfaceYAtWorld(cx, cz);
                const bool likelySkyOnly = (camera.Position.y > (float)surfaceY + walkEyeHeight + 26.0f) || !cameraAutoPlaced;
                if (likelySkyOnly) {
                    const int radius = hardSafeMode ? 10 : 6;
                    chunkManager.BeginBulkEdit();
                    for (int z = cz - radius; z <= cz + radius; ++z) {
                        for (int x = cx - radius; x <= cx + radius; ++x) {
                            const int h = Game::World::Generation::GetBaseSurfaceYAtWorld(x, z);
                            chunkManager.SetBlockWorld(glm::ivec3(x, h - 2, z), 1);
                            chunkManager.SetBlockWorld(glm::ivec3(x, h - 1, z), 1);
                            chunkManager.SetBlockWorld(glm::ivec3(x, h, z), 2);
                            for (int y = h + 1; y <= h + 6; ++y) {
                                chunkManager.SetBlockWorld(glm::ivec3(x, y, z), 0);
                            }
                        }
                    }
                    chunkManager.EndBulkEdit();

                    glm::vec3 forcedPos((float)cx + 0.5f, (float)surfaceY + walkEyeHeight + 0.25f, (float)cz + 0.5f);
                    (void)resolveSpawnUnstuck(forcedPos);
                    camera.Position = forcedPos;
                    camera.Pitch = -24.0f;
                    camera.updateCameraVectors();
                    camera.Velocity = glm::vec3(0.0f);
                    walkMode = false;
                    walkGrounded = false;
                    walkVerticalVel = 0.0f;
                    jumpBufferedUntil = -1.0;
                    spawnNoClipUntil = now + 2.0;
                    walkSurfaceY = (int)std::floor(camera.Position.y - walkEyeHeight - 0.02f);
                    cameraAutoPlaced = true;
                    emergencyTerrainDone = true;
                    if (traceRuntime) {
                        RunLog::Info("SPAWN emergency terrain bootstrap applied");
                    }
                } else {
                    emergencyTerrainDone = true;
                }
            }

            const Game::World::RaycastHit aimHit = chunkManager.RaycastWorld(camera.Position, camera.Front, reachDist);
            if (aimHit.hit) {
                lastAimHit = aimHit;
                lastAimHitTime = now;
                hasLastAimHit = true;
            }

            if (simpleBuild) {
                precisionBuild = false;
                precisionAnchor = false;
            }

            glm::ivec3 aimPrev = aimHit.prevWorld;
            if (planeLock) {
                aimPrev.y = planeLockY;
            }

            const bool lmbDown = Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool rmbDown = Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
            const bool mmbDown = Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_MIDDLE);
            int controlPlaceOpsThisFrame = 0;
            int controlBreakOpsThisFrame = 0;
            glm::ivec3 editorSelMin{0};
            glm::ivec3 editorSelMax{0};
            bool editorSelValid = false;

            bool builderPanelHover = false;
            if (!mouseCaptured && builderPanelEnabled && !menuVisible && !inventoryOpen) {
                const glm::vec2 mp = Input::GetMousePosition();
                const float mx = (curW > 0) ? (mp.x / (float)curW) * 2.0f - 1.0f : 0.0f;
                const float my = (curH > 0) ? (1.0f - (mp.y / (float)curH) * 2.0f) : 0.0f;
                const float panelX0 = 0.58f;
                const float panelX1 = 0.98f;
                const float panelY0 = -0.92f;
                const float panelY1 = 0.92f;
                if (mx >= panelX0 && mx <= panelX1 && my >= panelY0 && my <= panelY1) {
                    builderPanelHover = true;
                }
            }

            // Auto-capture mouse if clicking inside the window while free
            bool justCaptured = false;
            // Removed ImGui check as it caused compilation error (not included/used here)
            if (!mouseCaptured && !inventoryOpen && inputArmed && (lmbDown || rmbDown || mmbDown) && !builderPanelHover) {
                mouseCaptured = true;
                Input::SetCursorMode(true);
                justCaptured = true;
            }

            auto pushAction = [&](BuildAction&& action) {
                if (action.changes.empty()) return;
                redoStack.clear();
                undoStack.emplace_back(std::move(action));
                if ((int)undoStack.size() > undoMax) {
                    undoStack.erase(undoStack.begin());
                }
            };

            auto applyAction = [&](const BuildAction& action, bool undo) {
                chunkManager.BeginBulkEdit();
                for (const auto& c : action.changes) {
                    const uint8_t id = undo ? c.before : c.after;
                    chunkManager.SetBlockWorld(c.pos, id);
                }
                chunkManager.EndBulkEdit();
            };

            auto performUndo = [&]() {
                if (undoStack.empty()) return;
                BuildAction action = std::move(undoStack.back());
                undoStack.pop_back();
                applyAction(action, true);
                redoStack.emplace_back(std::move(action));
            };

            auto performRedo = [&]() {
                if (redoStack.empty()) return;
                BuildAction action = std::move(redoStack.back());
                redoStack.pop_back();
                applyAction(action, false);
                undoStack.emplace_back(std::move(action));
            };

            auto mirrorPos = [&](const glm::ivec3& p) {
                if (!mirrorBuild) return p;
                glm::ivec3 out = p;
                if (mirrorAxis == 0) out.x = mirrorOrigin.x - (p.x - mirrorOrigin.x);
                else if (mirrorAxis == 1) out.z = mirrorOrigin.z - (p.z - mirrorOrigin.z);
                else out.y = mirrorOrigin.y - (p.y - mirrorOrigin.y);
                return out;
            };

            auto tryPlace = [&](const glm::ivec3& p, BuildAction& action) {
                // Prevent placing block inside player (2-block height check)
                // Assume Camera is at Eye Level (~1.6m above feet)
                // Bounding box: configurable (smaller default for easier angle building)
                const float halfW = playerBuildHalfWidth;
                const glm::vec3 feet = camera.Position - glm::vec3(0.0f, 1.6f, 0.0f);
                const glm::vec3 pMin = glm::vec3(p);
                const glm::vec3 pMax = pMin + 1.0f;
                const glm::vec3 bMin = feet - glm::vec3(halfW, 0.0f, halfW);
                const glm::vec3 bMax = feet + glm::vec3(halfW, playerBuildHeight, halfW);

                const bool overlap = (pMin.x < bMax.x && pMax.x > bMin.x) &&
                                     (pMin.y < bMax.y && pMax.y > bMin.y) &&
                                     (pMin.z < bMax.z && pMax.z > bMin.z);
                
                // Enforce collision check even in simple mode
                if (overlap) {
                    return false;
                }

                const uint8_t prev = chunkManager.GetBlockWorld(p);
                if (prev == 0) {
                    chunkManager.SetBlockWorld(p, selectedBlockId);
                    controlPlaceOpsThisFrame++;
                    Telemetry::RecordBlockPlace(glfwGetTime(), p, selectedBlockId);
                    action.changes.push_back(BlockChange{p, prev, selectedBlockId});

                    if (buildWatchEnabled) {
                        std::uint64_t stamp = 0;
                        const glm::ivec3 cc = worldToChunk(p);
                        (void)chunkManager.GetChunkMeshStamp(cc, stamp);
                        buildWatch.push_back(BuildWatchEntry{p, selectedBlockId, glfwGetTime(), stamp, false});
                        if ((int)buildWatch.size() > buildWatchMax) {
                            buildWatch.erase(buildWatch.begin());
                        }
                    }

                    if (mirrorBuild) {
                        const glm::ivec3 mp = mirrorPos(p);
                        if (mp != p) {
                            const uint8_t mprev = chunkManager.GetBlockWorld(mp);
                            if (mprev == 0) {
                                chunkManager.SetBlockWorld(mp, selectedBlockId);
                                controlPlaceOpsThisFrame++;
                                Telemetry::RecordBlockPlace(glfwGetTime(), mp, selectedBlockId);
                                action.changes.push_back(BlockChange{mp, mprev, selectedBlockId});

                                if (buildWatchEnabled) {
                                    std::uint64_t stamp = 0;
                                    const glm::ivec3 cc = worldToChunk(mp);
                                    (void)chunkManager.GetChunkMeshStamp(cc, stamp);
                                    buildWatch.push_back(BuildWatchEntry{mp, selectedBlockId, glfwGetTime(), stamp, false});
                                    if ((int)buildWatch.size() > buildWatchMax) {
                                        buildWatch.erase(buildWatch.begin());
                                    }
                                }
                            }
                        }
                    }
                    return true;
                }
                return false;
            };

            auto tryBreak = [&](const glm::ivec3& p, BuildAction& action) {
                const uint8_t prev = chunkManager.GetBlockWorld(p);
                if (prev != 0) {
                    chunkManager.SetBlockWorld(p, 0);
                    controlBreakOpsThisFrame++;
                    Telemetry::RecordBlockBreak(glfwGetTime(), p, prev);
                    action.changes.push_back(BlockChange{p, prev, 0});

                    if (mirrorBuild) {
                        const glm::ivec3 mp = mirrorPos(p);
                        if (mp != p) {
                            const uint8_t mprev = chunkManager.GetBlockWorld(mp);
                            if (mprev != 0) {
                                chunkManager.SetBlockWorld(mp, 0);
                                controlBreakOpsThisFrame++;
                                Telemetry::RecordBlockBreak(glfwGetTime(), mp, mprev);
                                action.changes.push_back(BlockChange{mp, mprev, 0});
                            }
                        }
                    }
                }
            };

            const bool ctrlDown = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
            static bool lastUndo = false;
            static bool lastRedo = false;
            const bool undoDown = ctrlDown && Input::IsKeyPressed(GLFW_KEY_Z);
            const bool redoDown = ctrlDown && Input::IsKeyPressed(GLFW_KEY_Y);
            if (gameplayArmed && !inventoryOpen && undoDown && !lastUndo) {
                performUndo();
            }
            if (gameplayArmed && !inventoryOpen && redoDown && !lastRedo) {
                performRedo();
            }
            lastUndo = undoDown;
            lastRedo = redoDown;

            auto snapNormal = [](const glm::ivec3& n) {
                const int ax = std::abs(n.x);
                const int ay = std::abs(n.y);
                const int az = std::abs(n.z);
                if (ax >= ay && ax >= az) return glm::ivec3(n.x >= 0 ? 1 : -1, 0, 0);
                if (ay >= ax && ay >= az) return glm::ivec3(0, n.y >= 0 ? 1 : -1, 0);
                return glm::ivec3(0, 0, n.z >= 0 ? 1 : -1);
            };

            auto placePlane = [&](const glm::ivec3& a, const glm::ivec3& b, const glm::ivec3& n) {
                int placed = 0;
                BuildAction action;
                chunkManager.BeginBulkEdit();
                const glm::ivec3 normal = snapNormal(n);
                const int minX = std::min(a.x, b.x);
                const int maxX = std::max(a.x, b.x);
                const int minY = std::min(a.y, b.y);
                const int maxY = std::max(a.y, b.y);
                const int minZ = std::min(a.z, b.z);
                const int maxZ = std::max(a.z, b.z);

                if (normal.x != 0) {
                    const int x = a.x;
                    for (int y = minY; y <= maxY; ++y) {
                        for (int z = minZ; z <= maxZ; ++z) {
                            if (placed >= placeMax) {
                                chunkManager.EndBulkEdit();
                                return placed;
                            }
                            const glm::ivec3 p(x, y, z);
                            if (tryPlace(p, action)) {
                                placed++;
                            }
                        }
                    }
                    chunkManager.EndBulkEdit();
                    pushAction(std::move(action));
                    return placed;
                }

                if (normal.y != 0) {
                    const int y = a.y;
                    for (int x = minX; x <= maxX; ++x) {
                        for (int z = minZ; z <= maxZ; ++z) {
                            if (placed >= placeMax) {
                                chunkManager.EndBulkEdit();
                                return placed;
                            }
                            const glm::ivec3 p(x, y, z);
                            if (tryPlace(p, action)) {
                                placed++;
                            }
                        }
                    }
                    chunkManager.EndBulkEdit();
                    pushAction(std::move(action));
                    return placed;
                }

                const int z = a.z;
                for (int x = minX; x <= maxX; ++x) {
                    for (int y = minY; y <= maxY; ++y) {
                        if (placed >= placeMax) {
                            chunkManager.EndBulkEdit();
                            return placed;
                        }
                        const glm::ivec3 p(x, y, z);
                        if (tryPlace(p, action)) {
                            placed++;
                        }
                    }
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
                return placed;
            };

            auto placeLine = [&](const glm::ivec3& a, const glm::ivec3& b) {
                BuildAction action;
                chunkManager.BeginBulkEdit();
                int placed = 0;
                const int dx = std::abs(b.x - a.x);
                const int dy = std::abs(b.y - a.y);
                const int dz = std::abs(b.z - a.z);
                if (dx >= dy && dx >= dz) {
                    const int minX = std::min(a.x, b.x);
                    const int maxX = std::max(a.x, b.x);
                    for (int x = minX; x <= maxX; ++x) {
                        if (placed >= placeMax) break;
                        if (tryPlace(glm::ivec3(x, a.y, a.z), action)) placed++;
                    }
                } else if (dy >= dx && dy >= dz) {
                    const int minY = std::min(a.y, b.y);
                    const int maxY = std::max(a.y, b.y);
                    for (int y = minY; y <= maxY; ++y) {
                        if (placed >= placeMax) break;
                        if (tryPlace(glm::ivec3(a.x, y, a.z), action)) placed++;
                    }
                } else {
                    const int minZ = std::min(a.z, b.z);
                    const int maxZ = std::max(a.z, b.z);
                    for (int z = minZ; z <= maxZ; ++z) {
                        if (placed >= placeMax) break;
                        if (tryPlace(glm::ivec3(a.x, a.y, z), action)) placed++;
                    }
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
            };

            auto placeBox = [&](const glm::ivec3& a, const glm::ivec3& b) {
                BuildAction action;
                chunkManager.BeginBulkEdit();
                const int minX = std::min(a.x, b.x);
                const int maxX = std::max(a.x, b.x);
                const int minY = std::min(a.y, b.y);
                const int maxY = std::max(a.y, b.y);
                const int minZ = std::min(a.z, b.z);
                const int maxZ = std::max(a.z, b.z);
                int placed = 0;
                for (int x = minX; x <= maxX; ++x) {
                    for (int y = minY; y <= maxY; ++y) {
                        for (int z = minZ; z <= maxZ; ++z) {
                            if (placed >= placeMax) break;
                            if (tryPlace(glm::ivec3(x, y, z), action)) {
                                placed++;
                            }
                        }
                        if (placed >= placeMax) break;
                    }
                    if (placed >= placeMax) break;
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
            };

            Game::World::RaycastHit placeHit = aimHit;

            const bool editorInputReady = editorEnabled && gameplayArmed && mouseCaptured && !inventoryOpen && !menuVisible;
            static bool lastEditorB = false;
            static bool lastEditorT = false;
            static bool lastEditorX = false;
            static bool lastEditorEnter = false;
            static bool lastEditorBackspace = false;
            const bool bDown = Input::IsKeyPressed(GLFW_KEY_B);
            const bool tDown = Input::IsKeyPressed(GLFW_KEY_T);
            const bool xDown = Input::IsKeyPressed(GLFW_KEY_X);
            const bool enterDownEditor = Input::IsKeyPressed(GLFW_KEY_ENTER);
            const bool backDownEditor = Input::IsKeyPressed(GLFW_KEY_BACKSPACE);

            auto computeEditorSelection = [&](const glm::ivec3& start, const glm::ivec3& end,
                                              EditorDragMode mode, const glm::ivec3& normal,
                                              glm::ivec3& outMin, glm::ivec3& outMax) {
                glm::ivec3 a = start;
                glm::ivec3 b = end;

                if (mode == EditorDragMode::Face) {
                    if (normal.x != 0) b.x = a.x;
                    else if (normal.y != 0) b.y = a.y;
                    else b.z = a.z;
                } else if (mode == EditorDragMode::Column) {
                    b.y = 0;
                }

                outMin = glm::ivec3(
                    std::min(a.x, b.x),
                    std::min(a.y, b.y),
                    std::min(a.z, b.z)
                );
                outMax = glm::ivec3(
                    std::max(a.x, b.x),
                    std::max(a.y, b.y),
                    std::max(a.z, b.z)
                );
            };

            if (editorInputReady && bDown && !lastEditorB) {
                if (editorMode == EditorMode::Fill) editorMode = EditorMode::Replace;
                else if (editorMode == EditorMode::Replace) editorMode = EditorMode::Hollow;
                else if (editorMode == EditorMode::Hollow) editorMode = EditorMode::Remove;
                else editorMode = EditorMode::Fill;
            }
            if (editorInputReady && xDown && !lastEditorX) {
                if (editorDragMode == EditorDragMode::Box) editorDragMode = EditorDragMode::Face;
                else if (editorDragMode == EditorDragMode::Face) editorDragMode = EditorDragMode::Column;
                else editorDragMode = EditorDragMode::Box;
            }
            if (editorInputReady && tDown && !lastEditorT && aimHit.hit && aimHit.blockId != 0) {
                editorTargetId = aimHit.blockId;
            }
            if (editorInputReady && backDownEditor && !lastEditorBackspace) {
                editorStartValid = false;
                editorEndValid = false;
                editorDragActive = false;
                editorBatch.active = false;
                editorBatch.action.changes.clear();
            }

            lastEditorB = bDown;
            lastEditorT = tDown;
            lastEditorX = xDown;
            lastEditorEnter = enterDownEditor;
            lastEditorBackspace = backDownEditor;

            const bool lmbClicked = lmbDown && !lastLmb;
            const bool rmbClicked = rmbDown && !lastRmb;
            const bool mmbClicked = mmbDown && !lastMmb;

            // Minecraft-style pick block (middle mouse): copy aimed block to current hotbar slot.
            if (gameplayArmed && mouseCaptured && !menuVisible && !inventoryOpen && !justCaptured && mmbClicked && aimHit.hit && aimHit.blockId != 0) {
                const uint8_t prevSelectedBlockId = selectedBlockId;
                selectedBlockId = aimHit.blockId;
                setHotbarStack(hotbarIndex, selectedBlockId, kItemStackMaxCount);
                if (selectedBlockId != prevSelectedBlockId) {
                    Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                }
            }

            static bool lastBackspaceVisual = false;
            const bool backDownVisual = Input::IsKeyPressed(GLFW_KEY_BACKSPACE);
            const bool visualToolActive = visualBuildEnabled && gameplayArmed && mouseCaptured && !inventoryOpen && !menuVisible;
            const bool consumeBuildInput = visualToolActive && visualTool != VisualTool::None;

            auto updateVisualSelection = [&]() {
                if (selectionState == SelectionState::Idle) {
                    visualSelValid = false;
                    return;
                }
                visualSelMin = glm::ivec3(
                    std::min(selectionPos1.x, selectionPos2.x),
                    std::min(selectionPos1.y, selectionPos2.y),
                    std::min(selectionPos1.z, selectionPos2.z)
                );
                visualSelMax = glm::ivec3(
                    std::max(selectionPos1.x, selectionPos2.x),
                    std::max(selectionPos1.y, selectionPos2.y),
                    std::max(selectionPos1.z, selectionPos2.z)
                );
                visualSelValid = true;
            };

            auto nudgeVisualSelection = [&](const glm::ivec3& delta) {
                if (selectionState == SelectionState::Idle || !visualSelValid) return;
                selectionPos1 += delta;
                selectionPos2 += delta;
                if (selectionState == SelectionState::Selecting) {
                    selectionState = SelectionState::Locked;
                }
                updateVisualSelection();
            };

            auto doVisualFill = [&]() {
                if (selectionState != SelectionState::Locked || !visualSelValid) return;
                BuildAction action;
                chunkManager.BeginBulkEdit();
                for (int x = visualSelMin.x; x <= visualSelMax.x; ++x) {
                    for (int y = visualSelMin.y; y <= visualSelMax.y; ++y) {
                        for (int z = visualSelMin.z; z <= visualSelMax.z; ++z) {
                            const glm::ivec3 p(x, y, z);
                            const uint8_t prev = chunkManager.GetBlockWorld(p);
                            if (prev == patternBlockId) continue;
                            chunkManager.SetBlockWorld(p, patternBlockId);
                            action.changes.push_back(BlockChange{p, prev, patternBlockId});
                        }
                    }
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
            };

            auto doVisualCopy = [&]() {
                if (selectionState != SelectionState::Locked || !visualSelValid) return;
                clipboard.clear();
                clipboardValid = false;
                bool first = true;
                int stored = 0;
                for (int x = visualSelMin.x; x <= visualSelMax.x; ++x) {
                    for (int y = visualSelMin.y; y <= visualSelMax.y; ++y) {
                        for (int z = visualSelMin.z; z <= visualSelMax.z; ++z) {
                            const glm::ivec3 p(x, y, z);
                            const uint8_t id = chunkManager.GetBlockWorld(p);
                            if (id == 0) continue;
                            if (stored >= clipboardMaxBlocks) break;
                            const glm::ivec3 offset = p - visualSelMin;
                            clipboard.push_back(ClipboardVoxel{offset, id});
                            if (first) {
                                clipboardMin = offset;
                                clipboardMax = offset;
                                first = false;
                            } else {
                                clipboardMin = glm::ivec3(
                                    std::min(clipboardMin.x, offset.x),
                                    std::min(clipboardMin.y, offset.y),
                                    std::min(clipboardMin.z, offset.z)
                                );
                                clipboardMax = glm::ivec3(
                                    std::max(clipboardMax.x, offset.x),
                                    std::max(clipboardMax.y, offset.y),
                                    std::max(clipboardMax.z, offset.z)
                                );
                            }
                            stored++;
                        }
                        if (stored >= clipboardMaxBlocks) break;
                    }
                    if (stored >= clipboardMaxBlocks) break;
                }
                clipboardValid = !clipboard.empty();
            };

            auto doVisualCut = [&]() {
                if (selectionState != SelectionState::Locked || !visualSelValid) return;
                doVisualCopy();
                if (!clipboardValid) return;
                BuildAction action;
                chunkManager.BeginBulkEdit();
                for (int x = visualSelMin.x; x <= visualSelMax.x; ++x) {
                    for (int y = visualSelMin.y; y <= visualSelMax.y; ++y) {
                        for (int z = visualSelMin.z; z <= visualSelMax.z; ++z) {
                            const glm::ivec3 p(x, y, z);
                            const uint8_t prev = chunkManager.GetBlockWorld(p);
                            if (prev == 0) continue;
                            chunkManager.SetBlockWorld(p, 0);
                            action.changes.push_back(BlockChange{p, prev, 0});
                        }
                    }
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
            };

            auto doVisualPaste = [&](const glm::ivec3& anchor) {
                if (!clipboardValid) return;
                BuildAction action;
                chunkManager.BeginBulkEdit();
                for (const auto& voxel : clipboard) {
                    const glm::ivec3 p = anchor + voxel.offset;
                    const uint8_t prev = chunkManager.GetBlockWorld(p);
                    if (prev == voxel.id) continue;
                    chunkManager.SetBlockWorld(p, voxel.id);
                    action.changes.push_back(BlockChange{p, prev, voxel.id});
                }
                chunkManager.EndBulkEdit();
                pushAction(std::move(action));
            };

            if (visualToolActive) {
                if (visualTool == VisualTool::Selection) {
                    if (lmbClicked && aimHit.hit) {
                        if (selectionState == SelectionState::Idle || selectionState == SelectionState::Locked) {
                            selectionPos1 = aimHit.blockWorld;
                            selectionPos2 = selectionPos1;
                            selectionState = SelectionState::Selecting;
                        } else if (selectionState == SelectionState::Selecting) {
                            selectionPos2 = aimHit.blockWorld;
                            selectionState = SelectionState::Locked;
                        }
                    }

                    if (selectionState == SelectionState::Selecting) {
                        if (aimHit.hit) {
                            selectionPos2 = aimHit.blockWorld;
                        } else if (hasLastAimHit) {
                            selectionPos2 = lastAimHit.blockWorld;
                        }
                    }
                }

                if (backDownVisual && !lastBackspaceVisual) {
                    selectionState = SelectionState::Idle;
                    visualSelValid = false;
                }

                if (visualTool == VisualTool::Pattern && lmbClicked && aimHit.hit && aimHit.blockId != 0) {
                    patternBlockId = aimHit.blockId;
                }

                if (visualTool == VisualTool::Fill && lmbClicked) {
                    doVisualFill();
                }

                if ((visualTool == VisualTool::Copy || visualTool == VisualTool::Cut) && lmbClicked) {
                    if (visualTool == VisualTool::Copy) {
                        doVisualCopy();
                    } else {
                        doVisualCut();
                    }
                }

                if (visualTool == VisualTool::Paste) {
                    Game::World::RaycastHit ghostHit = aimHit;
                    if (!ghostHit.hit && hasLastAimHit) {
                        ghostHit = lastAimHit;
                    }
                    if (ghostHit.hit) {
                        pasteAnchor = ghostHit.prevWorld;
                        pasteAnchorValid = true;
                    } else {
                        pasteAnchorValid = false;
                    }

                    if (lmbClicked && pasteAnchorValid) {
                        doVisualPaste(pasteAnchor);
                    }
                }
            } else {
                pasteAnchorValid = false;
            }

            if (selectionState != SelectionState::Idle) {
                updateVisualSelection();
            }

            lastBackspaceVisual = backDownVisual;

            auto beginEditorBatch = [&]() {
                if (!editorSelValid || editorBatch.active) return;
                editorBatch.active = true;
                editorBatch.mode = editorMode;
                editorBatch.fillId = selectedBlockId;
                editorBatch.targetId = editorTargetId;
                editorBatch.min = editorSelMin;
                editorBatch.max = editorSelMax;
                editorBatch.cursor = editorBatch.min;
                editorBatch.visited = 0;
                editorBatch.total = (std::uint64_t)(editorBatch.max.x - editorBatch.min.x + 1) *
                                    (std::uint64_t)(editorBatch.max.y - editorBatch.min.y + 1) *
                                    (std::uint64_t)(editorBatch.max.z - editorBatch.min.z + 1);
                editorBatch.action.changes.clear();
            };

            if (editorInputReady && lmbClicked && aimHit.hit) {
                editorDragActive = true;
                editorStart = aimHit.blockWorld;
                editorEnd = editorStart;
                editorStartValid = true;
                editorEndValid = true;
                editorDragNormal = snapNormal(aimHit.prevWorld - aimHit.blockWorld);
                editorDragModeUsed = editorDragMode;
            }

            if (editorInputReady && editorDragActive && lmbDown) {
                Game::World::RaycastHit dragHit = aimHit;
                if (!dragHit.hit && hasLastAimHit) dragHit = lastAimHit;
                if (dragHit.hit) {
                    editorEnd = dragHit.blockWorld;
                    editorEndValid = true;
                }
            }

            if (editorInputReady && editorStartValid && editorEndValid) {
                computeEditorSelection(editorStart, editorEnd, editorDragModeUsed, editorDragNormal, editorSelMin, editorSelMax);
                editorSelValid = true;
            }

            if (editorInputReady && !lmbDown && lastLmb && editorDragActive) {
                editorDragActive = false;
                if (editorSelValid) beginEditorBatch();
            }

            if (editorInputReady && enterDownEditor && !lastEditorEnter) {
                if (editorSelValid) beginEditorBatch();
            }

            if (!editorEnabled && !consumeBuildInput && gameplayArmed && mouseCaptured && !justCaptured && !inventoryOpen && (lmbDown || rmbDown) && aimHit.hit) {
                const double t = glfwGetTime();
                const bool hitChanged = (aimHit.blockWorld.x != lastRaycastBlock.x) || (aimHit.blockWorld.y != lastRaycastBlock.y) || (aimHit.blockWorld.z != lastRaycastBlock.z) || (aimHit.blockId != lastRaycastBlockId);
                const bool allowLog = logHits && ((lastRaycastLogTime < 0.0) || (t - lastRaycastLogTime) >= 0.20 || hitChanged);
                if (allowLog) {
                    std::cout << "Hit block id=" << int(aimHit.blockId)
                              << " at (" << aimHit.blockWorld.x << "," << aimHit.blockWorld.y << "," << aimHit.blockWorld.z << ")"
                              << std::endl;
                    lastRaycastLogTime = t;
                    lastRaycastBlock = aimHit.blockWorld;
                    lastRaycastBlockId = aimHit.blockId;
                }

                const double breakInterval = 1.0 / (double)breakRate;
                const double placeInterval = 1.0 / (double)placeRate;

                if (lmbDown) {
                    if (lmbClicked) {
                        breakPressStartTime = t;
                    }
                    // Start destruction immediately on click, or continuously if held (limited by breakRate)
                    const bool holdReady = (breakPressStartTime >= 0.0) && ((t - breakPressStartTime) >= (double)breakHoldDelay);
                    const bool allowBreak = lmbClicked || ((holdReady || lastBreakTime < 0.0) && ((t - lastBreakTime) >= breakInterval));
                    if (allowBreak) {
                        BuildAction action;
                        tryBreak(aimHit.blockWorld, action);
                        pushAction(std::move(action));
                        lastBreakTime = t;
                    }
                } else {
                    breakPressStartTime = -1.0;
                }

                if (rmbDown) {
                    if (rmbClicked) {
                        placePressStartTime = t;
                        holdPlaceActive = true;
                        holdPlaceAnchor = placeHit.prevWorld;
                        holdPlaceLast = holdPlaceAnchor;
                        holdPlaceAxis = -1;
                    }

                    const glm::ivec3 currentPlaceTarget = placeHit.prevWorld;
                    if (!hasLastPlaceTarget || currentPlaceTarget != lastPlaceTarget) {
                        lastPlaceTarget = currentPlaceTarget;
                        hasLastPlaceTarget = true;
                        if (!rmbClicked) {
                            placePressStartTime = t;
                        }
                    }

                    if (!simpleBuild && precisionBuild) {
                        if (rmbClicked) {
                            if (!precisionAnchor) {
                                precisionAnchor = true;
                                precisionAnchorPos = placeHit.prevWorld;
                                precisionAnchorNormal = placeHit.prevWorld - placeHit.blockWorld;
                            } else {
                                if (buildMode == BuildMode::Plane) {
                                    const int placed = placePlane(precisionAnchorPos, placeHit.prevWorld, precisionAnchorNormal);
                                    if (placed >= placeMax) {
                                        std::cout << "Precision build: capped at " << placeMax << " blocks" << std::endl;
                                    }
                                } else if (buildMode == BuildMode::Line) {
                                    placeLine(precisionAnchorPos, placeHit.prevWorld);
                                } else {
                                    placeBox(precisionAnchorPos, placeHit.prevWorld);
                                }
                                precisionAnchor = false;
                            }
                        }
                    } else {
                        // Continuous placement if held, or immediate if clicked
                        const bool holdReady = (placePressStartTime >= 0.0) && ((t - placePressStartTime) >= (double)(placeHoldDelay + placeRetargetDelay));
                        const bool verticalFollowBoost = holdPlaceActive && holdPlaceAxis == 1 && (placeHit.prevWorld != holdPlaceLast);
                        const bool allowPlace = verticalFollowBoost || rmbClicked || ((holdReady || lastPlaceTime < 0.0) && ((t - lastPlaceTime) >= placeInterval));
                        if (allowPlace) {
                            BuildAction action;
                            if (!holdPlaceActive) {
                                holdPlaceActive = true;
                                holdPlaceAnchor = placeHit.prevWorld;
                                holdPlaceLast = holdPlaceAnchor;
                                holdPlaceAxis = -1;
                            }

                            const glm::ivec3 rawTarget = placeHit.prevWorld;
                            glm::ivec3 snappedTarget = rawTarget;

                            if (holdPlaceAxis < 0) {
                                const glm::ivec3 d = rawTarget - holdPlaceAnchor;
                                const int ax = std::abs(d.x);
                                const int ay = std::abs(d.y);
                                const int az = std::abs(d.z);
                                const int maxD = std::max(ax, std::max(ay, az));
                                if (maxD >= 1) {
                                    // Prefer Y on ties so vertical tower building is recognized quickly.
                                    if (ay >= ax && ay >= az) holdPlaceAxis = 1;
                                    else if (ax >= az) holdPlaceAxis = 0;
                                    else holdPlaceAxis = 2;
                                } else {
                                    // Keep first point stable until movement clearly indicates direction.
                                    snappedTarget = holdPlaceAnchor;
                                }
                            }

                            if (holdPlaceAxis == 0) {
                                snappedTarget.y = holdPlaceAnchor.y;
                                snappedTarget.z = holdPlaceAnchor.z;
                            } else if (holdPlaceAxis == 1) {
                                snappedTarget.x = holdPlaceAnchor.x;
                                snappedTarget.z = holdPlaceAnchor.z;
                            } else if (holdPlaceAxis == 2) {
                                snappedTarget.x = holdPlaceAnchor.x;
                                snappedTarget.y = holdPlaceAnchor.y;
                            }

                            chunkManager.BeginBulkEdit();
                            if (holdPlaceAxis < 0) {
                                (void)tryPlace(snappedTarget, action);
                                holdPlaceLast = snappedTarget;
                            } else {
                                const int startCoord = (holdPlaceAxis == 0) ? holdPlaceLast.x : (holdPlaceAxis == 1) ? holdPlaceLast.y : holdPlaceLast.z;
                                const int endCoord = (holdPlaceAxis == 0) ? snappedTarget.x : (holdPlaceAxis == 1) ? snappedTarget.y : snappedTarget.z;
                                const int step = (endCoord >= startCoord) ? 1 : -1;
                                int coord = startCoord;
                                int placedThisTick = 0;
                                while (true) {
                                    glm::ivec3 p = holdPlaceAnchor;
                                    if (holdPlaceAxis == 0) p.x = coord;
                                    else if (holdPlaceAxis == 1) p.y = coord;
                                    else p.z = coord;

                                    if (tryPlace(p, action)) {
                                        placedThisTick++;
                                        if (placedThisTick >= placeMax) {
                                            break;
                                        }
                                    }

                                    if (coord == endCoord) break;
                                    coord += step;
                                }
                                holdPlaceLast = snappedTarget;
                            }
                            chunkManager.EndBulkEdit();
                            pushAction(std::move(action));
                            lastPlaceTime = t;
                        }
                    }
                } else {
                    placePressStartTime = -1.0;
                    holdPlaceActive = false;
                    holdPlaceAxis = -1;
                }
            }

            lastLmb = lmbDown;
            lastRmb = rmbDown;
            lastMmb = mmbDown;
            if (!rmbDown) {
                hasLastPlaceTarget = false;
                holdPlaceActive = false;
                holdPlaceAxis = -1;
            }

            if (controlObsEnabled) {
                const float dtCtrl = std::max(0.0001f, deltaTime);
                const bool controlObsLive = worldStarted && !showLoadingOverlay && worldRenderer.GetChunkCount() >= 24;
                const bool keyForwardDown = keyForwardDownFrame;
                const bool keyBackDown = keyBackDownFrame;
                const bool keyLeftDown = keyLeftDownFrame;
                const bool keyRightDown = keyRightDownFrame;
                const bool keyUpDown = !walkMode && keyUpDownFrame;
                const bool keyDownDown = !walkMode && keyDownDownFrame;

                controlHoldForwardSec = keyForwardDown ? (controlHoldForwardSec + dtCtrl) : 0.0f;
                controlHoldBackSec = keyBackDown ? (controlHoldBackSec + dtCtrl) : 0.0f;
                controlHoldLeftSec = keyLeftDown ? (controlHoldLeftSec + dtCtrl) : 0.0f;
                controlHoldRightSec = keyRightDown ? (controlHoldRightSec + dtCtrl) : 0.0f;
                controlHoldLmbSec = lmbDown ? (controlHoldLmbSec + dtCtrl) : 0.0f;
                controlHoldRmbSec = rmbDown ? (controlHoldRmbSec + dtCtrl) : 0.0f;

                const int intentAxes = (keyForwardDown ? 1 : 0) + (keyBackDown ? 1 : 0) + (keyLeftDown ? 1 : 0) + (keyRightDown ? 1 : 0) + (keyUpDown ? 1 : 0) + (keyDownDown ? 1 : 0);
                const float intentNow = std::clamp((float)intentAxes / 2.0f, 0.0f, 1.0f);
                const float intentBlend = std::clamp(dtCtrl * 7.0f, 0.0f, 1.0f);
                controlIntentEma += (intentNow - controlIntentEma) * intentBlend;
                controlIntentActiveSec = (intentNow > 0.25f) ? (controlIntentActiveSec + dtCtrl) : 0.0f;

                const float moveDist = glm::length(camera.Position - prePhysicsPos);
                const float moveSpeed = moveDist / dtCtrl;
                const float expectedMoveSpeed = std::max(0.75f, camera.MovementSpeed * (walkMode ? 0.65f : 0.45f));
                float responseInst = 1.0f;
                if (intentNow > 0.01f) {
                    responseInst = std::clamp(moveSpeed / (expectedMoveSpeed * std::max(0.35f, intentNow)), 0.0f, 1.2f);
                } else {
                    responseInst = std::clamp(1.0f - (moveSpeed / (expectedMoveSpeed + 0.001f)), 0.0f, 1.0f);
                }
                const float responseBlend = std::clamp(dtCtrl * 6.0f, 0.0f, 1.0f);
                controlMoveResponseEma += (responseInst - controlMoveResponseEma) * responseBlend;

                const bool intentActive = controlObsLive && intentNow > 0.25f;
                const bool intentSustained = intentActive && controlIntentActiveSec >= controlDelayIntentMinSec;
                if (intentActive && !controlAwaitMove) {
                    if (intentSustained) {
                        controlAwaitMove = true;
                        controlAwaitMoveStart = now;
                        controlMoveDelayMsLast = 0;
                    }
                }
                if (!intentActive) {
                    controlAwaitMove = false;
                    controlAwaitMoveStart = -1.0;
                } else if (controlAwaitMove && moveSpeed >= expectedMoveSpeed * 0.35f) {
                    const long long delayRawMs = std::llround((now - controlAwaitMoveStart) * 1000.0);
                    const int delayMs = (int)std::clamp<long long>(delayRawMs, 0ll, 4000ll);
                    controlMoveDelayMsLast = (delayMs >= controlDelayReportMinMs) ? delayMs : 0;
                    if (delayMs >= controlDelayReportMinMs) {
                        controlMoveDelayMsMaxSec = std::max(controlMoveDelayMsMaxSec, delayMs);
                    }
                    controlAwaitMove = false;
                    controlAwaitMoveStart = -1.0;
                } else if (controlAwaitMove && controlAwaitMoveStart >= 0.0) {
                    const long long delayRawMs = std::llround((now - controlAwaitMoveStart) * 1000.0);
                    const int delayMs = (int)std::clamp<long long>(delayRawMs, 0ll, 4000ll);
                    controlMoveDelayMsLast = (delayMs >= controlDelayReportMinMs) ? delayMs : 0;
                    if (delayMs >= controlDelayReportMinMs) {
                        controlMoveDelayMsMaxSec = std::max(controlMoveDelayMsMaxSec, delayMs);
                    }
                }

                const float yawAbs = std::abs(camera.Yaw - lastFrameYaw);
                const float pitchAbs = std::abs(camera.Pitch - lastFramePitch);
                const float rotDegPerSec = (yawAbs + pitchAbs) / dtCtrl;
                const float rotBlend = std::clamp(dtCtrl * 5.0f, 0.0f, 1.0f);
                controlRotDegPerSecEma += (rotDegPerSec - controlRotDegPerSecEma) * rotBlend;

                const float frameMsNow = dtCtrl * 1000.0f;
                const float lagInstMs = std::max(0.0f, (1.0f - responseInst) * 26.0f) + std::max(0.0f, frameMsNow - 16.7f);
                const float lagBlend = std::clamp(dtCtrl * 6.0f, 0.0f, 1.0f);
                controlLagMsEma += (lagInstMs - controlLagMsEma) * lagBlend;

                if (keyForwardDown) controlDistForwardMetersCounter += moveDist;
                if (keyBackDown) controlDistBackMetersCounter += moveDist;
                if (keyLeftDown) controlDistLeftMetersCounter += moveDist;
                if (keyRightDown) controlDistRightMetersCounter += moveDist;

                controlPlaceOpsCounter += controlPlaceOpsThisFrame;
                controlBreakOpsCounter += controlBreakOpsThisFrame;
            }
            lastFrameYaw = camera.Yaw;
            lastFramePitch = camera.Pitch;
            if (!menuVisible) {
                // Keep menu click edge state in sync while menu isn't visible.
                lastMenuLmb = lmbDown;
            }

            if (editorBatch.active) {
                int budget = editorBatchPerFrame;
                auto advanceCursor = [&]() {
                    editorBatch.cursor.x++;
                    if (editorBatch.cursor.x > editorBatch.max.x) {
                        editorBatch.cursor.x = editorBatch.min.x;
                        editorBatch.cursor.z++;
                        if (editorBatch.cursor.z > editorBatch.max.z) {
                            editorBatch.cursor.z = editorBatch.min.z;
                            editorBatch.cursor.y++;
                        }
                    }
                };

                while (budget > 0 && editorBatch.cursor.y <= editorBatch.max.y) {
                    const glm::ivec3 p = editorBatch.cursor;
                    const bool boundary = (p.x == editorBatch.min.x || p.x == editorBatch.max.x ||
                                           p.y == editorBatch.min.y || p.y == editorBatch.max.y ||
                                           p.z == editorBatch.min.z || p.z == editorBatch.max.z);

                    if (editorBatch.mode == EditorMode::Hollow && !boundary) {
                        advanceCursor();
                        editorBatch.visited++;
                        budget--;
                        continue;
                    }

                    const uint8_t prev = chunkManager.GetBlockWorld(p);
                    bool shouldWrite = false;
                    uint8_t desired = prev;

                    if (editorBatch.mode == EditorMode::Fill || editorBatch.mode == EditorMode::Hollow) {
                        desired = editorBatch.fillId;
                        shouldWrite = true;
                    } else if (editorBatch.mode == EditorMode::Replace) {
                        if (prev == editorBatch.targetId) {
                            desired = editorBatch.fillId;
                            shouldWrite = true;
                        }
                    } else if (editorBatch.mode == EditorMode::Remove) {
                        if (prev != 0) {
                            desired = 0;
                            shouldWrite = true;
                        }
                    }

                    if (shouldWrite && prev != desired) {
                        chunkManager.SetBlockWorld(p, desired);
                        editorBatch.action.changes.push_back(BlockChange{p, prev, desired});
                    }

                    advanceCursor();
                    editorBatch.visited++;
                    budget--;
                }

                if (editorBatch.cursor.y > editorBatch.max.y) {
                    editorBatch.active = false;
                    pushAction(std::move(editorBatch.action));
                }
            }

            if (traceCameraHz > 0) {
                const double ct = glfwGetTime();
                const double interval = 1.0 / (double)traceCameraHz;
                if (lastCamTraceTime < 0.0 || (ct - lastCamTraceTime) >= interval) {
                    Telemetry::RecordCamera(ct, camera.Position, camera.Yaw, camera.Pitch);
                    lastCamTraceTime = ct;
                }
            }

            // Render
            const float tNow = (float)glfwGetTime();
            const float sunPhase = tNow * 0.1f;
            const float timeOfDay = dayNightCycle ? std::fmod(tNow * 0.01f, 1.0f) : 0.25f;
            const glm::vec3 sunDir = dayNightCycle
                ? glm::normalize(glm::vec3(std::sin(sunPhase), 0.18f + 0.92f * std::sin(sunPhase), std::cos(sunPhase)))
                : glm::normalize(glm::vec3(0.35f, 0.78f, 0.52f));
            const float dayFactorFog = std::clamp(sunDir.y * 0.5f + 0.5f, 0.0f, 1.0f);
            const float fogDensityRaw = fogDensityBase * (1.0f + (1.0f - dayFactorFog) * (fogNightBoost - 1.0f));
            const glm::vec3 fogDayColor(0.66f, 0.76f, 0.90f);
            const glm::vec3 fogNightColor(0.05f, 0.07f, 0.11f);
            const glm::vec3 fogColor = glm::mix(fogNightColor, fogDayColor, dayFactorFog);
            const std::size_t frameChunkCount = worldRenderer.GetChunkCount();
            const std::size_t frameTriCount = worldRenderer.GetLastTriangleCount();
            const std::size_t frameDrawCount = worldRenderer.GetLastDrawCount();
            const bool renderStarvedFrame = worldStarted
                && (frameChunkCount >= 768)
                && (frameTriCount == 0)
                && (frameDrawCount == 0);
            const bool pendingEmergency = stutterAlertPendingOldTicks > 0
                && streamPendingOldestTicks > (std::size_t)std::max(240, stutterAlertPendingOldTicks * 3);
            static double farfieldSuppressUntil = -1.0;
            if (renderStarvedFrame || pendingEmergency) {
                farfieldSuppressUntil = std::max(farfieldSuppressUntil, now + (double)farfieldSuppressHoldSec);
            }
            const bool farfieldSuppressed = (farfieldSuppressUntil > now);
            float fogDensity = fogDensityRaw;
            if (fogHighAltitudeRelief && fogHighAltEnd > fogHighAltStart) {
                const float tAlt = std::clamp((camera.Position.y - fogHighAltStart) / (fogHighAltEnd - fogHighAltStart), 0.0f, 1.0f);
                const float altScale = 1.0f - (1.0f - fogHighAltMinScale) * tAlt;
                fogDensity *= altScale;
            }
            if (fogStarvationRelief && (renderStarvedFrame || pendingEmergency)) {
                const float emergencyScale = 0.75f;
                fogDensity = std::max(0.00001f, fogDensity * std::max(fogStarvationMinScale, emergencyScale));
            }
            static int stableRenderCullChunks = 0;
            int renderCullTarget = std::max(viewDistanceChunks, activeStreamDistance);
            if (renderStarvedFrame || pendingEmergency) {
                renderCullTarget = std::max(renderCullTarget, std::max(24, viewDistanceChunks + 4));
            }
            if (stableRenderCullChunks <= 0) {
                stableRenderCullChunks = renderCullTarget;
            } else if (renderCullTarget < stableRenderCullChunks) {
                stableRenderCullChunks = std::max(renderCullTarget, stableRenderCullChunks - 1);
            } else if (renderCullTarget > stableRenderCullChunks) {
                stableRenderCullChunks = std::min(renderCullTarget, stableRenderCullChunks + 2);
            }
            const int renderCullChunks = wireframe ? std::min(stableRenderCullChunks, wireframeCullChunksCfg) : stableRenderCullChunks;
            const float nearFieldCullDistance = std::max(64.0f, (float)(renderCullChunks + 2) * (float)Game::World::CHUNK_SIZE);
            impostorQueuePressureLive = 0.0f;
            impostorBlendNearStartLive = impostorBlendNearStart;
            impostorBlendNearEndLive = std::max(impostorBlendNearStart + 64.0f, impostorBlendNearEnd);

            if (skyProg != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glDisable(GL_BLEND);

                glUseProgram(skyProg);
                const GLint locSun = glGetUniformLocation(skyProg, "u_SunDir");
                const GLint locTod = glGetUniformLocation(skyProg, "u_TimeOfDay");
                if (locSun >= 0) glUniform3f(locSun, sunDir.x, sunDir.y, sunDir.z);
                if (locTod >= 0) glUniform1f(locTod, timeOfDay);

                glBindVertexArray(skyVao);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                glUseProgram(0);

                glDepthMask(GL_TRUE);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            if (horizonTerrainEnabled && horizonProg != 0 && !horizonLevels.empty() && !wireframe && !farfieldSuppressed) {
                int clipBudget = horizonPointsPerFrame;
                if (horizonAdaptiveBudget) {
                    const float frameMsAdaptive = deltaTime * 1000.0f;
                    if (frameMsAdaptive > 26.0f) {
                        clipBudget = std::max(256, horizonPointsPerFrame / 3);
                    } else if (frameMsAdaptive > 19.0f) {
                        clipBudget = std::max(512, horizonPointsPerFrame / 2);
                    } else if (frameMsAdaptive < 12.0f) {
                        clipBudget = std::min(65536, horizonPointsPerFrame + horizonPointsPerFrame / 2);
                    }
                }
                if (fastInputPriorityActive) {
                    clipBudget = std::max(384, clipBudget / 2);
                }
                updateHorizonTerrain(camera.Position, now, clipBudget, false);

                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
                if (!hadDepth) glEnable(GL_DEPTH_TEST);
                if (hadBlend) glDisable(GL_BLEND);
                if (hadCull) glDisable(GL_CULL_FACE);
                glDepthMask(GL_TRUE);

                glUseProgram(horizonProg);
                const glm::mat4 vp = projection * camera.GetViewMatrix();
                const GLint locVP = glGetUniformLocation(horizonProg, "u_ViewProjection");
                const GLint locView = glGetUniformLocation(horizonProg, "u_ViewPos");
                const GLint locSun = glGetUniformLocation(horizonProg, "u_SunDir");
                const GLint locFogColor = glGetUniformLocation(horizonProg, "u_FogColor");
                const GLint locFogDensity = glGetUniformLocation(horizonProg, "u_FogDensity");
                if (locVP >= 0) glUniformMatrix4fv(locVP, 1, GL_FALSE, &vp[0][0]);
                if (locView >= 0) glUniform3f(locView, camera.Position.x, camera.Position.y, camera.Position.z);
                if (locSun >= 0) glUniform3f(locSun, sunDir.x, sunDir.y, sunDir.z);
                if (locFogColor >= 0) glUniform3f(locFogColor, fogColor.x, fogColor.y, fogColor.z);
                if (locFogDensity >= 0) glUniform1f(locFogDensity, fogDensity);

                for (std::size_t li = horizonLevels.size(); li-- > 0;) {
                    const HorizonLevel& level = horizonLevels[li];
                    if (!level.ready || level.vao == 0 || level.indices.empty()) continue;
                    glBindVertexArray(level.vao);
                    glDrawElements(GL_TRIANGLES, (GLsizei)level.indices.size(), GL_UNSIGNED_INT, (const void*)0);
                }
                glBindVertexArray(0);
                glUseProgram(0);

                if (hadCull) glEnable(GL_CULL_FACE);
                if (hadBlend) glEnable(GL_BLEND);
                if (!hadDepth) glDisable(GL_DEPTH_TEST);
            }

            if (impostorRingEnabled && impostorProg != 0 && impostorVao != 0 && impostorIndexCount > 0 && !wireframe && !farfieldSuppressed) {
                int cadenceDiv = 1;
                int activeBands = impostorRingBands;
                if (impostorAdaptiveBudget) {
                    const float frameMsAdaptive = deltaTime * 1000.0f;
                    if (lowEndController && lowEndLevel >= 3) {
                        cadenceDiv = 3;
                        activeBands = std::max(2, impostorRingBands / 2);
                    } else if (frameMsAdaptive > 24.0f) {
                        cadenceDiv = 3;
                        activeBands = std::max(2, impostorRingBands / 2);
                    } else if (frameMsAdaptive > 18.0f) {
                        cadenceDiv = 2;
                        activeBands = std::max(3, (impostorRingBands * 3) / 4);
                    }

                    const bool controlPressure = impostorControlProtect
                        && ((controlIntentEma > 0.42f)
                            || (controlLagMsEma >= (controlLagWarnMs * 1.20f))
                            || (stutterLatencyScore >= 45));
                    if (controlPressure) {
                        cadenceDiv = std::max(cadenceDiv, std::max(2, impostorLowpcMaxCadence - 1));
                        activeBands = std::max(impostorLowpcMinBands, impostorRingBands / 3);
                    }
                }
                if (fastInputPriorityActive) {
                    cadenceDiv = std::max(cadenceDiv, 2);
                    activeBands = std::max(impostorLowpcMinBands, activeBands - 2);
                }

                ++impostorFrameCounter;
                if ((impostorFrameCounter % (std::uint64_t)std::max(1, cadenceDiv)) == 0u) {
                    const int clampedBands = std::clamp(activeBands, impostorLowpcMinBands, impostorRingBands);
                    const GLsizei drawCount = (GLsizei)(clampedBands * impostorRingSegments * 6);
                    const float outerR = std::max(impostorInnerRadius + 32.0f, impostorOuterRadius);
                    float nearStart = impostorBlendNearStart;
                    float nearEnd = std::max(nearStart + 64.0f, impostorBlendNearEnd);
                    if (impostorDynamicBlend) {
                        nearStart = std::max(impostorDynamicMinStart, nearFieldCullDistance * impostorDynamicNearScale);
                        nearEnd = std::max(nearStart + 64.0f, nearStart + impostorDynamicNearWidth);
                        nearEnd = std::min(nearEnd, outerR - 32.0f);
                        nearStart = std::min(nearStart, nearEnd - 32.0f);
                    }

                    float queuePressure = 0.0f;
                    float effectiveQueuePush = impostorQueuePressurePush * impostorQueuePushMulLive;
                    if (impostorQueuePressureBlend) {
                        const float pendingP = std::clamp((float)streamPendingOldestTicks / (float)std::max(1, stutterAlertPendingOldTicks), 0.0f, 2.0f) * 0.34f;
                        const float overdueP = std::clamp((float)streamWatchdogOverdue / (float)std::max(1, stutterAlertOverdueChunks), 0.0f, 2.0f) * 0.34f;
                        const float fixedP = (stutterAlertFixedSatSec > 0)
                            ? std::clamp((float)fixedStepSaturatedFramesSec / (float)std::max(1, stutterAlertFixedSatSec), 0.0f, 2.0f) * 0.18f
                            : 0.0f;
                        const float latencyP = std::clamp((float)stutterLatencyScore / 100.0f, 0.0f, 1.0f) * 0.10f;
                        const float controlP = std::clamp((controlLagMsEma - impostorQueuePressureLagMs) / std::max(8.0f, impostorQueuePressureLagMs), 0.0f, 1.0f) * 0.16f;
                        queuePressure = std::clamp(pendingP + overdueP + fixedP + latencyP + controlP, 0.0f, 1.0f);

                        nearStart = std::min(outerR - 96.0f, nearStart + effectiveQueuePush * queuePressure);
                        nearEnd = std::min(outerR - 32.0f, nearEnd + effectiveQueuePush * 1.20f * queuePressure);
                        nearStart = std::min(nearStart, nearEnd - 32.0f);
                    }

                    if (impostorAutoTunePush) {
                        if (impostorAutoTuneLast < 0.0) {
                            impostorAutoTuneLast = now;
                        }
                        impostorQueuePressureAccum += queuePressure;
                        impostorQueuePressureSamples += 1;
                        if ((now - impostorAutoTuneLast) >= (double)impostorAutoTuneIntervalSec && impostorQueuePressureSamples >= impostorAutoTuneMinSamples) {
                            const float avgPressure = impostorQueuePressureAccum / (float)std::max(1, impostorQueuePressureSamples);
                            impostorQueuePressureAvgLive = avgPressure;
                            const float error = avgPressure - impostorAutoTuneTarget;
                            impostorQueuePushMulLive = std::clamp(
                                impostorQueuePushMulLive + error * impostorAutoTuneGain,
                                impostorAutoTuneMulMin,
                                impostorAutoTuneMulMax
                            );
                            impostorQueuePressureAccum = 0.0f;
                            impostorQueuePressureSamples = 0;
                            impostorAutoTuneLast = now;
                        } else if (impostorQueuePressureSamples > 0) {
                            impostorQueuePressureAvgLive = impostorQueuePressureAccum / (float)impostorQueuePressureSamples;
                        }
                    } else {
                        impostorQueuePressureAvgLive = queuePressure;
                        impostorQueuePushMulLive = 1.0f;
                    }
                    impostorQueuePressureLive = queuePressure;
                    impostorBlendNearStartLive = nearStart;
                    impostorBlendNearEndLive = nearEnd;

                    const float farStartT = std::clamp(
                        std::max(impostorBlendFarStart, (nearEnd / std::max(outerR, 1.0f)) + 0.08f + queuePressure * 0.05f),
                        0.20f,
                        0.995f
                    );

                    const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
                    if (!hadDepth) glEnable(GL_DEPTH_TEST);
                    if (!hadBlend) glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    if (hadCull) glDisable(GL_CULL_FACE);
                    glDepthMask(GL_FALSE);

                    glUseProgram(impostorProg);
                    const glm::mat4 vp = projection * camera.GetViewMatrix();
                    const GLint locVP = glGetUniformLocation(impostorProg, "u_ViewProjection");
                    const GLint locView = glGetUniformLocation(impostorProg, "u_ViewPos");
                    const GLint locSun = glGetUniformLocation(impostorProg, "u_SunDir");
                    const GLint locFogColor = glGetUniformLocation(impostorProg, "u_FogColor");
                    const GLint locFogDensity = glGetUniformLocation(impostorProg, "u_FogDensity");
                    const GLint locBaseHeight = glGetUniformLocation(impostorProg, "u_BaseHeight");
                    const GLint locHeightAmp = glGetUniformLocation(impostorProg, "u_HeightAmp");
                    const GLint locNoiseScale = glGetUniformLocation(impostorProg, "u_NoiseScale");
                    const GLint locTime = glGetUniformLocation(impostorProg, "u_Time");
                    const GLint locNearStart = glGetUniformLocation(impostorProg, "u_BlendNearStart");
                    const GLint locNearEnd = glGetUniformLocation(impostorProg, "u_BlendNearEnd");
                    const GLint locFarStart = glGetUniformLocation(impostorProg, "u_BlendFarStart");
                    const GLint locMinAlpha = glGetUniformLocation(impostorProg, "u_BlendMinAlpha");

                    if (locVP >= 0) glUniformMatrix4fv(locVP, 1, GL_FALSE, &vp[0][0]);
                    if (locView >= 0) glUniform3f(locView, camera.Position.x, camera.Position.y, camera.Position.z);
                    if (locSun >= 0) glUniform3f(locSun, sunDir.x, sunDir.y, sunDir.z);
                    if (locFogColor >= 0) glUniform3f(locFogColor, fogColor.x, fogColor.y, fogColor.z);
                    if (locFogDensity >= 0) glUniform1f(locFogDensity, fogDensity);
                    if (locBaseHeight >= 0) glUniform1f(locBaseHeight, impostorBaseHeight);
                    if (locHeightAmp >= 0) glUniform1f(locHeightAmp, impostorHeightAmp);
                    if (locNoiseScale >= 0) glUniform1f(locNoiseScale, impostorNoiseScale);
                    if (locTime >= 0) glUniform1f(locTime, tNow);
                    if (locNearStart >= 0) glUniform1f(locNearStart, nearStart);
                    if (locNearEnd >= 0) glUniform1f(locNearEnd, nearEnd);
                    if (locFarStart >= 0) glUniform1f(locFarStart, farStartT);
                    if (locMinAlpha >= 0) glUniform1f(locMinAlpha, impostorBlendMinAlpha);

                    glBindVertexArray(impostorVao);
                    glDrawElements(GL_TRIANGLES, drawCount, GL_UNSIGNED_INT, (const void*)0);
                    glBindVertexArray(0);
                    glUseProgram(0);

                    glDepthMask(GL_TRUE);
                    if (hadCull) glEnable(GL_CULL_FACE);
                    if (!hadBlend) glDisable(GL_BLEND);
                    if (!hadDepth) glDisable(GL_DEPTH_TEST);
                }
            }

            shader.Use();
            shader.SetMat4("u_ViewProjection", projection * camera.GetViewMatrix());
            shader.SetVec3("u_ViewPos", camera.Position);
            shader.SetVec3("u_FogColor", fogColor);
            shader.SetFloat("u_FogDensity", fogDensity);
            shader.SetFloat("u_Time", tNow); // INVENTION: Animated water + fog scattering

            worldRenderer.PrepareVisibleDraws(projection * camera.GetViewMatrix(), camera.Position, nearFieldCullDistance);
            if (renderStarvedFrame || pendingEmergency) {
                const float fallbackCullDistance = std::max(
                    nearFieldCullDistance,
                    (float)(std::max(viewDistanceChunks + 6, 28) * Game::World::CHUNK_SIZE)
                );
                worldRenderer.PrepareVisibleDraws(projection * camera.GetViewMatrix(), camera.Position, fallbackCullDistance);
            }

            // Sun direction for directional lighting.
            shader.SetVec3("u_SunDir", sunDir);

            if (!bindlessActive) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                shader.SetInt("u_BlockAtlas", 0);
            }

            // Two-pass draw: opaque first, then transparent.
            // This fixes the biggest "water looks wrong" issue: blending against not-yet-drawn opaque geometry.
            glEnable(GL_DEPTH_TEST);

            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            if (wireframe) {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                shader.SetInt("u_RenderPass", 0);
                worldRenderer.DrawAll(shader);
            } else {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                shader.SetInt("u_RenderPass", 0);
                worldRenderer.DrawAll(shader);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                shader.SetInt("u_RenderPass", 1);
                worldRenderer.DrawAll(shader);
                glDepthMask(GL_TRUE);
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            if (visualSelValid && editorSelProg != 0 && editorSelVao != 0 && editorSelVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
                if (hadCull) glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                if (!hadDepth) glEnable(GL_DEPTH_TEST);

                const float x0 = (float)visualSelMin.x;
                const float y0 = (float)visualSelMin.y;
                const float z0 = (float)visualSelMin.z;
                const float x1 = (float)(visualSelMax.x + 1);
                const float y1 = (float)(visualSelMax.y + 1);
                const float z1 = (float)(visualSelMax.z + 1);

                std::vector<float> fillVerts;
                std::vector<float> lineVerts;
                fillVerts.reserve(36 * 3);
                lineVerts.reserve(24 * 3);

                auto pushTri = [&](float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz) {
                    fillVerts.insert(fillVerts.end(), {ax, ay, az, bx, by, bz, cx, cy, cz});
                };
                auto pushLine = [&](float ax, float ay, float az, float bx, float by, float bz) {
                    lineVerts.insert(lineVerts.end(), {ax, ay, az, bx, by, bz});
                };

                // Faces
                pushTri(x0, y0, z0, x1, y0, z0, x1, y1, z0);
                pushTri(x0, y0, z0, x1, y1, z0, x0, y1, z0);

                pushTri(x0, y0, z1, x1, y1, z1, x1, y0, z1);
                pushTri(x0, y0, z1, x0, y1, z1, x1, y1, z1);

                pushTri(x0, y0, z0, x0, y1, z1, x0, y0, z1);
                pushTri(x0, y0, z0, x0, y1, z0, x0, y1, z1);

                pushTri(x1, y0, z0, x1, y0, z1, x1, y1, z1);
                pushTri(x1, y0, z0, x1, y1, z1, x1, y1, z0);

                pushTri(x0, y0, z0, x1, y0, z1, x1, y0, z0);
                pushTri(x0, y0, z0, x0, y0, z1, x1, y0, z1);

                pushTri(x0, y1, z0, x1, y1, z0, x1, y1, z1);
                pushTri(x0, y1, z0, x1, y1, z1, x0, y1, z1);

                // Outline
                pushLine(x0, y0, z0, x1, y0, z0);
                pushLine(x1, y0, z0, x1, y0, z1);
                pushLine(x1, y0, z1, x0, y0, z1);
                pushLine(x0, y0, z1, x0, y0, z0);

                pushLine(x0, y1, z0, x1, y1, z0);
                pushLine(x1, y1, z0, x1, y1, z1);
                pushLine(x1, y1, z1, x0, y1, z1);
                pushLine(x0, y1, z1, x0, y1, z0);

                pushLine(x0, y0, z0, x0, y1, z0);
                pushLine(x1, y0, z0, x1, y1, z0);
                pushLine(x1, y0, z1, x1, y1, z1);
                pushLine(x0, y0, z1, x0, y1, z1);

                glUseProgram(editorSelProg);
                const GLint vpLoc = glGetUniformLocation(editorSelProg, "u_ViewProjection");
                if (vpLoc >= 0) glUniformMatrix4fv(vpLoc, 1, GL_FALSE, &(projection * camera.GetViewMatrix())[0][0]);

                glBindVertexArray(editorSelVao);
                glBindBuffer(GL_ARRAY_BUFFER, editorSelVbo);

                if (fillVerts.size() > editorSelVboCapacityFloats) {
                    editorSelVboCapacityFloats = fillVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, editorSelVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(fillVerts.size() * sizeof(float)), fillVerts.data());
                glUniform4f(glGetUniformLocation(editorSelProg, "u_Color"), 0.25f, 0.85f, 0.45f, 0.25f);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(fillVerts.size() / 3));

                if (lineVerts.size() > editorSelVboCapacityFloats) {
                    editorSelVboCapacityFloats = lineVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, editorSelVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(lineVerts.size() * sizeof(float)), lineVerts.data());
                glUniform4f(glGetUniformLocation(editorSelProg, "u_Color"), 0.35f, 0.95f, 0.55f, 0.85f);
                glDrawArrays(GL_LINES, 0, (GLsizei)(lineVerts.size() / 3));

                glBindVertexArray(0);
                glUseProgram(0);

                if (hadCull) glEnable(GL_CULL_FACE);
                if (!hadBlend) glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                if (!hadDepth) glDisable(GL_DEPTH_TEST);
            }

            if (visualToolActive && visualTool == VisualTool::Paste && clipboardValid && pasteAnchorValid &&
                editorSelProg != 0 && ghostSelVao != 0 && ghostSelVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
                if (hadCull) glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                if (!hadDepth) glEnable(GL_DEPTH_TEST);

                std::vector<float> ghostVerts;
                ghostVerts.reserve((size_t)clipboardPreviewMax * 24 * 3);

                auto pushLine = [&](float ax, float ay, float az, float bx, float by, float bz) {
                    ghostVerts.insert(ghostVerts.end(), {ax, ay, az, bx, by, bz});
                };

                auto pushCube = [&](const glm::ivec3& p) {
                    const float x0 = (float)p.x;
                    const float y0 = (float)p.y;
                    const float z0 = (float)p.z;
                    const float x1 = x0 + 1.0f;
                    const float y1 = y0 + 1.0f;
                    const float z1 = z0 + 1.0f;

                    pushLine(x0, y0, z0, x1, y0, z0);
                    pushLine(x1, y0, z0, x1, y0, z1);
                    pushLine(x1, y0, z1, x0, y0, z1);
                    pushLine(x0, y0, z1, x0, y0, z0);

                    pushLine(x0, y1, z0, x1, y1, z0);
                    pushLine(x1, y1, z0, x1, y1, z1);
                    pushLine(x1, y1, z1, x0, y1, z1);
                    pushLine(x0, y1, z1, x0, y1, z0);

                    pushLine(x0, y0, z0, x0, y1, z0);
                    pushLine(x1, y0, z0, x1, y1, z0);
                    pushLine(x1, y0, z1, x1, y1, z1);
                    pushLine(x0, y0, z1, x0, y1, z1);
                };

                int count = 0;
                for (const auto& voxel : clipboard) {
                    if (count >= clipboardPreviewMax) break;
                    const glm::ivec3 p = pasteAnchor + voxel.offset;
                    pushCube(p);
                    count++;
                }

                glUseProgram(editorSelProg);
                const GLint vpLoc = glGetUniformLocation(editorSelProg, "u_ViewProjection");
                if (vpLoc >= 0) glUniformMatrix4fv(vpLoc, 1, GL_FALSE, &(projection * camera.GetViewMatrix())[0][0]);

                glBindVertexArray(ghostSelVao);
                glBindBuffer(GL_ARRAY_BUFFER, ghostSelVbo);
                if (ghostVerts.size() > ghostSelVboCapacityFloats) {
                    ghostSelVboCapacityFloats = ghostVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, ghostSelVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(ghostVerts.size() * sizeof(float)), ghostVerts.data());
                glUniform4f(glGetUniformLocation(editorSelProg, "u_Color"), 0.25f, 0.85f, 1.00f, 0.35f);
                glDrawArrays(GL_LINES, 0, (GLsizei)(ghostVerts.size() / 3));

                glBindVertexArray(0);
                glUseProgram(0);

                if (hadCull) glEnable(GL_CULL_FACE);
                if (!hadBlend) glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                if (!hadDepth) glDisable(GL_DEPTH_TEST);
            }

            if (!futuristicUi && !menuVisible && !inventoryOpen && perfHudEnabled && invProg != 0 && invVao != 0 && invVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();

                std::vector<float> hudVerts;
                std::vector<float> hudTextVerts;
                hudVerts.reserve(1024 * 9);
                hudTextVerts.reserve(2048 * 9);

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
                    hudVerts.insert(hudVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    hudVerts.insert(hudVerts.end(), {x1, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    hudVerts.insert(hudVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    hudVerts.insert(hudVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    hudVerts.insert(hudVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    hudVerts.insert(hudVerts.end(), {x0, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                };

                auto pushTextPx = [&](float xPx, float yPx, const std::string& text, float scale,
                                      float r, float g, float b, float a) {
                    if (text.empty()) return;
                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text.c_str(), nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex { float x, y, z; unsigned char c[4]; };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;
                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = xPx + quad[idx].x * scale;
                            const float py = yPx + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            hudTextVerts.insert(hudTextVerts.end(), {nx, ny, r, g, b, a, 0.0f, 0.0f, 0.0f});
                        };
                        emit(0);
                        emit(1);
                        emit(2);
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                const float hudPulse = futuristicUi ? (0.5f + 0.5f * std::sin((float)now * 2.25f)) : 0.0f;
                const float panelW = futuristicUi ? 0.62f : 0.58f;
                const float panelH = futuristicUi ? 0.44f : 0.40f;
                const float panelX0 = -0.98f;
                const float panelY1 = 0.98f;
                const float panelX1 = panelX0 + panelW;
                const float panelY0 = panelY1 - panelH;
                if (futuristicUi) {
                    pushQuad(panelX0 - 0.006f, panelY0 - 0.006f, panelX1 + 0.006f, panelY1 + 0.006f,
                             0.12f, 0.55f + 0.15f * hudPulse, 1.00f, 0.18f + 0.08f * hudPulse);
                    pushQuad(panelX0, panelY0, panelX1, panelY1, 0.03f, 0.05f, 0.10f, 0.88f);
                    pushQuad(panelX0 + 0.004f, panelY0 + 0.004f, panelX1 - 0.004f, panelY1 - 0.004f, 0.05f, 0.10f, 0.17f, 0.94f);
                    pushQuad(panelX0 + 0.004f, panelY1 - 0.045f, panelX1 - 0.004f, panelY1 - 0.004f,
                             0.10f, 0.28f, 0.45f + 0.15f * hudPulse, 0.86f);
                    for (int i = 0; i < 6; ++i) {
                        const float gy = panelY0 + 0.02f + i * 0.06f;
                        pushQuad(panelX0 + 0.008f, gy, panelX1 - 0.008f, gy + 0.002f,
                                 0.22f, 0.70f, 1.00f, 0.07f);
                    }
                } else {
                    pushQuad(panelX0, panelY0, panelX1, panelY1, 0.05f, 0.07f, 0.12f, 0.85f);
                    pushQuad(panelX0 + 0.004f, panelY0 + 0.004f, panelX1 - 0.004f, panelY1 - 0.004f, 0.08f, 0.12f, 0.20f, 0.92f);
                }

                const Engine::EliteTelemetrySnapshot elite = Telemetry::GetEliteMetrics();

                const float fpsHud = fpsCurrent;
                const float frameMs = deltaTime * 1000.0f;
                const float speed = glm::length(camera.Velocity);

                const std::size_t inflightGen = chunkManager.GetInFlightGenerate();
                const std::size_t inflightRemesh = chunkManager.GetInFlightRemesh();
                const std::size_t completed = chunkManager.GetCompletedCount();
                const std::size_t deferred = chunkManager.GetDeferredRemeshCount();

                float xPx = 14.0f;
                float yPx = 16.0f;
                const float line = 16.0f;

                pushTextPx(xPx, yPx, futuristicUi ? "AETHERFORGE // PERFORMANCE" : "PERF HUD", 1.1f, 0.90f, 0.97f, 1.00f, 0.95f);
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed); ss.precision(1);
                    ss << "Frame " << frameMs << " ms | FPS " << fpsHud;
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.85f, 0.92f, 1.00f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss << "Chunks " << worldRenderer.GetChunkCount()
                       << " | Tris " << worldRenderer.GetLastTriangleCount()
                       << " | Draws " << worldRenderer.GetLastDrawCount();
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.80f, 0.90f, 1.00f, 0.88f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss << "UploadBudget " << activeUploadBudget
                       << " | Gen " << inflightGen
                       << " | Remesh " << inflightRemesh
                       << " | Done " << completed
                       << " | Deferred " << deferred;
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.78f, 0.88f, 0.98f, 0.85f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss << "HOLE_GUARD staleGen " << chunkManager.GetStaleGenerateDropCount()
                       << " | staleRemesh " << chunkManager.GetStaleRemeshDropCount()
                       << " | surfMiss " << coverageMisses
                       << " | covBoost " << coverageBoostApplied
                       << " | alarm " << (coverageAlarmActive ? "ON" : "off")
                       << " | fixedSat " << fixedStepSaturatedFramesSec;
                    pushTextPx(xPx, yPx, ss.str(), 0.95f, 0.86f, 0.75f, 0.98f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss << "LATENCY score " << stutterLatencyScore
                       << " | pending " << streamPendingNotReady
                       << " oldTick " << streamPendingOldestTicks
                       << " | overdue " << streamWatchdogOverdue
                       << " / " << streamWatchdogEligible
                                        << " | impQ " << std::fixed << std::setprecision(2) << impostorQueuePressureLive
                                        << " avg " << std::fixed << std::setprecision(2) << impostorQueuePressureAvgLive
                                        << " mul " << std::fixed << std::setprecision(2) << impostorQueuePushMulLive
                       << " | cause " << stutterAlertCause;
                    pushTextPx(xPx, yPx, ss.str(), 0.95f,
                               stutterAlertActive ? 1.00f : 0.76f,
                               stutterAlertActive ? 0.58f : 0.88f,
                               stutterAlertActive ? 0.58f : 0.98f,
                               0.92f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed); ss.precision(1);
                    ss << "CONTROL q " << controlQualityScoreSec
                       << "% | lag " << controlLagMsSec << "ms"
                       << " | rot " << controlRotDegPerSecSec << "dps"
                              << " | moveDelay " << controlMoveDelayMsSec << "ms"
                              << " | m(W/A/S/D) " << controlDistForwardMetersSec << "/" << controlDistLeftMetersSec << "/" << controlDistBackMetersSec << "/" << controlDistRightMetersSec
                       << " | wasdHold " << controlWASDHoldSec << "s"
                       << " | lmb/rmb " << controlLmbHoldSec << "/" << controlRmbHoldSec << "s"
                       << " | place/break " << controlPlaceOpsSec << "/" << controlBreakOpsSec;
                    const bool controlWarn = controlLagMsSec >= (int)std::lround(controlLagWarnMs)
                        || controlQualityScoreSec < (int)std::lround(controlResponseThreshold * 100.0f);
                    pushTextPx(xPx, yPx, ss.str(), 0.95f,
                               controlWarn ? 1.00f : 0.74f,
                               controlWarn ? 0.62f : 0.92f,
                               controlWarn ? 0.45f : 0.96f,
                               0.93f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed); ss.precision(1);
                    ss << "Speed " << speed << " | View " << viewDistanceChunks << " | Margin " << streamMarginChunks;
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.75f, 0.85f, 0.96f, 0.85f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss << "Stream " << hudStreamViewDistance
                       << " | Height " << hudHeightChunks
                       << " | Unload " << unloadDistanceChunks
                       << " | ImpNear " << (int)std::lround(impostorBlendNearStartLive)
                       << "-" << (int)std::lround(impostorBlendNearEndLive);
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.74f, 0.92f, 0.95f, 0.88f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(1);
                    ss << "LowEnd L" << lowEndLevel
                       << " | fast " << lowEndEmaFastMs << "ms"
                       << " | down/s " << lowEndDownshiftEventsSec
                       << " up/s " << lowEndUpshiftEventsSec
                       << " | reason " << lowEndLastReason;
                    pushTextPx(xPx, yPx, ss.str(), 0.95f, 0.84f, 0.95f, 0.74f, 0.92f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(2);
                    ss << "Mode " << (walkMode ? "WALK" : "FLY")
                       << " | Ground " << (walkGrounded ? "yes" : "no")
                       << " | VY " << walkVerticalVel;
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.88f, 0.86f, 0.96f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(1);
                    ss << "S " << elite.smoothnessIndex << (elite.smoothnessPass ? " [OK]" : " [LOW]")
                       << " | R " << elite.realismScore << (elite.realismPass ? " [OK]" : " [LOW]");
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.72f, 0.96f, 0.84f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(3);
                    ss << "M " << elite.memoryEfficiency << (elite.memoryPass ? " [OK]" : " [HIGH]")
                       << " | CE " << elite.cacheEfficiency << (elite.cachePass ? " [OK]" : " [LOW]");
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.95f, 0.88f, 0.70f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(1);
                    ss << "TSI " << elite.toasterStabilityIndex << (elite.toasterStabilityPass ? " [OK]" : " [LOW]")
                       << " | TRS " << elite.toasterRealismScore << (elite.toasterRealismPass ? " [OK]" : " [LOW]");
                    pushTextPx(xPx, yPx, ss.str(), 1.0f, 0.92f, 0.86f, 0.98f, 0.92f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(2);
                    const float usedMB = elite.memoryGuarantee / (1024.0f * 1024.0f);
                    const float limitMB = elite.memoryGuaranteeLimit / (1024.0f * 1024.0f);
                    ss << "MemGuarantee " << usedMB << "MB / " << limitMB << "MB"
                       << (elite.memoryGuaranteePass ? " [OK]" : " [HIGH]")
                       << " | a " << (float)alpha;
                    pushTextPx(xPx, yPx, ss.str(), 0.95f, 0.84f, 0.95f, 0.84f, 0.90f);
                }
                yPx += line;
                {
                    std::ostringstream ss;
                    ss.setf(std::ios::fixed);
                    ss.precision(3);
                    ss << "H " << terrainMetrics.heightEntropy
                       << " | B " << terrainMetrics.biomeVariance
                       << " | E " << terrainMetrics.erosionDetail
                       << " | A " << terrainMetrics.artifactDensity;
                    pushTextPx(xPx, yPx, ss.str(), 0.95f, 0.80f, 0.90f, 0.98f, 0.88f);
                }

                hudVerts.insert(hudVerts.end(), hudTextVerts.begin(), hudTextVerts.end());

                glUseProgram(invProg);
                applyUiProgramUniforms(invProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                if (hudVerts.size() > invVboCapacityFloats) {
                    invVboCapacityFloats = hudVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(hudVerts.size() * sizeof(float)), hudVerts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(hudVerts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            if (!menuVisible && !inventoryOpen && builderPanelEnabled && invProg != 0 && invVao != 0 && invVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();
                const glm::vec2 mp = Input::GetMousePosition();
                const float mx = (w > 0.0f) ? (mp.x / w) * 2.0f - 1.0f : 0.0f;
                const float my = (h > 0.0f) ? (1.0f - (mp.y / h) * 2.0f) : 0.0f;
                const bool uiClick = (!mouseCaptured && lmbClicked);

                std::vector<float> overlayVerts;
                std::vector<float> overlayText;
                overlayVerts.reserve(2048 * 9);
                overlayText.reserve(2048 * 9);

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
                    overlayVerts.insert(overlayVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x0, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                };

                auto pushTextPx = [&](float xPx, float yPx, const std::string& text, float scale,
                                      float r, float g, float b, float a) {
                    if (text.empty()) return;
                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text.c_str(), nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex { float x, y, z; unsigned char c[4]; };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;
                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = xPx + quad[idx].x * scale;
                            const float py = yPx + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            overlayText.insert(overlayText.end(), {nx, ny, r, g, b, a, 0.0f, 0.0f, 0.0f});
                        };
                        emit(0);
                        emit(1);
                        emit(2);
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                auto ndcToPxX = [&](float x) { return (x * 0.5f + 0.5f) * w; };
                auto ndcToPxY = [&](float y) { return (1.0f - (y * 0.5f + 0.5f)) * h; };

                const float panelX0 = 0.58f;
                const float panelX1 = 0.98f;
                const float panelY0 = -0.92f;
                const float panelY1 = 0.92f;
                pushQuad(panelX0, panelY0, panelX1, panelY1, 0.04f, 0.07f, 0.12f, 0.92f);
                pushQuad(panelX0 + 0.004f, panelY0 + 0.004f, panelX1 - 0.004f, panelY1 - 0.004f, 0.07f, 0.10f, 0.16f, 0.96f);

                pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(panelY1 - 0.06f), "BUILDER", 1.2f, 0.92f, 0.98f, 1.00f, 0.98f);

                const char* toolName = "Selection";
                if (visualTool == VisualTool::Pattern) toolName = "Pattern";
                else if (visualTool == VisualTool::Fill) toolName = "Fill";
                else if (visualTool == VisualTool::Copy) toolName = "Copy";
                else if (visualTool == VisualTool::Cut) toolName = "Cut";
                else if (visualTool == VisualTool::Paste) toolName = "Paste";

                float infoY = panelY1 - 0.12f;
                std::ostringstream info;
                info << "Tool: " << toolName;
                pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), info.str(), 0.95f, 0.78f, 0.88f, 0.98f, 0.90f);
                infoY -= 0.05f;

                if (visualSelValid) {
                    const int dx = std::abs(visualSelMax.x - visualSelMin.x) + 1;
                    const int dy = std::abs(visualSelMax.y - visualSelMin.y) + 1;
                    const int dz = std::abs(visualSelMax.z - visualSelMin.z) + 1;
                    std::ostringstream sel;
                    sel << "Selection: " << dx << "x" << dy << "x" << dz;
                    pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), sel.str(), 0.9f, 0.75f, 0.85f, 0.96f, 0.88f);
                } else {
                    pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), "Selection: none", 0.9f, 0.70f, 0.80f, 0.92f, 0.82f);
                }
                infoY -= 0.05f;

                {
                    std::ostringstream pat;
                    pat << "Pattern: " << int(patternBlockId);
                    pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), pat.str(), 0.9f, 0.75f, 0.85f, 0.96f, 0.88f);
                }
                infoY -= 0.05f;

                {
                    std::ostringstream clip;
                    clip << "Clipboard: " << (clipboardValid ? std::to_string(clipboard.size()) + " blocks" : "empty");
                    pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), clip.str(), 0.9f, 0.75f, 0.85f, 0.96f, 0.88f);
                }
                infoY -= 0.05f;

                pushTextPx(ndcToPxX(panelX0 + 0.02f), ndcToPxY(infoY), mouseCaptured ? "Mouse: captured" : "Mouse: free", 0.85f, 0.70f, 0.82f, 0.95f, 0.80f);

                const float toolX = panelX0 + 0.02f;
                const float toolW = (panelX1 - panelX0) - 0.04f;

                float inspectorY = infoY - 0.06f;
                pushTextPx(ndcToPxX(toolX), ndcToPxY(inspectorY), "INSPECTOR", 0.95f, 0.84f, 0.94f, 1.00f, 0.92f);
                inspectorY -= 0.04f;

                if (visualSelValid) {
                    {
                        std::ostringstream ss;
                        ss << "Min: " << visualSelMin.x << ", " << visualSelMin.y << ", " << visualSelMin.z;
                        pushTextPx(ndcToPxX(toolX), ndcToPxY(inspectorY), ss.str(), 0.86f, 0.76f, 0.86f, 0.98f, 0.88f);
                    }
                    inspectorY -= 0.038f;
                    {
                        std::ostringstream ss;
                        ss << "Max: " << visualSelMax.x << ", " << visualSelMax.y << ", " << visualSelMax.z;
                        pushTextPx(ndcToPxX(toolX), ndcToPxY(inspectorY), ss.str(), 0.86f, 0.76f, 0.86f, 0.98f, 0.88f);
                    }
                    inspectorY -= 0.038f;
                    {
                        const int sx = std::abs(visualSelMax.x - visualSelMin.x) + 1;
                        const int sy = std::abs(visualSelMax.y - visualSelMin.y) + 1;
                        const int sz = std::abs(visualSelMax.z - visualSelMin.z) + 1;
                        std::ostringstream ss;
                        ss << "Size: " << sx << " x " << sy << " x " << sz;
                        pushTextPx(ndcToPxX(toolX), ndcToPxY(inspectorY), ss.str(), 0.86f, 0.76f, 0.86f, 0.98f, 0.88f);
                    }
                    inspectorY -= 0.048f;

                    const float nudgeH = 0.055f;
                    const float nudgeGap = 0.012f;
                    const float nudgeW = (toolW - nudgeGap) * 0.5f;
                    const float nudgeX0 = toolX;
                    const float nudgeX1 = nudgeX0 + nudgeW;
                    const float nudgeX2 = nudgeX1 + nudgeGap;
                    const float nudgeX3 = nudgeX2 + nudgeW;

                    auto pushNudgeButton = [&](float x0, float x1, float yTop, const char* label,
                                               const glm::ivec3& delta) {
                        const float y1 = yTop;
                        const float y0 = yTop - nudgeH;
                        const bool hover = (!mouseCaptured && mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                        if (hover && uiClick) {
                            nudgeVisualSelection(delta);
                        }
                        const float r = hover ? 0.15f : 0.11f;
                        const float g = hover ? 0.20f : 0.15f;
                        const float b = hover ? 0.27f : 0.20f;
                        pushQuad(x0, y0, x1, y1, r, g, b, 0.93f);
                        pushTextPx(ndcToPxX(x0 + 0.014f), ndcToPxY(y1 - 0.040f), label, 0.82f, 0.90f, 0.95f, 1.00f, 0.92f);
                    };

                    pushNudgeButton(nudgeX0, nudgeX1, inspectorY, "-X", glm::ivec3(-1, 0, 0));
                    pushNudgeButton(nudgeX2, nudgeX3, inspectorY, "+X", glm::ivec3(1, 0, 0));
                    inspectorY -= (nudgeH + 0.010f);
                    pushNudgeButton(nudgeX0, nudgeX1, inspectorY, "-Y", glm::ivec3(0, -1, 0));
                    pushNudgeButton(nudgeX2, nudgeX3, inspectorY, "+Y", glm::ivec3(0, 1, 0));
                    inspectorY -= (nudgeH + 0.010f);
                    pushNudgeButton(nudgeX0, nudgeX1, inspectorY, "-Z", glm::ivec3(0, 0, -1));
                    pushNudgeButton(nudgeX2, nudgeX3, inspectorY, "+Z", glm::ivec3(0, 0, 1));
                    inspectorY -= (nudgeH + 0.020f);
                } else {
                    pushTextPx(ndcToPxX(toolX), ndcToPxY(inspectorY), "No active selection", 0.86f, 0.70f, 0.80f, 0.92f, 0.82f);
                    inspectorY -= 0.08f;
                }

                float toolY = inspectorY;
                const float toolH = 0.085f;
                const float toolGap = 0.018f;

                pushTextPx(ndcToPxX(toolX), ndcToPxY(toolY), "TOOLS", 0.95f, 0.84f, 0.94f, 1.00f, 0.92f);
                toolY -= 0.04f;

                auto pushToolButton = [&](const char* label, bool active, const auto& onClick) {
                    const float x0 = toolX;
                    const float x1 = toolX + toolW;
                    const float y1 = toolY;
                    const float y0 = toolY - toolH;
                    const bool hover = (!mouseCaptured && mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                    if (hover && uiClick) {
                        onClick();
                    }
                    const float r = active ? 0.18f : (hover ? 0.14f : 0.10f);
                    const float g = active ? 0.26f : (hover ? 0.18f : 0.14f);
                    const float b = active ? 0.36f : (hover ? 0.22f : 0.18f);
                    pushQuad(x0, y0, x1, y1, r, g, b, 0.96f);
                    pushQuad(x0 + 0.004f, y0 + 0.004f, x1 - 0.004f, y1 - 0.004f, r + 0.05f, g + 0.05f, b + 0.06f, 0.90f);
                    pushTextPx(ndcToPxX(x0 + 0.02f), ndcToPxY(y1 - 0.055f), label, 1.05f, 0.94f, 0.98f, 1.00f, 0.96f);
                    toolY = y0 - toolGap;
                };

                pushToolButton("Selection", visualTool == VisualTool::Selection, [&]() { visualTool = VisualTool::Selection; });
                pushToolButton("Pattern", visualTool == VisualTool::Pattern, [&]() { visualTool = VisualTool::Pattern; });
                pushToolButton("Fill", visualTool == VisualTool::Fill, [&]() { visualTool = VisualTool::Fill; });
                pushToolButton("Copy", visualTool == VisualTool::Copy, [&]() { visualTool = VisualTool::Copy; });
                pushToolButton("Cut", visualTool == VisualTool::Cut, [&]() { visualTool = VisualTool::Cut; });
                pushToolButton("Paste", visualTool == VisualTool::Paste, [&]() { visualTool = VisualTool::Paste; });

                toolY -= 0.02f;
                pushTextPx(ndcToPxX(toolX), ndcToPxY(toolY), "ACTIONS", 0.95f, 0.84f, 0.94f, 1.00f, 0.92f);
                toolY -= 0.04f;

                auto pushActionButton = [&](const char* label, bool enabled, const auto& onClick) {
                    const float x0 = toolX;
                    const float x1 = toolX + toolW;
                    const float y1 = toolY;
                    const float y0 = toolY - 0.075f;
                    const bool hover = enabled && !mouseCaptured && mx >= x0 && mx <= x1 && my >= y0 && my <= y1;
                    if (hover && uiClick) {
                        onClick();
                    }
                    const float r = enabled ? (hover ? 0.16f : 0.12f) : 0.07f;
                    const float g = enabled ? (hover ? 0.20f : 0.16f) : 0.07f;
                    const float b = enabled ? (hover ? 0.26f : 0.20f) : 0.08f;
                    const float a = enabled ? 0.92f : 0.65f;
                    pushQuad(x0, y0, x1, y1, r, g, b, a);
                    pushTextPx(ndcToPxX(x0 + 0.02f), ndcToPxY(y1 - 0.05f), label, 0.98f, 0.88f, 0.95f, 1.00f, enabled ? 0.92f : 0.45f);
                    toolY = y0 - 0.014f;
                };

                const bool selReady = (selectionState == SelectionState::Locked && visualSelValid);
                pushActionButton("Apply Fill", selReady, [&]() { doVisualFill(); });
                pushActionButton("Copy Selection", selReady, [&]() { doVisualCopy(); });
                pushActionButton("Cut Selection", selReady, [&]() { doVisualCut(); });
                pushActionButton("Clear Selection", selectionState != SelectionState::Idle, [&]() {
                    selectionState = SelectionState::Idle;
                    visualSelValid = false;
                });
                pushActionButton("Clear Clipboard", clipboardValid, [&]() {
                    clipboard.clear();
                    clipboardValid = false;
                });
                pushActionButton("Undo", !undoStack.empty(), [&]() { performUndo(); });
                pushActionButton("Redo", !redoStack.empty(), [&]() { performRedo(); });

                overlayVerts.insert(overlayVerts.end(), overlayText.begin(), overlayText.end());

                glUseProgram(invProg);
                applyUiProgramUniforms(invProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                if (overlayVerts.size() > invVboCapacityFloats) {
                    invVboCapacityFloats = overlayVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(overlayVerts.size() * sizeof(float)), overlayVerts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(overlayVerts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            if (!menuVisible && !inventoryOpen && editorEnabled && invProg != 0 && invVao != 0 && invVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();
                const glm::vec2 mp = Input::GetMousePosition();
                const float mx = (w > 0.0f) ? (mp.x / w) * 2.0f - 1.0f : 0.0f;
                const float my = (h > 0.0f) ? (1.0f - (mp.y / h) * 2.0f) : 0.0f;

                std::vector<float> overlayVerts;
                std::vector<float> overlayText;
                overlayVerts.reserve(2048 * 9);
                overlayText.reserve(2048 * 9);

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
                    overlayVerts.insert(overlayVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    overlayVerts.insert(overlayVerts.end(), {x0, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                };

                auto pushTextPx = [&](float xPx, float yPx, const std::string& text, float scale,
                                      float r, float g, float b, float a) {
                    if (text.empty()) return;
                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text.c_str(), nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex { float x, y, z; unsigned char c[4]; };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;
                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = xPx + quad[idx].x * scale;
                            const float py = yPx + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            overlayText.insert(overlayText.end(), {nx, ny, r, g, b, a, 0.0f, 0.0f, 0.0f});
                        };
                        emit(0);
                        emit(1);
                        emit(2);
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                auto ndcToPxX = [&](float x) { return (x * 0.5f + 0.5f) * w; };
                auto ndcToPxY = [&](float y) { return (1.0f - (y * 0.5f + 0.5f)) * h; };

                const char* modeName = "Fill";
                if (editorMode == EditorMode::Replace) modeName = "Replace";
                else if (editorMode == EditorMode::Hollow) modeName = "Hollow";
                else if (editorMode == EditorMode::Remove) modeName = "Remove";

                const EditorDragMode displayDragMode = editorDragActive ? editorDragModeUsed : editorDragMode;
                const char* shapeName = "Box";
                if (displayDragMode == EditorDragMode::Face) shapeName = "Face";
                else if (displayDragMode == EditorDragMode::Column) shapeName = "Down";

                const float topBarY0 = 0.92f;
                const float topBarY1 = 1.0f;
                pushQuad(-1.0f, topBarY0, 1.0f, topBarY1, 0.07f, 0.10f, 0.16f, 0.92f);

                const float leftPanelX0 = -0.98f;
                const float leftPanelX1 = -0.60f;
                const float leftPanelY0 = -0.98f;
                const float leftPanelY1 = 0.90f;
                pushQuad(leftPanelX0, leftPanelY0, leftPanelX1, leftPanelY1, 0.05f, 0.08f, 0.12f, 0.92f);
                pushQuad(leftPanelX0 + 0.004f, leftPanelY0 + 0.004f, leftPanelX1 - 0.004f, leftPanelY1 - 0.004f, 0.08f, 0.12f, 0.18f, 0.96f);

                float topTextX = 12.0f;
                float topTextY = 8.0f;
                pushTextPx(topTextX, topTextY, "EDITOR", 1.25f, 0.92f, 0.98f, 1.00f, 0.98f);

                {
                    std::ostringstream ss;
                    ss << "Mode: " << modeName << "  Shape: " << shapeName
                       << "  Block: " << int(selectedBlockId);
                    if (editorMode == EditorMode::Replace) ss << "  Target: " << int(editorTargetId);
                    if (editorSelValid) {
                        const int dx = std::abs(editorSelMax.x - editorSelMin.x) + 1;
                        const int dy = std::abs(editorSelMax.y - editorSelMin.y) + 1;
                        const int dz = std::abs(editorSelMax.z - editorSelMin.z) + 1;
                        ss << "  Sel: " << dx << "x" << dy << "x" << dz;
                    }
                    pushTextPx(160.0f, topTextY, ss.str(), 1.0f, 0.80f, 0.90f, 1.00f, 0.92f);
                }

                if (editorBatch.active && editorBatch.total > 0) {
                    std::ostringstream ss;
                    ss << "Applying " << editorBatch.visited << "/" << editorBatch.total;
                    pushTextPx(w - 240.0f, topTextY, ss.str(), 0.95f, 0.78f, 0.95f, 0.92f, 0.92f);
                }

                const float toolX = leftPanelX0 + 0.02f;
                float toolY = leftPanelY1 - 0.08f;
                const float toolW = (leftPanelX1 - leftPanelX0) - 0.04f;
                const float toolH = 0.10f;
                const float toolGap = 0.02f;

                pushTextPx(ndcToPxX(toolX), ndcToPxY(leftPanelY1 - 0.04f), "TOOLS", 1.2f, 0.92f, 0.98f, 1.00f, 0.95f);

                auto pushToolButton = [&](const char* label, bool active, const auto& onClick) {
                    const float x0 = toolX;
                    const float x1 = toolX + toolW;
                    const float y1 = toolY;
                    const float y0 = toolY - toolH;
                    const bool hover = (!mouseCaptured && mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                    if (hover && lmbClicked) {
                        onClick();
                    }
                    const float r = active ? 0.20f : (hover ? 0.16f : 0.12f);
                    const float g = active ? 0.30f : (hover ? 0.20f : 0.16f);
                    const float b = active ? 0.38f : (hover ? 0.26f : 0.20f);
                    pushQuad(x0, y0, x1, y1, r, g, b, 0.98f);
                    pushQuad(x0 + 0.004f, y0 + 0.004f, x1 - 0.004f, y1 - 0.004f, r + 0.06f, g + 0.06f, b + 0.06f, 0.92f);
                    const float tx = ndcToPxX(x0 + 0.02f);
                    const float ty = ndcToPxY(y1 - 0.06f);
                    pushTextPx(tx, ty, label, 1.2f, 0.96f, 0.99f, 1.00f, 0.98f);
                    toolY = y0 - toolGap;
                };

                pushToolButton("Replace", editorMode == EditorMode::Replace, [&]() { editorMode = EditorMode::Replace; });
                pushToolButton("Fill", editorMode == EditorMode::Fill, [&]() { editorMode = EditorMode::Fill; });
                pushToolButton("Hollow", editorMode == EditorMode::Hollow, [&]() { editorMode = EditorMode::Hollow; });
                pushToolButton("Remove", editorMode == EditorMode::Remove, [&]() { editorMode = EditorMode::Remove; });

                toolY -= 0.03f;
                pushTextPx(ndcToPxX(toolX), ndcToPxY(toolY), "SHAPES", 1.0f, 0.82f, 0.92f, 0.98f, 0.9f);
                toolY -= 0.04f;
                pushToolButton("Box", displayDragMode == EditorDragMode::Box, [&]() { editorDragMode = EditorDragMode::Box; });
                pushToolButton("Face", displayDragMode == EditorDragMode::Face, [&]() { editorDragMode = EditorDragMode::Face; });
                pushToolButton("Down", displayDragMode == EditorDragMode::Column, [&]() { editorDragMode = EditorDragMode::Column; });

                const float hintY = leftPanelY0 + 0.12f;
                pushTextPx(ndcToPxX(leftPanelX0 + 0.02f), ndcToPxY(hintY + 0.08f), "LMB drag: select + apply", 0.9f, 0.78f, 0.88f, 0.98f, 0.85f);
                pushTextPx(ndcToPxX(leftPanelX0 + 0.02f), ndcToPxY(hintY + 0.04f), "B: mode | X: shape", 0.9f, 0.78f, 0.88f, 0.98f, 0.85f);
                pushTextPx(ndcToPxX(leftPanelX0 + 0.02f), ndcToPxY(hintY), "T: target | Backspace: clear", 0.9f, 0.78f, 0.88f, 0.98f, 0.85f);
                pushTextPx(ndcToPxX(leftPanelX0 + 0.02f), ndcToPxY(hintY - 0.04f), "ESC: free mouse", 0.9f, 0.78f, 0.88f, 0.98f, 0.85f);

                overlayVerts.insert(overlayVerts.end(), overlayText.begin(), overlayText.end());

                glUseProgram(invProg);
                applyUiProgramUniforms(invProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                if (overlayVerts.size() > invVboCapacityFloats) {
                    invVboCapacityFloats = overlayVerts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(overlayVerts.size() * sizeof(float)), overlayVerts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(overlayVerts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            // Start menu / pause menu overlay
            if (menuVisible && invProg != 0 && invVao != 0 && invVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                if (hadCull) glDisable(GL_CULL_FACE);

                // Refresh world list when entering Load screen
                if (menuScreen != lastMenuScreen) {
                    if (menuScreen == MenuScreen::Load) {
                        scanWorlds();
                    }
                    menuStatusText.clear();
                    lastMenuScreen = menuScreen;
                }

                const glm::vec2 mp = Input::GetMousePosition();
                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();
                const float uiScale = std::max(1.0f, std::min(w, h) / 720.0f);
                const float mx = (w > 0.0f) ? (mp.x / w) * 2.0f - 1.0f : 0.0f;
                const float my = (h > 0.0f) ? (1.0f - (mp.y / h) * 2.0f) : 0.0f;

                std::vector<float> verts;
                verts.reserve(4096 * 9);

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
                    // Triangle 1
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    // Triangle 2
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x0, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                };

                auto pushInvFrame = [&](float x0, float y0, float x1, float y1, float thickness,
                                         float r, float g, float b, float a) {
                    pushQuad(x0, y0, x1, y0 + thickness, r, g, b, a);
                    pushQuad(x0, y1 - thickness, x1, y1, r, g, b, a);
                    pushQuad(x0, y0, x0 + thickness, y1, r, g, b, a);
                    pushQuad(x1 - thickness, y0, x1, y1, r, g, b, a);
                };

                auto pushFrame = [&](float x0, float y0, float x1, float y1, float thickness,
                                      float r, float g, float b, float a) {
                    pushQuad(x0, y0, x1, y0 + thickness, r, g, b, a);
                    pushQuad(x0, y1 - thickness, x1, y1, r, g, b, a);
                    pushQuad(x0, y0, x0 + thickness, y1, r, g, b, a);
                    pushQuad(x1 - thickness, y0, x1, y1, r, g, b, a);
                };

                auto pushTextPxRaw = [&](float xPx, float yPx, const std::string& text, float scale,
                                         float r, float g, float b, float a) {
                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text.c_str(), nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex {
                        float x, y, z;
                        unsigned char c[4];
                    };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;
                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = xPx + quad[idx].x * scale;
                            const float py = yPx + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            verts.insert(verts.end(), {nx, ny, r, g, b, a, 0.0f, 0.0f, 0.0f});
                        };
                        // 0,1,2
                        emit(0);
                        emit(1);
                        emit(2);
                        // 0,2,3
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                auto pushTextPx = [&](float xPx, float yPx, const std::string& text, float scale,
                                      float r, float g, float b, float a) {
                    const float s = std::max(1.0f, scale);
                    const float o = 2.0f * uiScale;
                    // Soft shadow for readability.
                    pushTextPxRaw(xPx + o, yPx + o, text, s, 0.0f, 0.0f, 0.0f, std::min(0.55f, a));
                    pushTextPxRaw(xPx, yPx, text, s, r, g, b, a);
                };

                auto pushTextCenteredNdc = [&](float cxNdc, float cyNdc, const std::string& text, float scale,
                                               float r, float g, float b, float a) {
                    const float s = scale * uiScale;
                    const int tw = stb_easy_font_width((char*)text.c_str());
                    const int th = stb_easy_font_height((char*)text.c_str());
                    const float cxPx = (cxNdc * 0.5f + 0.5f) * w;
                    const float cyPx = (1.0f - (cyNdc * 0.5f + 0.5f)) * h;
                    const float xPx = cxPx - 0.5f * (float)tw * s;
                    const float yPx = cyPx - 0.5f * (float)th * s;
                    pushTextPx(xPx, yPx, text, s, r, g, b, a);
                };

                auto ndcToPxX = [&](float x) { return (x * 0.5f + 0.5f) * w; };
                auto ndcToPxY = [&](float y) { return (1.0f - (y * 0.5f + 0.5f)) * h; };

                auto pushButton = [&](float x0, float y0, float x1, float y1, const std::string& label,
                                      bool enabled, bool forceActive, bool& outHovered) {
                    const bool inside = (mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                    outHovered = inside && enabled;
                    const bool active = (inside || forceActive) && enabled;

                    // Backdrop veil already exists; draw a slight glow + body.
                    const float glowA = active ? 0.22f : 0.10f;
                    pushQuad(x0 - 0.012f, y0 - 0.012f, x1 + 0.012f, y1 + 0.012f, 0.20f, 0.55f, 1.00f, enabled ? glowA : 0.06f);

                    const float br = enabled ? (active ? 0.18f : 0.12f) : 0.07f;
                    const float bg = enabled ? (active ? 0.20f : 0.12f) : 0.07f;
                    const float bb = enabled ? (active ? 0.26f : 0.16f) : 0.08f;
                    pushQuad(x0, y0, x1, y1, br, bg, bb, 0.92f);

                    pushTextCenteredNdc(0.5f * (x0 + x1), 0.5f * (y0 + y1), label, 1.9f, 0.88f, 0.96f, 1.00f, enabled ? 0.98f : 0.35f);
                };

                const bool click = lmbDown && !lastMenuLmb;
                const bool activateKey = menuInput && menuEnter;

                // Fullscreen dark veil
                pushQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.01f, 0.02f, 0.04f, 0.74f);

                // Central panel
                const float px0 = -0.55f;
                const float px1 = 0.55f;
                const float py0 = -0.62f;
                const float py1 = 0.62f;
                pushQuad(px0 - 0.02f, py0 - 0.02f, px1 + 0.02f, py1 + 0.02f, 0.15f, 0.45f, 0.95f, 0.18f);
                pushQuad(px0, py0, px1, py1, 0.04f, 0.06f, 0.10f, 0.92f);

                // Title
                pushTextCenteredNdc(0.0f, 0.50f, "R9 HYPER CORE", 2.2f, 0.70f, 0.90f, 1.00f, 0.95f);

                // Async load status is controlled by showLoadingOverlay.
                if (showLoadingOverlay && menuScreen != MenuScreen::Loading) {
                    menuScreen = MenuScreen::Loading;
                }

                if (menuScreen == MenuScreen::Main) {
                    float y = 0.26f;
                    const float bh = 0.12f;
                    const float bw = 0.70f;
                    const float bx0 = -0.35f;
                    const float bx1 = bx0 + bw;
                    const float step = 0.14f;

                    bool hov = false;
                    const bool canResume = worldStarted;
                    pushButton(bx0, y, bx1, y + bh, canResume ? "SPIELEN" : "SPIELEN", true, menuMainIndex == 0, hov);
                    if (hov) menuMainIndex = 0;
                    if ((click && hov) || (activateKey && menuMainIndex == 0)) {
                        worldStarted = true;
                        menuOpen = false;
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                        preloadDone = (preloadRadiusChunks <= 0);
                        preloadStartTime = now;
                        cameraAutoPlaced = false;
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "CREATE WORLD", true, menuMainIndex == 1, hov);
                    if (hov) menuMainIndex = 1;
                    if ((click && hov) || (activateKey && menuMainIndex == 1)) {
                        ensureWorldsDir();
                        const std::uint64_t ms = (std::uint64_t)(glfwGetTime() * 1000.0);
                        const std::uint32_t seed = (std::uint32_t)(ms ^ (ms >> 32));
                        worldFile = std::string("worlds/world_") + std::to_string(ms) + ".hvew";
                        ini["quality"] = std::to_string(qualityLevel);
                        ini["world_file"] = worldFile;
                        SaveIniKeyValue(settingsPath, ini);

                        chunkManager.ClearAll(worldRenderer);
                        Game::World::Generation::Init((int)seed);
                        resetCamera();
                        cameraAutoPlaced = false;

                        preloadDone = (preloadRadiusChunks <= 0);
                        preloadStartTime = now;

                        worldStarted = true;
                        menuOpen = false;
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                        scanWorlds();
                        menuStatusText = "New world created";
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "LOAD WORLD", true, menuMainIndex == 2, hov);
                    if (hov) menuMainIndex = 2;
                    if ((click && hov) || (activateKey && menuMainIndex == 2)) {
                        menuScreen = MenuScreen::Load;
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "SETTINGS", true, menuMainIndex == 3, hov);
                    if (hov) menuMainIndex = 3;
                    if ((click && hov) || (activateKey && menuMainIndex == 3)) {
                        menuScreen = MenuScreen::Settings;
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "TERRAIN PRESETS", true, menuMainIndex == 4, hov);
                    if (hov) menuMainIndex = 4;
                    if ((click && hov) || (activateKey && menuMainIndex == 4)) {
                        menuScreen = MenuScreen::Terrain;
                        menuTerrainIndex = std::clamp(Game::World::g_CurrentPresetIndex, 0, 7);
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "SAVE WORLD", worldStarted, menuMainIndex == 5, hov);
                    if (hov) menuMainIndex = 5;
                    if (((click && hov) || (activateKey && menuMainIndex == 5)) && worldStarted) {
                        saveCurrentWorld("Menu");
                    }
                    y -= step;

                    pushButton(bx0, y, bx1, y + bh, "QUIT", true, menuMainIndex == 6, hov);
                    if (hov) menuMainIndex = 6;
                    if ((click && hov) || (activateKey && menuMainIndex == 6)) {
                        Window::Close();
                    }

                    // Footer info
                    pushTextCenteredNdc(0.0f, -0.52f, (std::string("WORLD: ") + worldFile), 1.2f, 0.70f, 0.80f, 0.92f, 0.85f);

                    if (!menuStatusText.empty()) {
                        pushTextCenteredNdc(0.0f, -0.64f, menuStatusText, 1.25f, 0.85f, 0.95f, 1.00f, 0.90f);
                    }
                } else if (menuScreen == MenuScreen::Terrain) {
                    pushTextCenteredNdc(0.0f, 0.38f, "TERRAIN PRESETS", 1.8f, 0.70f, 0.92f, 1.00f, 0.96f);

                    auto hexNibble = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                        return 0;
                    };
                    auto parseColor = [&](const std::string& hex, float& r, float& g, float& b) {
                        r = 0.40f;
                        g = 0.70f;
                        b = 1.00f;
                        if (hex.size() != 7 || hex[0] != '#') return;
                        const int rr = hexNibble(hex[1]) * 16 + hexNibble(hex[2]);
                        const int gg = hexNibble(hex[3]) * 16 + hexNibble(hex[4]);
                        const int bb = hexNibble(hex[5]) * 16 + hexNibble(hex[6]);
                        r = (float)rr / 255.0f;
                        g = (float)gg / 255.0f;
                        b = (float)bb / 255.0f;
                    };

                    const float cardW = 0.42f;
                    const float cardH = 0.12f;
                    const float leftX = -0.47f;
                    const float rightX = 0.05f;
                    const float startY = 0.22f;
                    const float rowStep = 0.15f;

                    int hovered = -1;
                    for (int i = 0; i < 8; ++i) {
                        const int row = i / 2;
                        const bool right = (i % 2) == 1;
                        const float x0 = right ? rightX : leftX;
                        const float x1 = x0 + cardW;
                        const float y1 = startY - row * rowStep;
                        const float y0 = y1 - cardH;

                        const bool inside = (mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                        if (inside) hovered = i;
                        const bool selected = (i == selectedTerrain) || (menuTerrainIndex == i);

                        float cr = 0.2f, cg = 0.5f, cb = 0.9f;
                        parseColor(Game::World::g_TerrainPresets[i].previewHex, cr, cg, cb);

                        pushQuad(x0, y0, x1, y1, 0.06f, 0.09f, 0.14f, selected ? 0.98f : 0.90f);
                        pushQuad(x0 + 0.01f, y1 - 0.03f, x1 - 0.01f, y1 - 0.01f, cr, cg, cb, 0.95f);
                        pushTextCenteredNdc(0.5f * (x0 + x1), y0 + 0.070f, Game::World::g_TerrainPresets[i].name, 1.00f, 0.84f, 0.94f, 1.00f, selected ? 0.97f : 0.78f);
                    }

                    if (hovered >= 0) {
                        menuTerrainIndex = hovered;
                        if (click) selectedTerrain = hovered;
                    }
                    if (activateKey && menuTerrainIndex >= 0 && menuTerrainIndex < 8) {
                        selectedTerrain = menuTerrainIndex;
                    }

                    const int applyIndex = 8;
                    bool hov = false;
                    pushButton(-0.40f, -0.56f, -0.02f, -0.44f, "APPLY", true, menuTerrainIndex == applyIndex, hov);
                    if (hov) menuTerrainIndex = applyIndex;
                    if ((click && hov) || (activateKey && menuTerrainIndex == applyIndex)) {
                        Game::World::g_CurrentPresetIndex = std::clamp(selectedTerrain, 0, 7);
                        Game::World::Generation::Init(Game::World::Generation::GetSeed());
                        chunkManager.ClearAll(worldRenderer);
                        resetCamera();
                        cameraAutoPlaced = false;
                        preloadDone = false;
                        preloadStartTime = now;
                        worldStarted = true;
                        menuOpen = false;
                        showLoadingOverlay = true;
                        menuScreen = MenuScreen::Loading;
                        mouseCaptured = false;
                        Input::SetCursorMode(false);
                        loadingStatus = "Preset aktiviert: " + Game::World::g_TerrainPresets[Game::World::g_CurrentPresetIndex].name;
                        menuStatusText = loadingStatus;
                    }

                    const int backIndex = 9;
                    pushButton(0.02f, -0.56f, 0.40f, -0.44f, "BACK", true, menuTerrainIndex == backIndex, hov);
                    if (hov) menuTerrainIndex = backIndex;
                    if ((click && hov) || (activateKey && menuTerrainIndex == backIndex)) {
                        menuScreen = MenuScreen::Main;
                    }

                    const Game::World::TerrainPreset& p = Game::World::g_TerrainPresets[std::clamp(selectedTerrain, 0, 7)];
                    pushTextCenteredNdc(0.0f, -0.68f, p.description, 1.08f, 0.72f, 0.86f, 1.00f, 0.90f);
                } else if (menuScreen == MenuScreen::Load) {
                    pushTextCenteredNdc(0.0f, 0.38f, "LOAD WORLD", 1.8f, 0.70f, 0.90f, 1.00f, 0.95f);

                    const float listX0 = -0.48f;
                    const float listX1 = 0.48f;
                    const float rowH = 0.09f;
                    float yTop = 0.22f;

                    const int maxRows = 5;
                    int hoveredRow = -1;
                    const int rowCount = std::min((int)worldList.size(), maxRows);
                    for (int i = 0; i < rowCount; ++i) {
                        const float y1 = yTop - i * (rowH + 0.02f);
                        const float y0 = y1 - rowH;
                        const bool inside = (mx >= listX0 && mx <= listX1 && my >= y0 && my <= y1);
                        if (inside) hoveredRow = i;

                        const bool selected = (i == selectedWorldIndex) || (menuLoadIndex == i);
                        const float a = selected ? 0.92f : 0.85f;
                        const float r = selected ? 0.16f : (inside ? 0.12f : 0.10f);
                        const float g = selected ? 0.18f : (inside ? 0.14f : 0.10f);
                        const float b = selected ? 0.22f : (inside ? 0.20f : 0.14f);
                        pushQuad(listX0, y0, listX1, y1, r, g, b, a);

                        // Show filename only (no directory noise)
                        const std::string name = std::filesystem::path(worldList[i]).filename().string();
                        pushTextCenteredNdc(0.0f, 0.5f * (y0 + y1), name, 1.25f, 0.85f, 0.95f, 1.00f, selected ? 0.95f : 0.75f);
                    }

                    if (hoveredRow >= 0 && click) {
                        selectedWorldIndex = hoveredRow;
                        menuLoadIndex = hoveredRow;
                    }
                    if (hoveredRow >= 0) {
                        menuLoadIndex = hoveredRow;
                    }
                    if (activateKey && menuLoadIndex >= 0 && menuLoadIndex < rowCount) {
                        selectedWorldIndex = menuLoadIndex;
                    }

                    bool hov = false;
                    const int loadIndex = rowCount;
                    const int backIndex = rowCount + 1;
                    pushButton(-0.35f, -0.44f, 0.35f, -0.32f, "LOAD", !worldList.empty(), menuLoadIndex == loadIndex, hov);
                    if (hov) menuLoadIndex = loadIndex;
                    if (((click && hov) || (activateKey && menuLoadIndex == loadIndex)) && !worldList.empty()) {
                        worldFile = worldList[selectedWorldIndex];

                        resetCamera();
                        cameraAutoPlaced = false;
                        preloadDone = (preloadRadiusChunks <= 0);
                        preloadStartTime = now;

                        beginLoadCurrentWorldAsync("Menu");
                    }

                    pushButton(-0.35f, -0.58f, 0.35f, -0.46f, "BACK", true, menuLoadIndex == backIndex, hov);
                    if (hov) menuLoadIndex = backIndex;
                    if ((click && hov) || (activateKey && menuLoadIndex == backIndex)) {
                        menuScreen = MenuScreen::Main;
                    }

                    if (worldList.empty()) {
                        pushTextCenteredNdc(0.0f, -0.06f, "No worlds found in ./worlds", 1.2f, 0.75f, 0.85f, 0.95f, 0.85f);
                    }

                    if (!menuStatusText.empty()) {
                        pushTextCenteredNdc(0.0f, -0.18f, menuStatusText, 1.3f, 1.00f, 0.55f, 0.55f, 0.95f);
                    }
                } else if (menuScreen == MenuScreen::Settings) {
                    pushTextCenteredNdc(0.0f, 0.38f, "SETTINGS", 1.8f, 0.70f, 0.90f, 1.00f, 0.95f);

                    auto toggleButton = [&](float x0, float y0, float x1, float y1, const std::string& label, bool state, bool selected, bool& hov) {
                        pushButton(x0, y0, x1, y1, label + (state ? ": ON" : ": OFF"), true, selected, hov);
                    };

                    bool hov = false;
                    // Mouse sensitivity
                    pushTextCenteredNdc(-0.18f, 0.18f, "Mouse Sens", 1.25f, 0.80f, 0.92f, 1.00f, 0.90f);
                    pushButton(0.05f, 0.13f, 0.18f, 0.22f, "-", true, menuSettingsIndex == 0, hov);
                    if (hov) menuSettingsIndex = 0;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 0) || (menuLeft && menuSettingsIndex == 0)) {
                        mouseSens = std::max(0.01f, mouseSens - 0.01f);
                        ini["mouse_sens"] = std::to_string(mouseSens);
                        SaveIniKeyValue(settingsPath, ini);
                    }
                    pushButton(0.20f, 0.13f, 0.33f, 0.22f, "+", true, menuSettingsIndex == 1, hov);
                    if (hov) menuSettingsIndex = 1;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 1) || (menuRight && menuSettingsIndex == 1)) {
                        mouseSens = std::min(1.0f, mouseSens + 0.01f);
                        ini["mouse_sens"] = std::to_string(mouseSens);
                        SaveIniKeyValue(settingsPath, ini);
                    }

                    // Invert Y
                    toggleButton(-0.35f, 0.00f, 0.35f, 0.12f, "Invert Y", invertY, menuSettingsIndex == 2, hov);
                    if (hov) menuSettingsIndex = 2;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 2)) {
                        invertY = !invertY;
                        ini["invert_y"] = invertY ? "1" : "0";
                        SaveIniKeyValue(settingsPath, ini);
                    }

                    // FOV
                    pushTextCenteredNdc(-0.22f, -0.15f, "FOV", 1.25f, 0.80f, 0.92f, 1.00f, 0.90f);
                    pushButton(0.05f, -0.20f, 0.18f, -0.11f, "-", true, menuSettingsIndex == 3, hov);
                    if (hov) menuSettingsIndex = 3;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 3) || (menuLeft && menuSettingsIndex == 3)) {
                        fovDeg = std::max(30.0f, fovDeg - 1.0f);
                        ini["fov"] = std::to_string(fovDeg);
                        SaveIniKeyValue(settingsPath, ini);
                    }
                    pushButton(0.20f, -0.20f, 0.33f, -0.11f, "+", true, menuSettingsIndex == 4, hov);
                    if (hov) menuSettingsIndex = 4;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 4) || (menuRight && menuSettingsIndex == 4)) {
                        fovDeg = std::min(120.0f, fovDeg + 1.0f);
                        ini["fov"] = std::to_string(fovDeg);
                        SaveIniKeyValue(settingsPath, ini);
                    }

                    // Graphics toggles
                    toggleButton(-0.35f, -0.36f, 0.35f, -0.24f, "VSync", vsync, menuSettingsIndex == 5, hov);
                    if (hov) menuSettingsIndex = 5;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 5)) {
                        vsync = !vsync;
                        glfwSwapInterval(vsync ? 1 : 0);
                        ini["vsync"] = vsync ? "1" : "0";
                        SaveIniKeyValue(settingsPath, ini);
                    }
                    toggleButton(-0.35f, -0.50f, 0.35f, -0.38f, "Wireframe (Keybind)", wireframe, menuSettingsIndex == 6, hov);
                    if (hov) menuSettingsIndex = 6;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 6)) {
                        std::cout << "Use keybind to toggle wireframe (default F3)." << std::endl;
                    }

                    // Quality preset
                    std::string q = (qualityLevel == 0) ? "LOW" : (qualityLevel == 1) ? "MED" : "HIGH";
                    pushButton(-0.35f, -0.64f, -0.02f, -0.52f, std::string("Quality: ") + q, true, menuSettingsIndex == 7, hov);
                    if (hov) menuSettingsIndex = 7;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 7)) {
                        qualityLevel = (qualityLevel + 1) % 3;
                        applyQuality();
                        ini["quality"] = std::to_string(qualityLevel);
                        SaveIniKeyValue(settingsPath, ini);
                        preloadDone = (preloadRadiusChunks <= 0);
                        preloadStartTime = now;
                        cameraAutoPlaced = false;
                    }

                    toggleButton(0.02f, -0.64f, 0.35f, -0.52f, "Day/Night", dayNightCycle, menuSettingsIndex == 8, hov);
                    if (hov) menuSettingsIndex = 8;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 8)) {
                        dayNightCycle = !dayNightCycle;
                        ini["day_night_cycle"] = dayNightCycle ? "1" : "0";
                        SaveIniKeyValue(settingsPath, ini);
                    }

                    pushButton(-0.35f, -0.78f, 0.35f, -0.66f, "CONTROLS...", true, menuSettingsIndex == 9, hov);
                    if (hov) menuSettingsIndex = 9;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 9)) {
                        rebindActive = false;
                        rebindAction = -1;
                        menuScreen = MenuScreen::Controls;
                    }

                    pushButton(-0.35f, -0.92f, 0.35f, -0.80f, "BACK", true, menuSettingsIndex == 10, hov);
                    if (hov) menuSettingsIndex = 10;
                    if ((click && hov) || (activateKey && menuSettingsIndex == 10)) {
                        menuScreen = MenuScreen::Main;
                    }
                } else if (menuScreen == MenuScreen::Controls) {
                    pushTextCenteredNdc(0.0f, 0.38f, "CONTROLS", 1.8f, 0.70f, 0.90f, 1.00f, 0.95f);

                    // Rebind logic: click an action, then press a key.
                    if (!rebindActive) {
                        pushTextCenteredNdc(0.0f, 0.28f, "Click an action to rebind", 1.15f, 0.75f, 0.85f, 0.95f, 0.85f);
                    } else {
                        pushTextCenteredNdc(0.0f, 0.28f, "Press a key... (ESC to cancel)", 1.15f, 0.90f, 0.95f, 1.00f, 0.95f);
                    }

                    struct ActionRow {
                        const char* label;
                        const char* iniKey;
                        int* keyPtr;
                    };
                    ActionRow rows[] = {
                        {"Forward", "key_forward", &keyForward},
                        {"Back", "key_back", &keyBack},
                        {"Left", "key_left", &keyLeft},
                        {"Right", "key_right", &keyRight},
                        {"Up", "key_up", &keyUp},
                        {"Down", "key_down", &keyDown},
                        {"Inventory", "key_inventory", &keyInventory},
                        {"Brake", "key_brake", &keyBrake},
                    };

                    const float x0 = -0.48f;
                    const float x1 = 0.48f;
                    const float rowH = 0.09f;
                    float yTop = 0.20f;

                    int hoveredRow = -1;
                    const int rowCount = (int)(sizeof(rows) / sizeof(rows[0]));
                    for (int i = 0; i < rowCount; ++i) {
                        const float y1 = yTop - i * (rowH + 0.02f);
                        const float y0 = y1 - rowH;
                        const bool inside = (mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                        if (inside) hoveredRow = i;

                        const bool selected = (rebindActive && rebindAction == i) || (menuControlsIndex == i);
                        const float r = selected ? 0.16f : (inside ? 0.11f : 0.09f);
                        const float g = selected ? 0.18f : (inside ? 0.13f : 0.09f);
                        const float b = selected ? 0.22f : (inside ? 0.18f : 0.12f);
                        pushQuad(x0, y0, x1, y1, r, g, b, 0.90f);

                        const std::string left = std::string(rows[i].label);
                        const std::string right = KeyName(*rows[i].keyPtr);
                        pushTextCenteredNdc(-0.18f, 0.5f * (y0 + y1), left, 1.15f, 0.85f, 0.95f, 1.00f, 0.88f);
                        pushTextCenteredNdc(0.28f, 0.5f * (y0 + y1), right, 1.15f, 0.70f, 0.90f, 1.00f, 0.88f);
                    }

                    auto beginRebind = [&](int actionIndex) {
                        rebindActive = true;
                        rebindAction = actionIndex;
                        menuControlsIndex = actionIndex;
                        rebindStartTime = now;
                        // Track edge presses by snapshotting a range of keys.
                        const int kMin = 32;
                        const int kMax = 348;
                        rebindPrev.assign((size_t)(kMax - kMin + 1), 0);
                        for (int k = kMin; k <= kMax; ++k) {
                            rebindPrev[(size_t)(k - kMin)] = Input::IsKeyPressed(k) ? 1 : 0;
                        }
                    };

                    if (click && hoveredRow >= 0) {
                        beginRebind(hoveredRow);
                    }
                    if (activateKey && menuControlsIndex >= 0 && menuControlsIndex < rowCount && !rebindActive) {
                        beginRebind(menuControlsIndex);
                    }
                    if (hoveredRow >= 0) {
                        menuControlsIndex = hoveredRow;
                    }

                    if (rebindActive) {
                        // Cancel
                        if (Input::IsKeyPressed(GLFW_KEY_ESCAPE)) {
                            rebindActive = false;
                            rebindAction = -1;
                        } else if ((now - rebindStartTime) > 0.10) {
                            const int kMin = 32;
                            const int kMax = 348;
                            int newKey = -1;
                            for (int k = kMin; k <= kMax; ++k) {
                                const bool down = Input::IsKeyPressed(k);
                                const bool prev = rebindPrev[(size_t)(k - kMin)] != 0;
                                if (down && !prev) {
                                    newKey = k;
                                    break;
                                }
                            }
                            // Update previous state
                            for (int k = kMin; k <= kMax; ++k) {
                                rebindPrev[(size_t)(k - kMin)] = Input::IsKeyPressed(k) ? 1 : 0;
                            }

                            if (newKey >= 0 && rebindAction >= 0 && rebindAction < rowCount) {
                                *rows[rebindAction].keyPtr = newKey;
                                ini[rows[rebindAction].iniKey] = std::to_string(newKey);
                                SaveIniKeyValue(settingsPath, ini);
                                rebindActive = false;
                                rebindAction = -1;
                            }
                        }
                    }

                    bool hov = false;
                    const int resetIndex = rowCount;
                    const int backIndex = rowCount + 1;
                    pushButton(-0.35f, -0.78f, 0.35f, -0.66f, "RESET DEFAULTS", true, menuControlsIndex == resetIndex, hov);
                    if (hov) menuControlsIndex = resetIndex;
                    if ((click && hov) || (activateKey && menuControlsIndex == resetIndex)) {
                        keyForward = GLFW_KEY_W;
                        keyBack = GLFW_KEY_S;
                        keyLeft = GLFW_KEY_A;
                        keyRight = GLFW_KEY_D;
                        keyUp = GLFW_KEY_SPACE;
                        keyDown = GLFW_KEY_LEFT_SHIFT;
                        keyInventory = GLFW_KEY_E;
                        keyBrake = GLFW_KEY_X;

                        ini["key_forward"] = std::to_string(keyForward);
                        ini["key_back"] = std::to_string(keyBack);
                        ini["key_left"] = std::to_string(keyLeft);
                        ini["key_right"] = std::to_string(keyRight);
                        ini["key_up"] = std::to_string(keyUp);
                        ini["key_down"] = std::to_string(keyDown);
                        ini["key_inventory"] = std::to_string(keyInventory);
                        ini["key_brake"] = std::to_string(keyBrake);
                        SaveIniKeyValue(settingsPath, ini);
                    }

                    pushButton(-0.35f, -0.92f, 0.35f, -0.80f, "BACK", true, menuControlsIndex == backIndex, hov);
                    if (hov) menuControlsIndex = backIndex;
                    if ((click && hov) || (activateKey && menuControlsIndex == backIndex)) {
                        rebindActive = false;
                        rebindAction = -1;
                        menuScreen = MenuScreen::Settings;
                    }
                } else if (menuScreen == MenuScreen::Amulet) {
                    const float pulse = 0.5f + 0.5f * std::sin((float)now * 2.0f);
                    const float glowA = 0.10f + 0.08f * pulse;
                    const float edgeA = 0.18f + 0.10f * pulse;
                    const float scanY = std::fmod((float)now * 0.22f, 1.0f) * 1.6f - 0.8f;
                    const float spin = (float)now * 6.0f;
                    const float spinX = 0.04f * std::cos(spin);
                    const float spinY = 0.04f * std::sin(spin);

                    pushQuad(-0.60f, 0.02f, 0.60f, 0.42f, 0.10f, 0.45f, 0.95f, glowA);
                    pushQuad(-0.55f, 0.04f, 0.55f, 0.40f, 0.04f, 0.08f, 0.14f, 0.92f);
                    pushQuad(-0.55f, 0.04f, -0.53f, 0.40f, 0.18f, 0.70f, 1.00f, edgeA);
                    pushQuad(0.53f, 0.04f, 0.55f, 0.40f, 0.18f, 0.70f, 1.00f, edgeA);
                    pushQuad(-0.55f, scanY, 0.55f, scanY + 0.02f, 0.20f, 0.75f, 1.00f, 0.18f + 0.12f * pulse);

                    auto badge = [&](float x0, float y0, const char* text, bool ok) {
                        const float x1 = x0 + 0.18f;
                        const float y1 = y0 + 0.06f;
                        const float r = ok ? 0.10f : 0.08f;
                        const float g = ok ? 0.55f : 0.18f;
                        const float b = ok ? 0.75f : 0.22f;
                        const float a = ok ? 0.85f : 0.55f;
                        pushQuad(x0, y0, x1, y1, r, g, b, a);
                        pushTextPx(ndcToPxX(x0 + 0.02f), ndcToPxY(y0 + 0.01f), text, 0.85f, 0.95f, 0.98f, 1.00f, 0.95f);
                    };

                    badge(0.18f, 0.32f, "IPC", amuletPayloadSent);
                    badge(0.38f, 0.32f, "SAVE", amuletSaveDetected);
                    badge(0.18f, 0.24f, "RELOAD", amuletReloadQueued);
                    badge(0.38f, 0.24f, "DONE", amuletReloadDone);

                    // Animated spinner shard
                    pushQuad(-0.45f + spinX, 0.28f + spinY, -0.41f + spinX, 0.32f + spinY, 0.35f, 0.85f, 1.00f, 0.85f);

                    pushTextCenteredNdc(0.0f, 0.30f, "AMULET EDITOR", 1.9f, 0.70f, 0.92f, 1.00f, 0.96f);
                    const std::string status = amuletStatusText.empty() ? "Waiting for save complete..." : amuletStatusText;
                    pushTextCenteredNdc(0.0f, 0.14f, status, 1.25f, 0.80f, 0.92f, 1.00f, 0.90f);
                    pushTextCenteredNdc(0.0f, -0.02f, "Close Amulet after saving to resume.", 1.1f, 0.70f, 0.82f, 0.92f, 0.82f);

                    const float listX = -0.46f;
                    const float listY = -0.18f;
                    const float rowH = 0.08f;
                    auto drawStep = [&](int index, const char* label, bool done) {
                        const float y0 = listY - rowH * index;
                        const float y1 = y0 + rowH * 0.75f;
                        const float fillA = done ? 0.22f : 0.08f;
                        const float edge = done ? 0.85f : 0.45f;
                        pushQuad(listX, y0, listX + 0.02f, y1, 0.15f, 0.70f, 1.00f, edge);
                        pushQuad(listX + 0.022f, y0, listX + 0.40f, y1, 0.06f, 0.10f, 0.18f, 0.88f);
                        pushQuad(listX + 0.022f, y0, listX + 0.40f * (done ? 1.0f : 0.35f), y1, 0.15f, 0.55f, 1.00f, fillA);
                        pushTextPx(ndcToPxX(listX + 0.05f), ndcToPxY(y0 + 0.01f), label, 1.05f, 0.75f, 0.90f, 1.00f, done ? 0.95f : 0.70f);
                    };

                    drawStep(0, "Payload sent", amuletPayloadSent);
                    drawStep(1, "Save detected", amuletSaveDetected);
                    drawStep(2, "Reload queued", amuletReloadQueued);
                    drawStep(3, "Reload complete", amuletReloadDone);

                    // Log panel
                    const float logX0 = 0.06f;
                    const float logX1 = 0.52f;
                    const float logY0 = -0.46f;
                    const float logY1 = -0.12f;
                    pushQuad(logX0, logY0, logX1, logY1, 0.04f, 0.06f, 0.10f, 0.90f);
                    pushTextPx(ndcToPxX(logX0 + 0.02f), ndcToPxY(logY1 - 0.03f), "Bridge Log", 1.0f, 0.65f, 0.85f, 1.00f, 0.85f);
                    for (size_t i = 0; i < amuletLog.size(); ++i) {
                        const float y = logY1 - 0.08f - (float)i * 0.07f;
                        pushTextPx(ndcToPxX(logX0 + 0.02f), ndcToPxY(y), amuletLog[i], 0.95f, 0.75f, 0.90f, 1.00f, 0.80f);
                    }

                    // Action buttons
                    bool hov = false;
                    const float bx0 = -0.16f;
                    const float bx1 = 0.16f;
                    const float by0 = -0.56f;
                    const float by1 = -0.46f;
                    pushButton(bx0, by0, bx1, by1, "RELAUNCH", true, false, hov);
                    if (hov && click) {
                        addAmuletLog("Relaunch requested");
                        amuletBridgeActive = false;
                        launchAmuletBridge();
                    }

                    const float cx0 = 0.20f;
                    const float cx1 = 0.52f;
                    pushButton(cx0, by0, cx1, by1, "RETURN", true, false, hov);
                    if (hov && click) {
                        addAmuletLog("Return to game");
                        amuletBridgeActive = false;
                        menuOpen = false;
                        menuScreen = MenuScreen::Main;
                        mouseCaptured = true;
                        Input::SetCursorMode(true);
                    }
                } else {
                    // Loading
                    const bool startupPreloadActive = (!loadingWorld && !chunkManager.IsAsyncLoadInProgress() && !preloadDone && preloadTargetChunks > 0);
                    const float p01 = startupPreloadActive
                        ? std::clamp((float)worldRenderer.GetChunkCount() / (float)preloadTargetChunks, 0.0f, 1.0f)
                        : std::clamp(chunkManager.GetAsyncLoadProgress01(), 0.0f, 1.0f);

                    const float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 2.3f);
                    pushTextCenteredNdc(0.0f, 0.34f, "AETHERFORGE LOADING", 1.85f, 0.60f + 0.18f * pulse, 0.86f + 0.10f * pulse, 1.00f, 0.98f);

                    std::string st;
                    if (startupPreloadActive) {
                        st = "Streaming megacity sectors and terrain lattice...";
                    } else {
                        st = chunkManager.GetAsyncLoadStatus();
                    }
                    if (!st.empty()) {
                        pushTextCenteredNdc(0.0f, 0.20f, st, 1.15f, 0.80f, 0.90f, 1.00f, 0.90f);
                    }

                    std::ostringstream progressLabel;
                    progressLabel.setf(std::ios::fixed);
                    progressLabel.precision(1);
                    progressLabel << (p01 * 100.0f) << "%";
                    if (startupPreloadActive) {
                        progressLabel << "  [" << worldRenderer.GetChunkCount() << "/" << preloadTargetChunks << " chunks]";
                    }

                    // Progress bar
                    const float bx0 = -0.45f;
                    const float bx1 = 0.45f;
                    const float by0 = 0.02f;
                    const float by1 = 0.10f;
                    pushQuad(bx0, by0, bx1, by1, 0.08f, 0.11f, 0.16f, 0.95f);
                    pushQuad(bx0, by0, bx0 + (bx1 - bx0) * p01, by1, 0.18f, 0.64f + 0.20f * pulse, 1.00f, 0.96f);
                    pushTextCenteredNdc(0.0f, -0.02f, progressLabel.str(), 1.15f, 0.72f, 0.90f, 1.00f, 0.92f);

                    if (startupPreloadActive) {
                        pushTextCenteredNdc(0.0f, -0.10f, "Stabilizing horizon cache to minimize pop-in...", 1.06f, 0.68f, 0.82f, 0.95f, 0.86f);
                    } else {
                        pushTextCenteredNdc(0.0f, -0.10f, "(import runs in small batches per frame)", 1.06f, 0.68f, 0.82f, 0.95f, 0.86f);
                    }
                }

                // lastMenuLmb update moved to end of frame to support inventory clicks correctly.

                glUseProgram(invProg);
                applyUiProgramUniforms(invProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                if (verts.size() > invVboCapacityFloats) {
                    invVboCapacityFloats = verts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
                if (hadCull) glEnable(GL_CULL_FACE);
            }

            // Hotbar HUD (2D overlay)
            if (!inventoryOpen && !menuVisible && hotbarEnabled && hotbarProg != 0 && hotbarVao != 0 && hotbarVbo != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                if (hadDepth) glDisable(GL_DEPTH_TEST);

                std::vector<float> verts;
                verts.reserve(12 * 2 * 6 * 9);

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
                    // Triangle 1
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    // Triangle 2
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x0, y1, r, g, b, a, 0.0f, 0.0f, 0.0f});
                };

                auto pushQuadTex = [&](float x0, float y0, float x1, float y1,
                                       float u0, float v0, float u1, float v1,
                                       float r, float g, float b, float a) {
                    // Triangle 1
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, u0, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y0, r, g, b, a, u1, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, u1, v1, 1.0f});
                    // Triangle 2
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, u0, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, u1, v1, 1.0f});
                    verts.insert(verts.end(), {x0, y1, r, g, b, a, u0, v1, 1.0f});
                };

                auto colorForBlock = [&](int id, float& r, float& g, float& b) {
                    if (IsGlassBlockId(id) || IsPaneBlockId(id)) {
                        GetGlassColorForBlock(id, r, g, b);
                        return;
                    }

                    if (IsArchitectureBlockId(id)) {
                        GetArchitectureColorForBlock(id, r, g, b);
                        return;
                    }

                    switch (id) {
                        case 1: r = 0.47f; g = 0.31f; b = 0.18f; break; // Dirt
                        case 2: r = 0.27f; g = 0.59f; b = 0.22f; break; // Grass
                        case 3: r = 0.51f; g = 0.51f; b = 0.51f; break; // Stone
                        case 4: r = 0.86f; g = 0.82f; b = 0.63f; break; // Sand
                        case 5: r = 0.24f; g = 0.47f; b = 0.82f; break; // Water
                        case 6: r = 0.94f; g = 0.94f; b = 0.98f; break; // Snow
                        case 7: r = 0.35f; g = 0.24f; b = 0.12f; break; // Wood
                        case 8: r = 0.14f; g = 0.39f; b = 0.14f; break; // Leaves
                        case 9: r = 0.78f; g = 0.78f; b = 0.78f; break; // Concrete
                        case 10: r = 0.63f; g = 0.24f; b = 0.24f; break; // Brick
                        case 12: r = 0.47f; g = 0.47f; b = 0.55f; break; // Metal
                        case 13: r = 0.88f; g = 0.88f; b = 0.91f; break; // Marble
                        case 14: r = 0.18f; g = 0.18f; b = 0.20f; break; // Basalt
                        case 15: r = 0.82f; g = 0.78f; b = 0.69f; break; // Limestone
                        case 16: r = 0.27f; g = 0.31f; b = 0.37f; break; // Slate
                        case 17: r = 0.71f; g = 0.35f; b = 0.22f; break; // Terracotta
                        case 18: r = 0.22f; g = 0.22f; b = 0.23f; break; // Asphalt
                        default: r = 0.9f; g = 0.0f; b = 0.9f; break;
                    }
                };

                auto pushInvFrame = [&](float x0, float y0, float x1, float y1, float thickness,
                                         float r, float g, float b, float a) {
                    pushQuad(x0, y0, x1, y0 + thickness, r, g, b, a);
                    pushQuad(x0, y1 - thickness, x1, y1, r, g, b, a);
                    pushQuad(x0, y0, x0 + thickness, y1, r, g, b, a);
                    pushQuad(x1 - thickness, y0, x1, y1, r, g, b, a);
                };

                const int slots = 12;
                const float size = 0.095f;
                const float pad = 0.010f;
                const float total = slots * size + (slots - 1) * pad;
                const float startX = -0.5f * total;
                const float y = -0.86f;
                const float hotbarPulse = futuristicUi ? (0.5f + 0.5f * std::sin((float)now * 3.2f)) : 0.0f;

                if (futuristicUi) {
                    const float trayX0 = startX - 0.04f;
                    const float trayX1 = startX + total + 0.04f;
                    const float trayY0 = y - 0.03f;
                    const float trayY1 = y + size + 0.03f;
                    pushQuad(trayX0 - 0.005f, trayY0 - 0.005f, trayX1 + 0.005f, trayY1 + 0.005f,
                             0.10f, 0.50f + 0.10f * hotbarPulse, 0.95f, 0.20f + 0.06f * hotbarPulse);
                    pushQuad(trayX0, trayY0, trayX1, trayY1, 0.02f, 0.05f, 0.10f, 0.90f);
                    pushQuad(trayX0 + 0.004f, trayY0 + 0.004f, trayX1 - 0.004f, trayY1 - 0.004f, 0.05f, 0.11f, 0.20f, 0.96f);
                }

                for (int i = 0; i < slots; ++i) {
                    const int id = (int)hotbarSlots[(size_t)i].id;
                    float r = 0.2f, g = 0.2f, b = 0.2f;
                    float cr = futuristicUi ? 0.05f : 0.1f;
                    float cg = futuristicUi ? 0.10f : 0.1f;
                    float cb = futuristicUi ? 0.20f : 0.1f;
                    colorForBlock(id, r, g, b);

                    const float x0 = startX + i * (size + pad);
                    const float y0 = y;
                    const float x1 = x0 + size;
                    const float y1 = y + size;

                    // Slot background
                    pushQuad(x0, y0, x1, y1, cr, cg, cb, 1.0f);

                    // Inner color (slightly inset); highlight if selected
                    const bool selected = (i == hotbarIndex);
                    const float inset = selected ? 0.003f : 0.007f;
                    float ir = 1.0f, ig = 1.0f, ib = 1.0f;
                    if (selected) {
                        ir = std::min(1.0f, ir + 0.10f);
                        ig = std::min(1.0f, ig + 0.10f);
                        ib = std::min(1.0f, ib + 0.10f);
                    }

                    if (futuristicUi) {
                        if (selected) {
                            const float neonA = 0.62f + 0.26f * hotbarPulse;
                            pushInvFrame(x0 - 0.008f, y0 - 0.008f, x1 + 0.008f, y1 + 0.008f, 0.0035f,
                                         0.00f, 0.72f, 1.00f, neonA);
                            pushInvFrame(x0 - 0.004f, y0 - 0.004f, x1 + 0.004f, y1 + 0.004f, 0.0035f,
                                         0.50f, 0.92f, 1.00f, 0.88f);
                        } else {
                            pushInvFrame(x0 - 0.003f, y0 - 0.003f, x1 + 0.003f, y1 + 0.003f, 0.003f,
                                         0.16f, 0.34f, 0.56f, 0.26f);
                        }
                    }

                    if (id > 0) {
                        const int tilesX = kAtlasTilesX;
                        const int tilesY = kAtlasTilesY;
                        const int tileIndex = id - 1;
                        const int tileX = tileIndex % tilesX;
                        const int tileY = tileIndex / tilesX;
                        const float du = 1.0f / (float)tilesX;
                        const float dv = 1.0f / (float)tilesY;
                        const float padUv = 0.01f * du;
                        const float u0 = tileX * du + padUv;
                        const float v0 = tileY * dv + padUv;
                        const float u1 = (tileX + 1) * du - padUv;
                        const float v1 = (tileY + 1) * dv - padUv;

                        pushQuadTex(x0 + inset, y0 + inset, x1 - inset, y1 - inset, u0, v0, u1, v1, ir, ig, ib, 1.0f);
                    }
                }

                glUseProgram(hotbarProg);
                applyUiProgramUniforms(hotbarProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(hotbarProg, "u_BlockAtlas"), 0);
                glBindVertexArray(hotbarVao);
                glBindBuffer(GL_ARRAY_BUFFER, hotbarVbo);
                if (verts.size() > hotbarVboCapacityFloats) {
                    hotbarVboCapacityFloats = verts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, hotbarVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            // Inventory overlay (draw on top; click selects block)
            // Inventory overlay (Minecraft-like Creative Visuals)
            if (!menuVisible && inventoryOpen && invProg != 0 && invVao != 0 && invVbo != 0) {
            { // Scope for new inventory code
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                const glm::vec2 mp = Input::GetMousePosition();
                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();
                const float mx = (w > 0.0f) ? (mp.x / w) * 2.0f - 1.0f : 0.0f;
                const float my = (h > 0.0f) ? (1.0f - (mp.y / h) * 2.0f) : 0.0f;
                const bool clickInv = lmbDown && !lastInvLmb;
                const bool rightClickInv = rmbDown && !lastInvRmb;
                const bool shiftDown = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);

                static bool lastAssignDown[9] = { false,false,false,false,false,false,false,false,false };
                const int hotbarAssignKeys[9] = {
                    GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
                    GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9
                };

                // --- DATA SETUP ---
                // (Keep existing category definitions but simplified for this view)
                struct InvItem { int id; std::string name; };
                struct InvCategory { std::string name; int iconId; std::vector<InvItem> items; };
                
                static std::vector<InvCategory> categories;
                static int builtMaxId = -1;
                static int builtImportPage = -1;

                // Rebuild categories if block count changes
                if (builtMaxId != runtimeMaxBlockId || builtImportPage != g_MinecraftImportPage) {
                    categories.clear();
                    
                    // define standard categories
                    auto addCat = [&](const std::string& name, int icon, const std::vector<int>& ids) {
                        InvCategory cat;
                        cat.name = name;
                        cat.iconId = icon;
                        for(int id : ids) {
                            cat.items.push_back({id, GetBlockDisplayName(id)});
                        }
                        categories.push_back(cat);
                    };

                    // 1. Building Blocks
                    addCat("Building", 3, {1, 3, 4, 7, 9, 10, 12, 13, 14, 15, 16, 17, 18, 52, 53, 54, 57, 58, 64, 75});
                    // 2. Nature
                    addCat("Nature", 2, {2, 5, 6, 8, 86, 87, 88, 89});
                    // 3. Glass
                    std::vector<int> glass;
                    for(int i=11; i<=34; i++) glass.push_back(i); 
                    for(int i=35; i<=51; i++) glass.push_back(i);
                    addCat("Glass", 20, glass);
                    // 4. Tech / Arch
                    addCat("Architecture", 81, {55, 56, 59, 60, 61, 62, 63, 71, 72, 79, 80, 81, 82, 83});
                    // 5. Imported (Minecraft)
                    std::vector<int> imported;
                    for(int i=kBuiltinBlockMaxId+1; i<=runtimeMaxBlockId; i++) imported.push_back(i);
                    if(!imported.empty()) addCat("Imported", kBuiltinBlockMaxId+1, imported);
                    
                    // Fallback
                    if(categories.empty()) addCat("All", 1, {1,2,3});
                    
                    builtMaxId = runtimeMaxBlockId;
                    builtImportPage = g_MinecraftImportPage;
                }
                
                static int activeTab = 0;
                static int scrollRow = 0;
                if(activeTab >= (int)categories.size()) activeTab = 0;

                // Scroll Handling
                const float scrollY = Input::GetScrollDeltaY();
                if (std::abs(scrollY) > 0.1f) {
                    scrollRow += (scrollY > 0.0f) ? -1 : 1;
                    if (scrollRow < 0) scrollRow = 0;
                }

                // --- RENDERING HELPERS ---
                std::vector<float> verts;
                verts.reserve(8192);
                std::vector<float> textVerts; 

                auto pushQuad = [&](float x0, float y0, float x1, float y1, float R, float G, float B, float A) {
                    verts.insert(verts.end(), {x0, y0, R, G, B, A, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y0, R, G, B, A, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, R, G, B, A, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x0, y0, R, G, B, A, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x1, y1, R, G, B, A, 0.0f, 0.0f, 0.0f});
                    verts.insert(verts.end(), {x0, y1, R, G, B, A, 0.0f, 0.0f, 0.0f});
                };
                
                auto pushQuadTex = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float R, float G, float B, float A) {
                     verts.insert(verts.end(), {x0, y0, R, G, B, A, u0, v0, 1.0f});
                     verts.insert(verts.end(), {x1, y0, R, G, B, A, u1, v0, 1.0f});
                     verts.insert(verts.end(), {x1, y1, R, G, B, A, u1, v1, 1.0f});
                     verts.insert(verts.end(), {x0, y0, R, G, B, A, u0, v0, 1.0f});
                     verts.insert(verts.end(), {x1, y1, R, G, B, A, u1, v1, 1.0f});
                     verts.insert(verts.end(), {x0, y1, R, G, B, A, u0, v1, 1.0f});
                };

                auto pushIcon = [&](float x, float y, float size, int id, float alpha = 1.0f) {
                    if (id <= 0) return;
                    int tilesX = kAtlasTilesX;
                    int tilesY = kAtlasTilesY; 
                    int tileIdx = id - 1;
                    int tx = tileIdx % tilesX;
                    int ty = tileIdx / tilesX;
                    float u0 = (float)tx / tilesX + 0.001f;
                    float v0 = (float)ty / tilesY + 0.001f;
                    float u1 = (float)(tx + 1) / tilesX - 0.001f;
                    float v1 = (float)(ty + 1) / tilesY - 0.001f;
                    pushQuadTex(x, y, x + size, y + size, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, alpha);
                };
                
                auto pushText = [&](float xN, float yN, const std::string& txt, float scale, bool shadow = true) {
                    if(txt.empty()) return;
                    float px = (xN * 0.5f + 0.5f) * w;
                    float py = (1.0f - (yN * 0.5f + 0.5f)) * h;
                    char buf[8192];
                    int quads = stb_easy_font_print(0, 0, (char*)txt.c_str(), nullptr, buf, sizeof(buf));
                    struct V { float x,y,z; unsigned char c[4]; };
                    V* v = (V*)buf;
                    for(int i=0; i<quads; i++) {
                        V* q = v + i*4;
                        auto emit = [&](int idx, float ox, float oy, float R, float G, float B) {
                             float nx = (px + q[idx].x*scale + ox) / w * 2.0f - 1.0f;
                             float ny = 1.0f - (py + q[idx].y*scale + oy) / h * 2.0f;
                             textVerts.insert(textVerts.end(), {nx, ny, R, G, B, 1.0f, 0.0f, 0.0f, 0.0f});
                        };
                        if(shadow) {
                           emit(0,2,2,0,0,0); emit(1,2,2,0,0,0); emit(2,2,2,0,0,0);
                           emit(0,2,2,0,0,0); emit(2,2,2,0,0,0); emit(3,2,2,0,0,0);
                        }
                        emit(0,0,0,1,1,1); emit(1,0,0,1,1,1); emit(2,0,0,1,1,1);
                        emit(0,0,0,1,1,1); emit(2,0,0,1,1,1); emit(3,0,0,1,1,1);
                    }
                };

                // --- LAYOUT CONSTANTS ---
                const float winW = 1.3f;
                const float winH = 1.3f;
                const float winX0 = -winW/2.0f;
                const float winX1 = winW/2.0f;
                const float winY0 = -winH/2.0f;
                const float winY1 = winH/2.0f;
                
                // Color Palette (Minecraft Creative Menu)
                const glm::vec4 cBg      = {0.77f, 0.77f, 0.77f, 1.0f}; // C6C6C6
                const glm::vec4 cBorderL = {1.0f, 1.0f, 1.0f, 1.0f};    // White
                const glm::vec4 cBorderD = {0.33f, 0.33f, 0.33f, 1.0f}; // Dark
                const glm::vec4 cSlot    = {0.55f, 0.55f, 0.55f, 1.0f}; // Slot
                                
                // 1. Dim Background
                pushQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.6f);

                // 2. Main Window Frame
                // Border Bevel
                pushQuad(winX0-0.005f, winY0-0.005f, winX1+0.005f, winY1+0.005f, 0.0f, 0.0f, 0.0f, 1.0f); // Black Outline
                pushQuad(winX0, winY0, winX1, winY1, cBg.r, cBg.g, cBg.b, cBg.a);
                pushQuad(winX0 + 0.01f, winY1 - 0.06f, winX1 - 0.01f, winY1 - 0.01f, 0.22f, 0.22f, 0.22f, 0.85f);
                pushText(winX0 + 0.03f, winY1 - 0.046f, "R9 CREATIVE INVENTORY", 1.2f);
                pushText(winX1 - 0.43f, winY1 - 0.046f, "LMB stack | RMB x1 | Shift+Click move", 0.95f, false);
                
                // 3. Tabs (Top)
                float tabW = 0.06f;
                float tabH = 0.06f;
                float tabX = winX0 + 0.02f;
                
                for(int i=0; i<(int)categories.size(); i++) {
                     bool active = (i == activeTab);
                     float ty0 = winY1;
                     float ty1 = winY1 + tabH;
                     float tx0 = tabX + i * (tabW + 0.005f);
                     float tx1 = tx0 + tabW;
                     
                     if (active) {
                         ty0 -= 0.01f; // overlap
                         ty1 += 0.01f; // pop up
                         pushQuad(tx0, ty0, tx1, ty1, cBg.r, cBg.g, cBg.b, 1.0f);
                     } else {
                         pushQuad(tx0, ty0, tx1, ty1, cBg.r*0.9f, cBg.g*0.9f, cBg.b*0.9f, 1.0f);
                     }
                     
                     // Helper for icon (manual)
                     int icon = categories[i].iconId;
                     if (icon > 0) {
                         float ix0 = tx0 + 0.01f;
                         float iy0 = ty0 + 0.01f;
                         float ix1 = tx1 - 0.01f;
                         float iy1 = ty1 - 0.01f;
                         int tIdx = icon - 1;
                         float u = (tIdx % kAtlasTilesX) * (1.0f/kAtlasTilesX);
                         float v = (tIdx / kAtlasTilesX) * (1.0f/kAtlasTilesY);
                         pushQuadTex(ix0, iy0, ix1, iy1, u, v, u + 1.0f/kAtlasTilesX, v + 1.0f/kAtlasTilesY, 1.0f, 1.0f, 1.0f, 1.0f);
                     }

                     if (mx >= tx0 && mx <= tx1 && my >= ty0 && my <= ty1 && clickInv) {
                         activeTab = i;
                         scrollRow = 0;
                     }
                }

                // 4. Creative Grid
                const float gridX0 = winX0 + 0.03f;
                const float gridX1 = winX1 - 0.08f; 
                const float gridY1 = winY1 - 0.15f; 
                const float gridY0 = winY0 + 0.35f; 
                
                pushText(gridX0, winY1 - 0.08f, categories[activeTab].name, 1.5f);
                
                const int cols = 9;
                const int rows = 5;
                const float slotSize = (gridX1 - gridX0) / cols;
                const float slotGap = 0.005f;
                
                const auto& items = categories[activeTab].items;
                int totalRows = (items.size() + cols - 1) / cols;
                if (scrollRow > totalRows - rows) scrollRow = std::max(0, totalRows - rows);
                
                int startIndex = scrollRow * cols;
                int hoveredItemId = -1;
                std::string hoveredItemName;
                
                for(int r=0; r<rows; r++) {
                    for(int c=0; c<cols; c++) {
                        int idx = startIndex + r*cols + c;
                        float sx0 = gridX0 + c * slotSize;
                        float sy1 = gridY1 - r * slotSize;
                        float sx1 = sx0 + slotSize - slotGap;
                        float sy0 = sy1 - slotSize + slotGap;
                        
                        pushQuad(sx0, sy0, sx1, sy1, cSlot.r, cSlot.g, cSlot.b, 1.0f);
                        pushQuad(sx0, sy1-0.002f, sx1, sy1, 0.2f,0.2f,0.2f,1.0f); // Shadows
                        pushQuad(sx0, sy0, sx0+0.002f, sy1, 0.2f,0.2f,0.2f,1.0f);
                        pushQuad(sx1-0.002f, sy0, sx1, sy1, 1.0f,1.0f,1.0f,1.0f); // Highlights
                        pushQuad(sx0, sy0, sx1, sy0+0.002f, 1.0f,1.0f,1.0f,1.0f);
                        
                        if (idx < (int)items.size()) {
                            int id = items[idx].id;
                            int tIdx = id - 1;
                            float u = (tIdx % kAtlasTilesX) * (1.0f/kAtlasTilesX);
                            float v = (tIdx / kAtlasTilesX) * (1.0f/kAtlasTilesY);
                            float p = 0.01f;
                            pushQuadTex(sx0+p, sy0+p, sx1-p, sy1-p, u, v, u+(1.0f/kAtlasTilesX), v+(1.0f/kAtlasTilesY), 1.0f,1.0f,1.0f,1.0f);
                            if (id == selectedBlockId) {
                                pushQuad(sx0, sy1 - 0.006f, sx1, sy1, 0.96f, 0.86f, 0.22f, 1.0f);
                                pushQuad(sx0, sy0, sx1, sy0 + 0.006f, 0.96f, 0.86f, 0.22f, 1.0f);
                                pushQuad(sx0, sy0, sx0 + 0.006f, sy1, 0.96f, 0.86f, 0.22f, 1.0f);
                                pushQuad(sx1 - 0.006f, sy0, sx1, sy1, 0.96f, 0.86f, 0.22f, 1.0f);
                            }
                            
                            if (mx >= sx0 && mx <= sx1 && my >= sy0 && my <= sy1) {
                                hoveredItemId = id;
                                hoveredItemName = items[(size_t)idx].name;
                                pushQuad(sx0, sy0, sx1, sy1, 1.0f, 1.0f, 1.0f, 0.3f);
                                if (clickInv) {
                                    mouseCursorStack.id = id;
                                    mouseCursorStack.count = 64;
                                }
                                if (rightClickInv) {
                                    mouseCursorStack.id = id;
                                    mouseCursorStack.count = 1;
                                }
                            }
                        }
                    }
                }

                // Quick workflow: assign hovered creative item directly to hotbar by pressing 1..9.
                for (int hk = 0; hk < 9; ++hk) {
                    const bool keyDown = Input::IsKeyPressed(hotbarAssignKeys[hk]);
                    if (keyDown && !lastAssignDown[hk] && hoveredItemId > 0) {
                        setHotbarStack(hk, (uint16_t)hoveredItemId, kItemStackMaxCount);
                        hotbarIndex = hk;
                        selectedBlockId = hoveredItemId;
                    }
                    lastAssignDown[hk] = keyDown;
                }

                if (hoveredItemId > 0) {
                    const std::string info = hoveredItemName + "  [ID " + std::to_string(hoveredItemId) + "]";
                    pushText(gridX0, gridY0 - 0.035f, info, 1.1f);
                    pushText(gridX1 - 0.35f, gridY0 - 0.035f, "Press 1..9 -> Hotbar", 0.95f, false);
                }
                
                // Scrollbar
                float sbX0 = gridX1 + 0.01f;
                float sbX1 = winX1 - 0.02f;
                float sbH = gridY1 - gridY0;
                pushQuad(sbX0, gridY0, sbX1, gridY1, 0.0f, 0.0f, 0.0f, 0.5f);
                float handleH = (totalRows > 0) ? (sbH * ((float)rows / totalRows)) : sbH;
                if(handleH > sbH) handleH = sbH;
                float maxScroll = (float)std::max(0, totalRows - rows);
                float scrollFrac = (maxScroll > 0) ? ((float)scrollRow / maxScroll) : 0.0f;
                float handleY1 = gridY1 - (sbH - handleH) * scrollFrac;
                float handleY0 = handleY1 - handleH;
                pushQuad(sbX0, handleY0, sbX1, handleY1, 0.8f, 0.8f, 0.8f, 1.0f);

                // 5. Player Inventory (Bottom)
                float invY1 = gridY0 - 0.05f;
                float invSlotSize = slotSize;
                for(int r=0; r<3; r++) {
                    for(int c=0; c<9; c++) {
                        float sx0 = gridX0 + c * invSlotSize;
                        float sy1 = invY1 - r * invSlotSize;
                        float sx1 = sx0 + invSlotSize - slotGap;
                        float sy0 = sy1 - invSlotSize + slotGap;
                        pushQuad(sx0, sy0, sx1, sy1, cSlot.r, cSlot.g, cSlot.b, 1.0f);
                        int slotIdx = 9 + r * 9 + c;
                         if(slotIdx < (int)mainInventorySlots.size()) {
                             ItemStack& st = mainInventorySlots[slotIdx];
                             if(st.count > 0) {
                                int tIdx = st.id - 1;
                                float u = (tIdx % kAtlasTilesX) * (1.0f/kAtlasTilesX);
                                float v = (tIdx / kAtlasTilesX) * (1.0f/kAtlasTilesY);
                                pushQuadTex(sx0+0.01f, sy0+0.01f, sx1-0.01f, sy1-0.01f, u, v, u+(1.0/16.0), v+(1.0/16.0), 1.0f,1.0f,1.0f,1.0f);
                             }
                             if (mx >= sx0 && mx <= sx1 && my >= sy0 && my <= sy1) {
                                pushQuad(sx0, sy0, sx1, sy1, 1.0f, 1.0f, 1.0f, 0.3f);
                                if(clickInv) {
                                     if (shiftDown && st.count > 0) {
                                        for (int hb = 0; hb < 9; ++hb) {
                                            if (hotbarSlots[(size_t)hb].count == 0) {
                                                hotbarSlots[(size_t)hb] = st;
                                                st = ItemStack{};
                                                break;
                                            }
                                        }
                                     } else if (mouseCursorStack.count == 0) { mouseCursorStack = st; st = ItemStack{}; }
                                     else if (st.count == 0) { st = mouseCursorStack; mouseCursorStack = ItemStack{}; }
                                     else { std::swap(st, mouseCursorStack); }
                                }
                             }
                         }
                    }
                }
                
                // Hotbar
                float hotbarY1 = invY1 - 3.2f * invSlotSize;
                 for(int c=0; c<9; c++) {
                        float sx0 = gridX0 + c * invSlotSize;
                        float sy1 = hotbarY1;
                        float sx1 = sx0 + invSlotSize - slotGap;
                        float sy0 = sy1 - invSlotSize + slotGap;
                        pushQuad(sx0, sy0, sx1, sy1, cSlot.r, cSlot.g, cSlot.b, 1.0f);
                         if(c < (int)hotbarSlots.size()) {
                             ItemStack& st = hotbarSlots[c];
                             if(st.count > 0) {
                                int tIdx = st.id - 1;
                                float u = (tIdx % kAtlasTilesX) * (1.0f/kAtlasTilesX);
                                float v = (tIdx / kAtlasTilesX) * (1.0f/kAtlasTilesY);
                                pushQuadTex(sx0+0.01f, sy0+0.01f, sx1-0.01f, sy1-0.01f, u, v, u+(1.0/16.0), v+(1.0/16.0), 1.0f,1.0f,1.0f,1.0f);
                             }
                             if (mx >= sx0 && mx <= sx1 && my >= sy0 && my <= sy1) {
                                pushQuad(sx0, sy0, sx1, sy1, 1.0f, 1.0f, 1.0f, 0.3f);
                                if(clickInv) {
                                    if (mouseCursorStack.count == 0) { mouseCursorStack = st; st = ItemStack{}; }
                                     else if (st.count == 0) { st = mouseCursorStack; mouseCursorStack = ItemStack{}; }
                                     else { std::swap(st, mouseCursorStack); }
                                }
                             }
                        }
                 }
                
                // Cursor
                if (mouseCursorStack.count > 0) {
                    float ms = 0.08f; 
                    float mxa = mx - ms/2.0f;
                    float mya = my - ms/2.0f;
                    int tIdx = mouseCursorStack.id - 1;
                    float u = (tIdx % kAtlasTilesX) * (1.0f/kAtlasTilesX);
                    float v = (tIdx / kAtlasTilesX) * (1.0f/kAtlasTilesY);
                    pushQuadTex(mxa, mya, mxa+ms, mya+ms, u, v, u+(1.0/16.0), v+(1.0/16.0), 1.0f,1.0f,1.0f,1.0f);
                }

                // Keep click edge state in sync while inventory is open.
                lastInvLmb = lmbDown;
                lastInvRmb = rmbDown;

                // Draw inventory geometry + text generated above.
                verts.insert(verts.end(), textVerts.begin(), textVerts.end());
                if (!verts.empty()) {
                    glUseProgram(invProg);
                    applyUiProgramUniforms(invProg);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                    glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                    glBindVertexArray(invVao);
                    glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                    if (verts.size() > invVboCapacityFloats) {
                        invVboCapacityFloats = verts.size() * 2;
                        glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                    }
                    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
                    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 9));
                    glBindVertexArray(0);
                    glUseProgram(0);
                }

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);

            }
            // Declarations moved outside for shared access (fix C2065)
            bool hadBlend = glIsEnabled(GL_BLEND);
            bool hadDepth = glIsEnabled(GL_DEPTH_TEST);
            
            if (false) { // Old code disabled
                std::vector<float> verts;
                std::vector<float> textVerts;

                // Local copies for disabled block compilation
                const glm::vec2 mp = Input::GetMousePosition();
                const float w = (float)Window::GetWidth();
                const float h = (float)Window::GetHeight();
                const float mx = (w > 0.0f) ? (mp.x / w) * 2.0f - 1.0f : 0.0f;
                const float my = (h > 0.0f) ? (1.0f - (mp.y / h) * 2.0f) : 0.0f;
                const bool clickInv = lmbDown && !lastInvLmb;
                const bool rightClickInv = rmbDown && !lastInvRmb;
                const bool releaseInv = !lmbDown && lastInvLmb;

                // hadBlend/hadDepth moved outside

                // Dummy helpers for disabled block (only missing ones)
                auto pushQuad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {};
                // auto pushQuadTex removed (original exists below)
                // auto pushTextInv removed (original exists below)
                // auto pushTextInvLeft removed (original exists below)
                // auto pushInvFrame removed (original exists below)
                // auto colorForBlock removed (original exists below)
                // auto findItemName removed (original exists below)
                
                auto HandleInventoryClick = [&](std::vector<ItemStack>& slots, int slotIndex, int button) {};

                // Use static dummies for state vars
                static std::string invSearch;
                static bool invSearchActive = false;
                static bool invUppercase = false;
                static std::array<uint8_t, 512> invKeyPrev{};
                static std::array<double, 512> invKeyNextRepeat{};
                static std::array<uint8_t, 512> invNavPrev{};
                static int invScrollRow = 0;
                static int invLastTab = 0;
                static std::string invLastSearch;
                static int invGridSel = 0;
                static int invSearchCursor = 0;
                static bool invDraggingScroll = false;
                static float invDragGrabOffset = 0.0f;
                static double invLastTime = 0.0;
                static float invScrollVelocity = 0.0f;
                static float invScrollAccum = 0.0f;
                static std::array<uint8_t, 512> invHotbarKeyPrev{};
                static bool invPaletteDragActive = false;
                



                auto pushQuadTex = [&](float x0, float y0, float x1, float y1,
                                       float u0, float v0, float u1, float v1,
                                       float r, float g, float b, float a) {
                    // Triangle 1
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, u0, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y0, r, g, b, a, u1, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, u1, v1, 1.0f});
                    // Triangle 2
                    verts.insert(verts.end(), {x0, y0, r, g, b, a, u0, v0, 1.0f});
                    verts.insert(verts.end(), {x1, y1, r, g, b, a, u1, v1, 1.0f});
                    verts.insert(verts.end(), {x0, y1, r, g, b, a, u0, v1, 1.0f});
                };

                // Local helper for text
                auto pushTextInv = [&](float cxNdc, float cyNdc, const char* text, float scale, bool highlight) {
                    if (!text || !*text) return;
                    float x = (cxNdc * 0.5f + 0.5f) * w;
                    float y = (1.0f - (cyNdc * 0.5f + 0.5f)) * h;
                    const int tw = stb_easy_font_width((char*)text);
                    const int th = stb_easy_font_height((char*)text);
                    x -= (tw * scale) * 0.5f;
                    y -= (th * scale) * 0.5f;

                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text, nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex { float x, y, z; unsigned char c[4]; };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;

                    const float r = highlight ? 1.0f : 0.9f;
                    const float g = highlight ? 1.0f : 0.9f;
                    const float b = highlight ? 1.0f : 0.9f;

                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = x + quad[idx].x * scale;
                            const float py = y + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            textVerts.insert(textVerts.end(), {nx, ny, r, g, b, 1.0f, 0.0f, 0.0f, 0.0f});
                        };
                        emit(0);
                        emit(1);
                        emit(2);
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                auto pushTextInvLeft = [&](float xNdc, float cyNdc, const char* text, float scale, bool highlight) {
                    if (!text || !*text) return;
                    float x = (xNdc * 0.5f + 0.5f) * w;
                    float y = (1.0f - (cyNdc * 0.5f + 0.5f)) * h;
                    const int th = stb_easy_font_height((char*)text);
                    y -= (th * scale) * 0.5f;

                    char buf[65536];
                    const int quads = stb_easy_font_print(0.0f, 0.0f, (char*)text, nullptr, buf, (int)sizeof(buf));
                    struct EasyFontVertex { float x, y, z; unsigned char c[4]; };
                    const EasyFontVertex* v = (const EasyFontVertex*)buf;

                    const float r = highlight ? 1.0f : 0.9f;
                    const float g = highlight ? 1.0f : 0.9f;
                    const float b = highlight ? 1.0f : 0.9f;

                    for (int q = 0; q < quads; ++q) {
                        const EasyFontVertex* quad = v + q * 4;
                        auto emit = [&](int idx) {
                            const float px = x + quad[idx].x * scale;
                            const float py = y + quad[idx].y * scale;
                            const float nx = (w > 0.0f) ? (px / w) * 2.0f - 1.0f : 0.0f;
                            const float ny = (h > 0.0f) ? (1.0f - (py / h) * 2.0f) : 0.0f;
                            textVerts.insert(textVerts.end(), {nx, ny, r, g, b, 1.0f, 0.0f, 0.0f, 0.0f});
                        };
                        emit(0);
                        emit(1);
                        emit(2);
                        emit(0);
                        emit(2);
                        emit(3);
                    }
                };

                auto colorForBlock = [&](int id, float& r, float& g, float& b) {
                    if (IsGlassBlockId(id) || IsPaneBlockId(id)) {
                        GetGlassColorForBlock(id, r, g, b);
                        return;
                    }

                    if (IsArchitectureBlockId(id)) {
                        GetArchitectureColorForBlock(id, r, g, b);
                        return;
                    }

                    switch (id) {
                        case 1: r = 0.47f; g = 0.31f; b = 0.18f; break; // Dirt
                        case 2: r = 0.27f; g = 0.59f; b = 0.22f; break; // Grass
                        case 3: r = 0.51f; g = 0.51f; b = 0.51f; break; // Stone
                        case 4: r = 0.86f; g = 0.82f; b = 0.63f; break; // Sand
                        case 5: r = 0.24f; g = 0.47f; b = 0.82f; break; // Water
                        case 6: r = 0.94f; g = 0.94f; b = 0.98f; break; // Snow
                        case 7: r = 0.35f; g = 0.24f; b = 0.12f; break; // Wood
                        case 8: r = 0.14f; g = 0.39f; b = 0.14f; break; // Leaves
                        case 9: r = 0.78f; g = 0.78f; b = 0.78f; break; // Concrete
                        case 10: r = 0.63f; g = 0.24f; b = 0.24f; break; // Brick
                        case 12: r = 0.47f; g = 0.47f; b = 0.55f; break; // Metal
                        case 13: r = 0.88f; g = 0.88f; b = 0.91f; break; // Marble
                        case 14: r = 0.18f; g = 0.18f; b = 0.20f; break; // Basalt
                        case 15: r = 0.82f; g = 0.78f; b = 0.69f; break; // Limestone
                        case 16: r = 0.27f; g = 0.31f; b = 0.37f; break; // Slate
                        case 17: r = 0.71f; g = 0.35f; b = 0.22f; break; // Terracotta
                        case 18: r = 0.22f; g = 0.22f; b = 0.23f; break; // Asphalt
                        default: r = 0.9f; g = 0.0f; b = 0.9f; break;
                    }
                };

                auto pushInvFrame = [&](float x0, float y0, float x1, float y1, float thickness,
                                         float r, float g, float b, float a) {
                    pushQuad(x0, y0, x1, y0 + thickness, r, g, b, a);
                    pushQuad(x0, y1 - thickness, x1, y1, r, g, b, a);
                    pushQuad(x0, y0, x0 + thickness, y1, r, g, b, a);
                    pushQuad(x1 - thickness, y0, x1, y1, r, g, b, a);
                };

                // Backdrop layers (depth + focus)
                pushQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.02f, 0.03f, 0.05f, 0.96f);
                pushQuad(-0.96f, -0.96f, 0.96f, 0.96f, 0.05f, 0.07f, 0.11f, 0.18f);
                pushQuad(-0.84f, -0.84f, 0.84f, 0.84f, 0.07f, 0.10f, 0.16f, 0.12f);

                // Main panel frame (compact)
                const float panelX0 = -0.74f;
                const float panelX1 = 0.74f;
                const float panelY0 = -0.64f;
                const float panelY1 = 0.62f;
                pushQuad(panelX0 - 0.025f, panelY0 - 0.025f, panelX1 + 0.025f, panelY1 + 0.025f, 0.18f, 0.45f, 0.90f, 0.14f);
                pushQuad(panelX0 - 0.008f, panelY0 - 0.008f, panelX1 + 0.008f, panelY1 + 0.008f, 0.12f, 0.24f, 0.42f, 0.20f);
                pushQuad(panelX0, panelY0, panelX1, panelY1, 0.05f, 0.07f, 0.11f, 0.97f);
                pushInvFrame(panelX0, panelY0, panelX1, panelY1, 0.006f, 0.13f, 0.20f, 0.31f, 0.94f);

                // Title bar
                pushQuad(panelX0, panelY1 - 0.10f, panelX1, panelY1, 0.08f, 0.12f, 0.18f, 0.98f);
                pushQuad(panelX0 + 0.01f, panelY1 - 0.097f, panelX1 - 0.01f, panelY1 - 0.091f, 0.28f, 0.54f, 0.90f, 0.85f);
                pushTextInv(0.0f, panelY1 - 0.05f, "INVENTORY", 2.0f, true);
                pushTextInvLeft(panelX0 + 0.02f, panelY1 - 0.078f, "R9 BUILD INTERFACE", 0.95f, false);

                struct InvItem { int id; const char* name; };
                struct InvCategory { const char* name; const char* desc; int iconId; std::vector<InvItem> items; };
                static std::vector<std::string> mcExtraNames;
                static std::vector<InvItem> mcExtraItems;
                static int mcExtraBuiltMax = -1;
                static int mcExtraBuiltPage = -1;
                if (mcExtraBuiltMax != runtimeMaxBlockId || mcExtraBuiltPage != g_MinecraftImportPage) {
                    mcExtraNames.clear();
                    mcExtraItems.clear();
                    const int extraCount = std::max(0, runtimeMaxBlockId - kBuiltinBlockMaxId);
                    mcExtraNames.reserve((size_t)extraCount);
                    mcExtraItems.reserve((size_t)extraCount);

                    for (int id = kBuiltinBlockMaxId + 1; id <= runtimeMaxBlockId; ++id) {
                        const std::string tex = GetTextureNameForBlockId(id);
                        if (tex.empty()) continue;
                        mcExtraNames.push_back(HumanizeBlockToken(tex));
                        mcExtraItems.push_back(InvItem{ id, mcExtraNames.back().c_str() });
                    }
                    mcExtraBuiltMax = runtimeMaxBlockId;
                    mcExtraBuiltPage = g_MinecraftImportPage;
                }

                static std::vector<InvCategory> categories;
                static int categoriesBuiltMax = -1;
                static int categoriesBuiltPage = -1;
                if (categoriesBuiltMax != runtimeMaxBlockId || categoriesBuiltPage != g_MinecraftImportPage) {
                    categories.clear();
                    categories = {
                        { "Nature", "Terrain + foliage", 2, {
                            { 2, "Grass Block" }, { 1, "Dirt" }, { 4, "Sand" },
                            { 6, "Snow" }, { 8, "Leaves" }, { 5, "Water" }
                        }},
                        { "Base Build", "Core structural blocks", 3, {
                            { 3, "Stone" }, { 7, "Wood Planks" }, { 10, "Brick" },
                            { 9, "Concrete" }, { 12, "Metal Block" },
                            { 13, "Marble" }, { 14, "Basalt" }, { 15, "Limestone" },
                            { 16, "Slate" }, { 17, "Terracotta" }, { 18, "Asphalt" }
                        }},
                        { "Glass Blocks", "Clear + dyed solid glass", 11, {
                            { 11, "Clear Glass" },
                            { 19, "White Glass" }, { 20, "Light Gray Glass" }, { 21, "Gray Glass" },
                            { 22, "Black Glass" }, { 23, "Red Glass" }, { 24, "Orange Glass" },
                            { 25, "Yellow Glass" }, { 26, "Lime Glass" }, { 27, "Green Glass" },
                            { 28, "Cyan Glass" }, { 29, "Light Blue Glass" }, { 30, "Blue Glass" },
                            { 31, "Purple Glass" }, { 32, "Magenta Glass" }, { 33, "Pink Glass" },
                            { 34, "Brown Glass" }
                        }},
                        { "Glass Panes", "Slim framed pane variants", 35, {
                            { 35, "Clear Glass Pane" },
                            { 36, "White Glass Pane" }, { 37, "Light Gray Glass Pane" }, { 38, "Gray Glass Pane" },
                            { 39, "Black Glass Pane" }, { 40, "Red Glass Pane" }, { 41, "Orange Glass Pane" },
                            { 42, "Yellow Glass Pane" }, { 43, "Lime Glass Pane" }, { 44, "Green Glass Pane" },
                            { 45, "Cyan Glass Pane" }, { 46, "Light Blue Glass Pane" }, { 47, "Blue Glass Pane" },
                            { 48, "Purple Glass Pane" }, { 49, "Magenta Glass Pane" }, { 50, "Pink Glass Pane" },
                            { 51, "Brown Glass Pane" }
                        }},
                        { "Arch Concrete", "Facade concrete systems", 52, {
                            { 52, "Smooth Concrete" }, { 53, "Dark Concrete" }, { 54, "White Concrete" },
                            { 55, "Concrete Tile Grid" }, { 56, "Concrete Panel Joints" },
                            { 73, "Fine Plaster" }, { 74, "Stucco" }
                        }},
                        { "Arch Metals", "Industrial and modern metals", 57, {
                            { 57, "Brushed Steel" }, { 58, "Steel Plate" }, { 59, "Steel Grate" },
                            { 60, "Aluminum Panel" }, { 61, "Carbon Steel" },
                            { 62, "Copper Cladding" }, { 63, "Oxidized Copper" },
                            { 80, "Vent Panel" }, { 81, "Circuit Panel" }, { 82, "Light Panel" },
                            { 83, "Structural Beam" }
                        }},
                        { "Arch Stone", "Premium cladding and trims", 64, {
                            { 64, "Polished Stone" }, { 65, "Polished Basalt" }, { 66, "Polished Granite" },
                            { 67, "Polished Limestone" }, { 68, "Slate Tile Grid" },
                            { 71, "Ceramic Tile" }, { 72, "Ceramic Tile Dark" },
                            { 75, "White Brick" }, { 76, "Dark Brick" },
                            { 77, "Trim Stone" }, { 78, "Column Stone" }, { 79, "Window Frame" },
                            { 69, "Roof Shingles" }, { 70, "Dark Roof Shingles" }
                        }}
                    };

                    if (!mcExtraItems.empty()) {
                        categories.push_back(InvCategory{ "Minecraft Blocks", "Imported local block set", kBuiltinBlockMaxId + 1, mcExtraItems });
                    }
                    categoriesBuiltMax = runtimeMaxBlockId;
                    categoriesBuiltPage = g_MinecraftImportPage;
                }

                auto findItemName = [&](int id) -> const char* {
                    for (const auto& cat : categories) {
                        for (const auto& it : cat.items) {
                            if (it.id == id) return it.name;
                        }
                    }
                    return GetBlockDisplayName(id);
                };

                static int activeTab = 0;
                if (categories.empty()) {
                    categories.push_back(InvCategory{ "Fallback", "No blocks", 1, {} });
                }
                activeTab = std::clamp(activeTab, 0, (int)categories.size() - 1);

                // Category panel (larger, fast scan)
                const float catX0 = panelX0 + 0.05f;
                const float catX1 = catX0 + 0.26f;
                const float catY1 = panelY1 - 0.14f;
                const float catY0 = panelY0 + 0.22f;
                pushQuad(catX0, catY0, catX1, catY1, 0.07f, 0.09f, 0.13f, 0.95f);
                pushInvFrame(catX0, catY0, catX1, catY1, 0.004f, 0.12f, 0.16f, 0.24f, 0.9f);

                const float catRowH = 0.072f;
                const float catPad = 0.010f;
                float catTop = catY1 - 0.02f;
                for (int t = 0; t < (int)categories.size(); ++t) {
                    const float y1 = catTop - t * (catRowH + catPad);
                    const float y0 = y1 - catRowH;
                    if (y0 < catY0) break;

                    const bool hover = (mx >= catX0 + 0.01f && mx <= catX1 - 0.01f && my >= y0 && my <= y1);
                    const bool active = (t == activeTab);
                    const float r = active ? 0.18f : (hover ? 0.14f : 0.10f);
                    const float g = active ? 0.20f : (hover ? 0.16f : 0.12f);
                    const float b = active ? 0.26f : (hover ? 0.20f : 0.16f);
                    pushQuad(catX0 + 0.01f, y0, catX1 - 0.01f, y1, r, g, b, 0.98f);
                    if (active) {
                        pushQuad(catX0 + 0.012f, y0 + 0.004f, catX0 + 0.018f, y1 - 0.004f, 0.36f, 0.62f, 0.96f, 0.96f);
                    }
                    if (active) {
                        pushInvFrame(catX0 + 0.01f, y0, catX1 - 0.01f, y1, 0.004f, 0.24f, 0.32f, 0.45f, 0.95f);
                    }

                    const int iconId = categories[t].iconId;
                    const int tilesX = kAtlasTilesX;
                    const int tilesY = kAtlasTilesY;
                    const int tileIndex = iconId - 1;
                    const int tileX = tileIndex % tilesX;
                    const int tileY = tileIndex / tilesX;
                    const float du = 1.0f / (float)tilesX;
                    const float dv = 1.0f / (float)tilesY;
                    const float padUv = 0.01f * du;
                    const float u0 = tileX * du + padUv;
                    const float v0 = tileY * dv + padUv;
                    const float u1 = (tileX + 1) * du - padUv;
                    const float v1 = (tileY + 1) * dv - padUv;

                    const float iconSize = (y1 - y0) * 0.68f;
                    const float iconX0 = 0.5f * (catX0 + catX1) - 0.5f * iconSize;
                    const float iconX1 = iconX0 + iconSize;
                    const float iconY0 = 0.5f * (y0 + y1) - 0.5f * iconSize;
                    const float iconY1 = iconY0 + iconSize;
                    pushQuadTex(iconX0, iconY0, iconX1, iconY1, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);

                    if (clickInv && hover) {
                        activeTab = t;
                    }
                }

                const float contentX0 = catX1 + 0.04f;
                const float contentX1 = panelX1 - 0.06f;

                const double nowTime = glfwGetTime();
                const float pulse = 0.5f + 0.5f * std::sin((float)nowTime * 6.0f);

                // Search bar
                const float searchY1 = panelY1 - 0.20f;
                const float searchY0 = searchY1 - 0.08f;
                const float searchX0 = contentX0;
                const float searchX1 = contentX1;
                const bool searchHover = (mx >= searchX0 && mx <= searchX1 && my >= searchY0 && my <= searchY1);
                if (clickInv) {
                    invSearchActive = searchHover;
                    if (invSearchActive) {
                        invSearchCursor = (int)invSearch.size();
                    }
                }

                pushQuad(searchX0, searchY0, searchX1, searchY1, 0.06f, 0.08f, 0.12f, 0.98f);
                pushQuad(searchX0 + 0.005f, searchY0 + 0.005f, searchX1 - 0.005f, searchY1 - 0.005f, 0.10f, 0.12f, 0.18f, 0.98f);
                if (invSearchActive) {
                    pushInvFrame(searchX0 - 0.003f, searchY0 - 0.003f, searchX1 + 0.003f, searchY1 + 0.003f, 0.003f, 0.26f, 0.44f, 0.72f, 0.98f);
                } else if (searchHover) {
                    pushInvFrame(searchX0 - 0.002f, searchY0 - 0.002f, searchX1 + 0.002f, searchY1 + 0.002f, 0.002f, 0.20f, 0.32f, 0.50f, 0.75f);
                }
                pushTextInvLeft(searchX0, searchY1 + 0.05f, "SEARCH + FILTER", 1.15f, true);
                pushTextInvLeft(searchX1 - 0.38f, searchY1 + 0.05f, "DRAG TO HOTBAR", 1.05f, false);
                pushTextInvLeft(searchX0 + 0.02f, searchY0 - 0.025f, "ENTER: assign | ARROWS: navigate | ESC: leave search", 0.95f, false);

                // Uppercase toggle button
                const float capsX1 = searchX1 - 0.01f;
                const float capsX0 = capsX1 - 0.08f;
                const float capsY0 = searchY0 + 0.01f;
                const float capsY1 = searchY1 - 0.01f;
                const bool capsHover = (mx >= capsX0 && mx <= capsX1 && my >= capsY0 && my <= capsY1);
                pushQuad(capsX0, capsY0, capsX1, capsY1, capsHover ? 0.18f : 0.12f, 0.16f, 0.22f, 0.98f);
                pushTextInv(0.5f * (capsX0 + capsX1), 0.5f * (capsY0 + capsY1), invUppercase ? "ABC" : "abc", 1.2f, invUppercase);
                if (clickInv && capsHover) {
                    invUppercase = !invUppercase;
                }

                // Clear button
                const float clearX1 = capsX0 - 0.01f;
                const float clearX0 = clearX1 - 0.06f;
                const bool clearHover = (mx >= clearX0 && mx <= clearX1 && my >= capsY0 && my <= capsY1);
                pushQuad(clearX0, capsY0, clearX1, capsY1, clearHover ? 0.18f : 0.12f, 0.12f, 0.16f, 0.98f);
                pushTextInv(0.5f * (clearX0 + clearX1), 0.5f * (capsY0 + capsY1), "X", 1.2f, clearHover);
                if (clickInv && clearHover) {
                    invSearch.clear();
                    invSearchCursor = 0;
                }

                invSearchCursor = std::clamp(invSearchCursor, 0, (int)invSearch.size());
                std::string searchDisplay = invSearch;
                if (invSearchActive && std::fmod((float)glfwGetTime(), 1.0f) < 0.5f) {
                    searchDisplay.insert(searchDisplay.begin() + invSearchCursor, '|');
                }
                if (invSearch.empty() && !invSearchActive) {
                    pushTextInvLeft(searchX0 + 0.02f, 0.5f * (searchY0 + searchY1), "Type to search...", 1.25f, false);
                } else {
                    pushTextInvLeft(searchX0 + 0.02f, 0.5f * (searchY0 + searchY1), searchDisplay.c_str(), 1.6f, true);
                }

                // Search input (basic A-Z, 0-9, space, backspace)
                auto emitChar = [&](char ch) {
                    if (invSearch.size() >= 24) return;
                    invSearch.insert(invSearch.begin() + invSearchCursor, ch);
                    invSearchCursor = std::min((int)invSearch.size(), invSearchCursor + 1);
                };

                auto handleChar = [&](int key, char ch) {
                    const bool down = Input::IsKeyPressed(key);
                    const bool prev = invKeyPrev[(size_t)key] != 0;
                    if (invSearchActive && down && !prev) {
                        emitChar(ch);
                        invKeyNextRepeat[(size_t)key] = nowTime + 0.35;
                    } else if (invSearchActive && down && prev) {
                        if (nowTime >= invKeyNextRepeat[(size_t)key]) {
                            emitChar(ch);
                            invKeyNextRepeat[(size_t)key] = nowTime + 0.05;
                        }
                    } else if (!down) {
                        invKeyNextRepeat[(size_t)key] = 0.0;
                    }
                    invKeyPrev[(size_t)key] = down ? 1 : 0;
                };

                auto handleKey = [&](int key, auto onPress) {
                    const bool down = Input::IsKeyPressed(key);
                    const bool prev = invKeyPrev[(size_t)key] != 0;
                    if (invSearchActive && down && !prev) {
                        onPress();
                        invKeyNextRepeat[(size_t)key] = nowTime + 0.35;
                    } else if (invSearchActive && down && prev) {
                        if (nowTime >= invKeyNextRepeat[(size_t)key]) {
                            onPress();
                            invKeyNextRepeat[(size_t)key] = nowTime + 0.05;
                        }
                    } else if (!down) {
                        invKeyNextRepeat[(size_t)key] = 0.0;
                    }
                    invKeyPrev[(size_t)key] = down ? 1 : 0;
                };

                handleKey(GLFW_KEY_BACKSPACE, [&]() {
                    if (!invSearch.empty() && invSearchCursor > 0) {
                        invSearch.erase(invSearch.begin() + (invSearchCursor - 1));
                        invSearchCursor = std::max(0, invSearchCursor - 1);
                    }
                });
                handleKey(GLFW_KEY_LEFT, [&]() {
                    invSearchCursor = std::max(0, invSearchCursor - 1);
                });
                handleKey(GLFW_KEY_RIGHT, [&]() {
                    invSearchCursor = std::min((int)invSearch.size(), invSearchCursor + 1);
                });
                handleKey(GLFW_KEY_ESCAPE, [&]() { invSearchActive = false; });

                handleChar(GLFW_KEY_SPACE, ' ');
                handleChar(GLFW_KEY_0, '0');
                handleChar(GLFW_KEY_1, '1');
                handleChar(GLFW_KEY_2, '2');
                handleChar(GLFW_KEY_3, '3');
                handleChar(GLFW_KEY_4, '4');
                handleChar(GLFW_KEY_5, '5');
                handleChar(GLFW_KEY_6, '6');
                handleChar(GLFW_KEY_7, '7');
                handleChar(GLFW_KEY_8, '8');
                handleChar(GLFW_KEY_9, '9');
                const bool shiftDown = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
                const bool useUpper = invUppercase || shiftDown;
                handleChar(GLFW_KEY_A, useUpper ? 'A' : 'a');
                handleChar(GLFW_KEY_B, useUpper ? 'B' : 'b');
                handleChar(GLFW_KEY_C, useUpper ? 'C' : 'c');
                handleChar(GLFW_KEY_D, useUpper ? 'D' : 'd');
                handleChar(GLFW_KEY_E, useUpper ? 'E' : 'e');
                handleChar(GLFW_KEY_F, useUpper ? 'F' : 'f');
                handleChar(GLFW_KEY_G, useUpper ? 'G' : 'g');
                handleChar(GLFW_KEY_H, useUpper ? 'H' : 'h');
                handleChar(GLFW_KEY_I, useUpper ? 'I' : 'i');
                handleChar(GLFW_KEY_J, useUpper ? 'J' : 'j');
                handleChar(GLFW_KEY_K, useUpper ? 'K' : 'k');
                handleChar(GLFW_KEY_L, useUpper ? 'L' : 'l');
                handleChar(GLFW_KEY_M, useUpper ? 'M' : 'm');
                handleChar(GLFW_KEY_N, useUpper ? 'N' : 'n');
                handleChar(GLFW_KEY_O, useUpper ? 'O' : 'o');
                handleChar(GLFW_KEY_P, useUpper ? 'P' : 'p');
                handleChar(GLFW_KEY_Q, useUpper ? 'Q' : 'q');
                handleChar(GLFW_KEY_R, useUpper ? 'R' : 'r');
                handleChar(GLFW_KEY_S, useUpper ? 'S' : 's');
                handleChar(GLFW_KEY_T, useUpper ? 'T' : 't');
                handleChar(GLFW_KEY_U, useUpper ? 'U' : 'u');
                handleChar(GLFW_KEY_V, useUpper ? 'V' : 'v');
                handleChar(GLFW_KEY_W, useUpper ? 'W' : 'w');
                handleChar(GLFW_KEY_X, useUpper ? 'X' : 'x');
                handleChar(GLFW_KEY_Y, useUpper ? 'Y' : 'y');
                handleChar(GLFW_KEY_Z, useUpper ? 'Z' : 'z');

                if (activeTab != invLastTab || invSearch != invLastSearch) {
                    invScrollRow = 0;
                    invLastTab = activeTab;
                    invLastSearch = invSearch;
                }

                // Grid area panel
                const float gridX0 = contentX0;
                const float gridX1 = contentX1;
                const float gridHeaderY1 = searchY0 - 0.01f;
                const float gridHeaderY0 = gridHeaderY1 - 0.05f;
                const float gridY0 = panelY0 + 0.46f;
                const float gridY1 = gridHeaderY0 - 0.01f;
                pushQuad(gridX0, gridY0, gridX1, gridY1, 0.07f, 0.09f, 0.13f, 0.95f);
                pushInvFrame(gridX0, gridY0, gridX1, gridY1, 0.004f, 0.12f, 0.16f, 0.24f, 0.9f);

                // Build filtered list (search overrides active tab)
                std::vector<InvItem> filteredItems;
                filteredItems.reserve(64);

                auto toLowerChar = [&](char c) {
                    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                };

                std::string queryLower = invSearch;
                for (char& c : queryLower) c = toLowerChar(c);

                auto matchesQuery = [&](const char* name) {
                    if (queryLower.empty()) return true;
                    std::string nameLower = name ? name : "";
                    for (char& c : nameLower) c = toLowerChar(c);
                    return nameLower.find(queryLower) != std::string::npos;
                };

                for (const auto& cat : categories) {
                    for (const auto& it : cat.items) {
                        if (matchesQuery(it.name)) {
                            filteredItems.push_back(it);
                        }
                    }
                }

                const bool usingSearch = !queryLower.empty();
                const std::vector<InvItem>& viewItems = usingSearch ? filteredItems : categories[activeTab].items;

                // Section header above grid
                pushQuad(gridX0, gridHeaderY0, gridX1, gridHeaderY1, 0.08f, 0.10f, 0.16f, 0.98f);
                pushInvFrame(gridX0, gridHeaderY0, gridX1, gridHeaderY1, 0.003f, 0.12f, 0.18f, 0.28f, 0.9f);
                std::string headerLeft = usingSearch
                    ? std::string("SEARCH RESULTS")
                    : (std::string(categories[activeTab].name) + " — " + categories[activeTab].desc);
                pushTextInvLeft(gridX0 + 0.01f, 0.5f * (gridHeaderY0 + gridHeaderY1), headerLeft.c_str(), 1.2f, true);
                std::string headerRight = std::string("ITEMS ") + std::to_string(viewItems.size());
                pushTextInvLeft(gridX1 - 0.16f, 0.5f * (gridHeaderY0 + gridHeaderY1), headerRight.c_str(), 1.1f, false);

                // Minecraft import page controls (clickable)
                const int mcPageMax = GetMinecraftImportPageMax();
                const float mcBtnW = 0.05f;
                const float mcBtnPad = 0.008f;
                const float mcRightX1 = gridX1 - 0.01f;
                const float mcNextX0 = mcRightX1 - mcBtnW;
                const float mcNextX1 = mcRightX1;
                const float mcPrevX1 = mcNextX0 - mcBtnPad;
                const float mcPrevX0 = mcPrevX1 - mcBtnW;
                const float mcBtnY0 = gridHeaderY0 + 0.006f;
                const float mcBtnY1 = gridHeaderY1 - 0.006f;
                const bool mcPrevHover = (mx >= mcPrevX0 && mx <= mcPrevX1 && my >= mcBtnY0 && my <= mcBtnY1);
                const bool mcNextHover = (mx >= mcNextX0 && mx <= mcNextX1 && my >= mcBtnY0 && my <= mcBtnY1);

                const float mpr = mcPrevHover ? 0.20f : 0.14f;
                const float mnr = mcNextHover ? 0.20f : 0.14f;
                pushQuad(mcPrevX0, mcBtnY0, mcPrevX1, mcBtnY1, mpr, mpr, mpr, 0.98f);
                pushInvFrame(mcPrevX0, mcBtnY0, mcPrevX1, mcBtnY1, 0.0025f, 0.10f, 0.14f, 0.20f, 0.9f);
                pushTextInv(0.5f * (mcPrevX0 + mcPrevX1), 0.5f * (mcBtnY0 + mcBtnY1), "<", 1.3f, true);
                pushQuad(mcNextX0, mcBtnY0, mcNextX1, mcBtnY1, mnr, mnr, mnr, 0.98f);
                pushInvFrame(mcNextX0, mcBtnY0, mcNextX1, mcBtnY1, 0.0025f, 0.10f, 0.14f, 0.20f, 0.9f);
                pushTextInv(0.5f * (mcNextX0 + mcNextX1), 0.5f * (mcBtnY0 + mcBtnY1), ">", 1.3f, true);

                std::string mcPageText = std::string("MC ") + std::to_string(g_MinecraftImportPage) + "/" + std::to_string(mcPageMax);
                pushTextInvLeft(mcPrevX0 - 0.19f, 0.5f * (gridHeaderY0 + gridHeaderY1), mcPageText.c_str(), 1.0f, false);

                if (clickInv) {
                    int pageDelta = 0;
                    if (mcPrevHover) pageDelta = -1;
                    if (mcNextHover) pageDelta = +1;
                    if (pageDelta != 0) {
                        const int oldPage = g_MinecraftImportPage;
                        g_MinecraftImportPage = ClampMinecraftImportPage(g_MinecraftImportPage + pageDelta);
                        if (g_MinecraftImportPage != oldPage) {
                            runtimeMaxBlockId = GetRuntimeMaxBlockId();
                            selectedBlockId = (uint8_t)std::clamp((int)selectedBlockId, 1, runtimeMaxBlockId);
                            patternBlockId = selectedBlockId;
                            ini["import_page"] = std::to_string(g_MinecraftImportPage);
                            SaveIniKeyValue(settingsPath, ini);
                        }
                    }
                }

                // Grid for active tab
                int hoveredId = 0;
                const char* hoveredName = nullptr;
                const int cols = 9;
                const float pad = 0.02f;
                const float gridW = (gridX1 - gridX0);
                const float gridH = (gridY1 - gridY0);
                const float size = std::min((gridW - (cols + 1) * pad) / cols, 0.12f);
                const int rowsVisible = std::max(1, (int)std::floor((gridH - pad) / (size + pad)));
                const int totalRows = (int)((viewItems.size() + cols - 1) / cols);
                if (totalRows > 0) {
                    std::string pageInfo = std::string("ROW ") + std::to_string(invScrollRow + 1) + "/" + std::to_string(totalRows);
                    pushTextInvLeft(gridX1 - 0.36f, 0.5f * (gridHeaderY0 + gridHeaderY1), pageInfo.c_str(), 1.0f, false);
                }

                const float scrollY = Input::GetScrollDeltaY();
                const double dt = (invLastTime > 0.0) ? std::clamp(nowTime - invLastTime, 0.0, 0.05) : 0.016;
                invLastTime = nowTime;

                if (scrollY != 0.0f) {
                    invScrollVelocity += (scrollY > 0.0f ? -1.0f : 1.0f) * 6.0f;
                }

                if (!invDraggingScroll) {
                    invScrollAccum += invScrollVelocity * (float)dt;
                    const float damping = std::exp(-8.0f * (float)dt);
                    invScrollVelocity *= damping;
                } else {
                    invScrollVelocity = 0.0f;
                    invScrollAccum = 0.0f;
                }

                while (invScrollAccum >= 1.0f) {
                    invScrollRow += 1;
                    invScrollAccum -= 1.0f;
                }
                while (invScrollAccum <= -1.0f) {
                    invScrollRow -= 1;
                    invScrollAccum += 1.0f;
                }

                const int maxScroll = std::max(0, totalRows - rowsVisible);
                invScrollRow = std::clamp(invScrollRow, 0, maxScroll);
                if (invScrollRow == 0 || invScrollRow == maxScroll) {
                    invScrollVelocity = 0.0f;
                    invScrollAccum = 0.0f;
                }

                // Keyboard navigation for grid
                if (!invSearchActive) {
                    if (viewItems.empty()) {
                        invGridSel = 0;
                    } else {
                        invGridSel = std::clamp(invGridSel, 0, (int)viewItems.size() - 1);
                    }

                    auto navKey = [&](int key, auto onPress) {
                        const bool down = Input::IsKeyPressed(key);
                        const bool prev = invNavPrev[(size_t)key] != 0;
                        if (down && !prev) {
                            onPress();
                            invKeyNextRepeat[(size_t)key] = nowTime + 0.35;
                        } else if (down && prev) {
                            if (nowTime >= invKeyNextRepeat[(size_t)key]) {
                                onPress();
                                invKeyNextRepeat[(size_t)key] = nowTime + 0.05;
                            }
                        } else if (!down) {
                            invKeyNextRepeat[(size_t)key] = 0.0;
                        }
                        invNavPrev[(size_t)key] = down ? 1 : 0;
                    };

                    navKey(GLFW_KEY_LEFT, [&]() { invGridSel = std::max(0, invGridSel - 1); });
                    navKey(GLFW_KEY_RIGHT, [&]() { invGridSel = std::min((int)viewItems.size() - 1, invGridSel + 1); });
                    navKey(GLFW_KEY_UP, [&]() { invGridSel = std::max(0, invGridSel - cols); });
                    navKey(GLFW_KEY_DOWN, [&]() { invGridSel = std::min((int)viewItems.size() - 1, invGridSel + cols); });
                    navKey(GLFW_KEY_ENTER, [&]() {
                        if (!viewItems.empty()) {
                            selectedBlockId = (uint8_t)viewItems[(size_t)invGridSel].id;
                            setHotbarStack(hotbarIndex, selectedBlockId, kItemStackMaxCount);
                            Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                        }
                    });

                    const int selRow = (cols > 0) ? (invGridSel / cols) : 0;
                    if (selRow < invScrollRow) invScrollRow = selRow;
                    if (selRow >= invScrollRow + rowsVisible) invScrollRow = selRow - rowsVisible + 1;
                    invScrollRow = std::clamp(invScrollRow, 0, maxScroll);
                }

                const int startIndex = invScrollRow * cols;
                const int endIndex = std::min((int)viewItems.size(), startIndex + rowsVisible * cols);

                const float startX = gridX0 + pad;
                const float startY = gridY1 - pad;

                for (int idx = startIndex; idx < endIndex; ++idx) {
                    const int local = idx - startIndex;
                    const int cx = local % cols;
                    const int cy = local / cols;
                    const InvItem& item = viewItems[(size_t)idx];
                    const int id = item.id;

                    const float x0 = startX + cx * (size + pad);
                    const float y1 = startY - cy * (size + pad);
                    const float x1 = x0 + size;
                    const float y0 = y1 - size;

                    const bool inside = (mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
                    const bool navFocus = (idx == invGridSel);
                    if (inside || navFocus) {
                        hoveredId = id;
                        hoveredName = item.name;
                    }

                    float r = 0.2f, g = 0.2f, b = 0.2f;
                    colorForBlock(id, r, g, b);

                    const bool selected = (id == (int)selectedBlockId);
                    float bg = inside ? 0.20f : 0.12f;
                    if (navFocus) bg = 0.22f;
                    if (selected) bg = 0.30f;
                    pushQuad(x0, y0, x1, y1, bg, bg, bg, 0.96f);
                    pushInvFrame(x0, y0, x1, y1, 0.003f, 0.08f, 0.10f, 0.14f, 0.9f);
                    if (inside && !selected) {
                        pushInvFrame(x0 - 0.002f, y0 - 0.002f, x1 + 0.002f, y1 + 0.002f, 0.002f, 0.18f, 0.24f, 0.34f, 0.7f);
                    }

                    const float inset = 0.010f;
                    float ir = 1.0f, ig = 1.0f, ib = 1.0f;
                    if (inside) {
                        ir = std::min(1.0f, ir + 0.06f);
                        ig = std::min(1.0f, ig + 0.06f);
                        ib = std::min(1.0f, ib + 0.06f);
                    }
                    if (selected) {
                        ir = std::min(1.0f, ir + 0.10f);
                        ig = std::min(1.0f, ig + 0.10f);
                        ib = std::min(1.0f, ib + 0.10f);
                        const float pr = 0.20f + 0.20f * pulse;
                        const float pg = 0.35f + 0.25f * pulse;
                        const float pb = 0.60f + 0.30f * pulse;
                        pushInvFrame(x0 - 0.004f, y0 - 0.004f, x1 + 0.004f, y1 + 0.004f, 0.004f, pr, pg, pb, 0.98f);
                    }

                    const int tilesX = kAtlasTilesX;
                    const int tilesY = kAtlasTilesY;
                    const int tileIndex = id - 1;
                    const int tileX = tileIndex % tilesX;
                    const int tileY = tileIndex / tilesX;
                    const float du = 1.0f / (float)tilesX;
                    const float dv = 1.0f / (float)tilesY;
                    const float padUv = 0.01f * du;
                    const float u0 = tileX * du + padUv;
                    const float v0 = tileY * dv + padUv;
                    const float u1 = (tileX + 1) * du - padUv;
                    const float v1 = (tileY + 1) * dv - padUv;

                    pushQuadTex(x0 + inset, y0 + inset, x1 - inset, y1 - inset, u0, v0, u1, v1, ir, ig, ib, 1.0f);

                    if (clickInv && inside) {
                        invGridSel = idx;
                        mouseCursorStack = ItemStack{ (uint16_t)id, kItemStackMaxCount };
                        invPaletteDragActive = true;
                    }
                    if (rightClickInv && inside) {
                        invGridSel = idx;
                        mouseCursorStack = ItemStack{ (uint16_t)id, 1 };
                    }
                }

                if (viewItems.empty()) {
                    pushTextInv(0.0f, 0.5f * (gridY0 + gridY1), "No results", 1.8f, false);
                }

                if (!invSearchActive) {
                    auto assignSlot = [&](int key, int slot) {
                        const bool down = Input::IsKeyPressed(key);
                        const bool prev = invHotbarKeyPrev[(size_t)key] != 0;
                        if (hoveredId > 0 && down && !prev) {
                            setHotbarStack(slot, (uint16_t)hoveredId, kItemStackMaxCount);
                            hotbarIndex = slot;
                            selectedBlockId = (uint8_t)hoveredId;
                            Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                        }
                        invHotbarKeyPrev[(size_t)key] = down ? 1 : 0;
                    };

                    assignSlot(GLFW_KEY_1, 0);
                    assignSlot(GLFW_KEY_2, 1);
                    assignSlot(GLFW_KEY_3, 2);
                    assignSlot(GLFW_KEY_4, 3);
                    assignSlot(GLFW_KEY_5, 4);
                    assignSlot(GLFW_KEY_6, 5);
                    assignSlot(GLFW_KEY_7, 6);
                    assignSlot(GLFW_KEY_8, 7);
                    assignSlot(GLFW_KEY_9, 8);
                    assignSlot(GLFW_KEY_0, 9);
                    assignSlot(GLFW_KEY_MINUS, 10);
                    assignSlot(GLFW_KEY_EQUAL, 11);
                }

                // Scrollbar
                const float sbW = 0.015f;
                const float sbX1 = gridX1 - 0.01f;
                const float sbX0 = sbX1 - sbW;
                const float sbY0 = gridY0 + 0.01f;
                const float sbY1 = gridY1 - 0.01f;
                pushQuad(sbX0, sbY0, sbX1, sbY1, 0.08f, 0.10f, 0.14f, 0.95f);

                if (totalRows > 0) {
                    const float t = (totalRows > rowsVisible) ? (float)invScrollRow / (float)maxScroll : 0.0f;
                    const float thumbH = std::max(0.05f, (float)rowsVisible / std::max(1, totalRows)) * (sbY1 - sbY0);
                    const float thumbY0 = sbY0 + t * ((sbY1 - sbY0) - thumbH);
                    const float thumbY1 = thumbY0 + thumbH;
                    const bool thumbHover = (mx >= sbX0 && mx <= sbX1 && my >= thumbY0 && my <= thumbY1);
                    const float tr = invDraggingScroll ? 0.28f : (thumbHover ? 0.22f : 0.18f);
                    const float tg = invDraggingScroll ? 0.30f : (thumbHover ? 0.24f : 0.20f);
                    const float tb = invDraggingScroll ? 0.36f : (thumbHover ? 0.30f : 0.26f);
                    pushQuad(sbX0 + 0.001f, thumbY0, sbX1 - 0.001f, thumbY1, tr, tg, tb, 0.98f);
                    if (clickInv && thumbHover) {
                        invDraggingScroll = true;
                        invDragGrabOffset = my - thumbY0;
                    }
                    if (!lmbDown) {
                        invDraggingScroll = false;
                    }
                    if (invDraggingScroll && maxScroll > 0) {
                        float newThumbY0 = my - invDragGrabOffset;
                        newThumbY0 = std::clamp(newThumbY0, sbY0, sbY1 - thumbH);
                        const float newT = (newThumbY0 - sbY0) / ((sbY1 - sbY0) - thumbH);
                        invScrollRow = std::clamp((int)std::round(newT * (float)maxScroll), 0, maxScroll);
                    }
                }

                // Page buttons
                const float pgW = 0.05f;
                const float pgH = 0.06f;
                const float pgY0 = gridY0 - 0.08f;
                const float pgY1 = pgY0 + pgH;
                const float pgLeftX0 = gridX0 + 0.02f;
                const float pgLeftX1 = pgLeftX0 + pgW;
                const float pgRightX1 = gridX1 - 0.02f;
                const float pgRightX0 = pgRightX1 - pgW;
                const bool pgLeftHover = (mx >= pgLeftX0 && mx <= pgLeftX1 && my >= pgY0 && my <= pgY1);
                const bool pgRightHover = (mx >= pgRightX0 && mx <= pgRightX1 && my >= pgY0 && my <= pgY1);
                const float pgl = pgLeftHover ? 0.18f : 0.12f;
                const float pgr = pgRightHover ? 0.18f : 0.12f;
                pushQuad(pgLeftX0, pgY0, pgLeftX1, pgY1, pgl, pgl, pgl, 0.98f);
                pushInvFrame(pgLeftX0, pgY0, pgLeftX1, pgY1, 0.003f, 0.10f, 0.14f, 0.20f, 0.9f);
                pushTextInv(0.5f * (pgLeftX0 + pgLeftX1), 0.5f * (pgY0 + pgY1), "<", 1.6f, true);
                pushQuad(pgRightX0, pgY0, pgRightX1, pgY1, pgr, pgr, pgr, 0.98f);
                pushInvFrame(pgRightX0, pgY0, pgRightX1, pgY1, 0.003f, 0.10f, 0.14f, 0.20f, 0.9f);
                pushTextInv(0.5f * (pgRightX0 + pgRightX1), 0.5f * (pgY0 + pgY1), ">", 1.6f, true);

                const int tabCount = (int)categories.size();
                if (clickInv && pgLeftHover && tabCount > 0) {
                    activeTab = (activeTab - 1 + tabCount) % tabCount;
                    invGridSel = 0;
                    invScrollRow = 0;
                }
                if (clickInv && pgRightHover && tabCount > 0) {
                    activeTab = (activeTab + 1) % tabCount;
                    invGridSel = 0;
                    invScrollRow = 0;
                }

                const float hotbarX0 = panelX0 + 0.02f;
                const float hotbarX1 = panelX1 - 0.02f;
                const float hotbarY0 = panelY0 + 0.03f;
                const float hotbarY1 = hotbarY0 + 0.10f;
                const float hotbarW = hotbarX1 - hotbarX0;

                const int invMainCols = 9;
                const int invMainRows = 3;
                const float invSlotPad = 0.006f;
                const float invMainTop = pgY0 - 0.018f;
                const float invMainBottom = hotbarY1 + 0.020f;
                const float invSlotSizeX = (hotbarW - invSlotPad * (float)(invMainCols + 1)) / (float)invMainCols;
                const float invSlotSizeY = (invMainTop - invMainBottom - invSlotPad * (float)(invMainRows + 1)) / (float)invMainRows;
                const float invSlotSize = std::clamp(std::min(invSlotSizeX, invSlotSizeY), 0.045f, 0.075f);
                const float invSlotsW = invSlotPad * (float)(invMainCols + 1) + invSlotSize * (float)invMainCols;
                const float invSlotsH = invSlotPad * (float)(invMainRows + 1) + invSlotSize * (float)invMainRows;
                const float invSlotsX0 = 0.5f * (hotbarX0 + hotbarX1) - 0.5f * invSlotsW;
                const float invSlotsY0 = invMainBottom;
                const float invSlotsX1 = invSlotsX0 + invSlotsW;
                const float invSlotsY1 = invSlotsY0 + invSlotsH;
                pushQuad(invSlotsX0 - 0.008f, invSlotsY0 - 0.008f, invSlotsX1 + 0.008f, invSlotsY1 + 0.008f, 0.10f, 0.11f, 0.14f, 0.98f);
                pushInvFrame(invSlotsX0 - 0.008f, invSlotsY0 - 0.008f, invSlotsX1 + 0.008f, invSlotsY1 + 0.008f, 0.003f, 0.18f, 0.22f, 0.30f, 0.92f);

                int mainInvHoverIndex = -1;
                for (int r = 0; r < invMainRows; ++r) {
                    for (int c = 0; c < invMainCols; ++c) {
                        const int idx = r * invMainCols + c;
                        const ItemStack& stack = mainInventorySlots[(size_t)idx];
                        const float sx0 = invSlotsX0 + invSlotPad + c * (invSlotSize + invSlotPad);
                        const float sy1 = invSlotsY1 - invSlotPad - r * (invSlotSize + invSlotPad);
                        const float sx1 = sx0 + invSlotSize;
                        const float sy0 = sy1 - invSlotSize;
                        const bool inside = (mx >= sx0 && mx <= sx1 && my >= sy0 && my <= sy1);
                        if (inside) mainInvHoverIndex = idx;

                        const float bg = inside ? 0.25f : 0.16f;
                        pushQuad(sx0, sy0, sx1, sy1, bg, bg, bg, 0.98f);
                        pushInvFrame(sx0, sy0, sx1, sy1, 0.0025f, 0.30f, 0.30f, 0.32f, 0.92f);

                        if (!IsStackEmpty(stack)) {
                            const int id = (int)stack.id;
                            const int tileIndex = id - 1;
                            const int tileX = tileIndex % kAtlasTilesX;
                            const int tileY = tileIndex / kAtlasTilesX;
                            const float du = 1.0f / (float)kAtlasTilesX;
                            const float dv = 1.0f / (float)kAtlasTilesY;
                            const float padUv = 0.01f * du;
                            const float u0 = tileX * du + padUv;
                            const float v0 = tileY * dv + padUv;
                            const float u1 = (tileX + 1) * du - padUv;
                            const float v1 = (tileY + 1) * dv - padUv;
                            pushQuadTex(sx0 + 0.004f, sy0 + 0.004f, sx1 - 0.004f, sy1 - 0.004f, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);

                            const std::string countText = std::to_string((int)stack.count);
                            pushTextInvLeft(sx1 - 0.020f, sy0 + 0.010f, countText.c_str(), 0.82f, false);
                        }

                        if (inside && clickInv) {
                            HandleInventoryClick(mainInventorySlots, idx, GLFW_MOUSE_BUTTON_LEFT);
                        }
                        if (inside && rightClickInv) {
                            HandleInventoryClick(mainInventorySlots, idx, GLFW_MOUSE_BUTTON_RIGHT);
                        }
                    }
                }

                pushQuad(hotbarX0, hotbarY0, hotbarX1, hotbarY1, 0.08f, 0.10f, 0.16f, 0.98f);
                pushInvFrame(hotbarX0, hotbarY0, hotbarX1, hotbarY1, 0.003f, 0.12f, 0.18f, 0.28f, 0.9f);
                pushTextInvLeft(hotbarX0 + 0.01f, hotbarY1 + 0.012f, "HOTBAR (1-9, 0, -, =)", 1.1f, false);

                const char* selectedName = findItemName((int)selectedBlockId);
                const char* hoverName = hoveredName ? hoveredName : selectedName;
                if (hoverName || selectedName) {
                    const float infoY0 = hotbarY1 + 0.01f;
                    const float infoY1 = infoY0 + 0.05f;
                    pushQuad(hotbarX0, infoY0, hotbarX1, infoY1, 0.07f, 0.09f, 0.14f, 0.98f);
                    pushInvFrame(hotbarX0, infoY0, hotbarX1, infoY1, 0.003f, 0.12f, 0.16f, 0.24f, 0.9f);
                    if (hoverName) {
                        pushTextInvLeft(hotbarX0 + 0.01f, 0.5f * (infoY0 + infoY1), "HOVER:", 1.05f, false);
                        pushTextInvLeft(hotbarX0 + 0.12f, 0.5f * (infoY0 + infoY1), hoverName, 1.15f, true);
                    }
                    if (selectedName) {
                        pushTextInvLeft(hotbarX0 + 0.52f, 0.5f * (infoY0 + infoY1), "SELECTED:", 1.05f, false);
                        pushTextInvLeft(hotbarX0 + 0.66f, 0.5f * (infoY0 + infoY1), selectedName, 1.15f, true);
                    }
                }

                const int numHotbar = 12;
                const float slotPad = 0.01f;
                const float slotSize = (hotbarW - slotPad * (float)(numHotbar + 1)) / (float)numHotbar;
                int hotbarHoverIndex = -1;
                const std::array<const char*, 12> hotbarKeyLabels = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=" };
                for (int i = 0; i < numHotbar; ++i) {
                    const float sx0 = hotbarX0 + slotPad + i * (slotSize + slotPad);
                    const float sx1 = sx0 + slotSize;
                    const float sy0 = hotbarY0 + 0.01f;
                    const float sy1 = sy0 + slotSize;
                    const int id = (int)hotbarSlots[(size_t)i].id;
                    const bool selected = (i == hotbarIndex);
                    const bool hotInside = (mx >= sx0 && mx <= sx1 && my >= sy0 && my <= sy1);
                    const float bg = selected ? 0.28f : (hotInside ? 0.18f : 0.14f);
                    pushQuad(sx0, sy0, sx1, sy1, bg, bg, bg, 0.96f);
                    pushInvFrame(sx0, sy0, sx1, sy1, 0.003f, 0.10f, 0.12f, 0.16f, 0.9f);
                    if (selected) {
                        pushInvFrame(sx0 - 0.002f, sy0 - 0.002f, sx1 + 0.002f, sy1 + 0.002f, 0.003f, 0.28f, 0.50f, 0.82f, 0.98f);
                    } else if (hotInside) {
                        pushInvFrame(sx0 - 0.0015f, sy0 - 0.0015f, sx1 + 0.0015f, sy1 + 0.0015f, 0.002f, 0.20f, 0.32f, 0.50f, 0.88f);
                    }

                    const float keyY0 = sy0 + 0.002f;
                    const float keyY1 = keyY0 + 0.017f;
                    const float keyX0 = sx0 + 0.003f;
                    const float keyX1 = keyX0 + 0.017f;
                    pushQuad(keyX0, keyY0, keyX1, keyY1, selected ? 0.22f : 0.14f, selected ? 0.30f : 0.18f, selected ? 0.45f : 0.24f, 0.94f);
                    pushInvFrame(keyX0, keyY0, keyX1, keyY1, 0.0015f, 0.30f, 0.44f, 0.64f, 0.85f);
                    pushTextInv(0.5f * (keyX0 + keyX1), 0.5f * (keyY0 + keyY1), hotbarKeyLabels[(size_t)i], 0.80f, selected);
                    if (!IsStackEmpty(hotbarSlots[(size_t)i])) {
                        const std::string countText = std::to_string((int)hotbarSlots[(size_t)i].count);
                        pushTextInvLeft(sx1 - 0.025f, sy0 + 0.012f, countText.c_str(), 0.85f, false);
                    }

                    if (id > 0) {
                        const int tilesX = kAtlasTilesX;
                        const int tilesY = kAtlasTilesY;
                        const int tileIndex = id - 1;
                        const int tileX = tileIndex % tilesX;
                        const int tileY = tileIndex / tilesX;
                        const float du = 1.0f / (float)tilesX;
                        const float dv = 1.0f / (float)tilesY;
                        const float padUv = 0.01f * du;
                        const float u0 = tileX * du + padUv;
                        const float v0 = tileY * dv + padUv;
                        const float u1 = (tileX + 1) * du - padUv;
                        const float v1 = (tileY + 1) * dv - padUv;
                        pushQuadTex(sx0 + 0.006f, sy0 + 0.006f, sx1 - 0.006f, sy1 - 0.006f, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    if (hotInside) hotbarHoverIndex = i;
                    if (clickInv && hotInside) {
                        const uint8_t prevSelected = selectedBlockId;
                        HandleInventoryClick(hotbarSlots, i, GLFW_MOUSE_BUTTON_LEFT);
                        hotbarIndex = i;
                        selectedBlockId = getHotbarBlockId(i);
                        if (selectedBlockId != prevSelected) {
                            Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                        }
                    }
                    if (rightClickInv && hotInside) {
                        const uint8_t prevSelected = selectedBlockId;
                        HandleInventoryClick(hotbarSlots, i, GLFW_MOUSE_BUTTON_RIGHT);
                        hotbarIndex = i;
                        selectedBlockId = getHotbarBlockId(i);
                        if (selectedBlockId != prevSelected) {
                            Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                        }
                    }
                }

                if (releaseInv && invPaletteDragActive && !IsStackEmpty(mouseCursorStack)) {
                    if (hotbarHoverIndex >= 0) {
                        setHotbarStack(hotbarHoverIndex, mouseCursorStack.id, mouseCursorStack.count);
                        hotbarIndex = hotbarHoverIndex;
                        selectedBlockId = getHotbarBlockId(hotbarIndex);
                        Telemetry::RecordSelectBlock(glfwGetTime(), selectedBlockId);
                        mouseCursorStack = ItemStack{};
                    } else if (mainInvHoverIndex >= 0) {
                        mainInventorySlots[(size_t)mainInvHoverIndex] = mouseCursorStack;
                        NormalizeStack(mainInventorySlots[(size_t)mainInvHoverIndex]);
                        mouseCursorStack = ItemStack{};
                    }
                    invPaletteDragActive = false;
                }

                // Keep lower area clean for slot visibility and drag/drop precision.

                // Tooltip card
                if (hoveredName) {
                    const float tipW = 0.60f;
                    const float tipH = 0.08f;
                    const float tipX0 = -0.5f * tipW;
                    const float tipX1 = 0.5f * tipW;
                    const float tipY0 = panelY0 + 0.06f;
                    const float tipY1 = tipY0 + tipH;
                    pushQuad(tipX0 - 0.006f, tipY0 - 0.006f, tipX1 + 0.006f, tipY1 + 0.006f, 0.14f, 0.18f, 0.24f, 0.95f);
                    pushQuad(tipX0, tipY0, tipX1, tipY1, 0.06f, 0.08f, 0.12f, 0.98f);
                    pushTextInv(0.0f, 0.5f * (tipY0 + tipY1), hoveredName, 1.7f, true);
                    std::string tipMeta = std::string("ID ") + std::to_string(hoveredId) + "  |  SLOT " + std::to_string(hotbarIndex + 1);
                    pushTextInvLeft(tipX0 + 0.02f, tipY0 + 0.017f, tipMeta.c_str(), 0.95f, false);
                }

                if (!IsStackEmpty(mouseCursorStack)) {
                    const float ghostSize = 0.05f;
                    const float gx0 = mx - ghostSize * 0.5f;
                    const float gx1 = mx + ghostSize * 0.5f;
                    const float gy0 = my - ghostSize * 0.5f;
                    const float gy1 = my + ghostSize * 0.5f;

                    const int tilesX = kAtlasTilesX;
                    const int tilesY = kAtlasTilesY;
                    const int tileIndex = (int)mouseCursorStack.id - 1;
                    const int tileX = tileIndex % tilesX;
                    const int tileY = tileIndex / tilesX;
                    const float du = 1.0f / (float)tilesX;
                    const float dv = 1.0f / (float)tilesY;
                    const float padUv = 0.01f * du;
                    const float u0 = tileX * du + padUv;
                    const float v0 = tileY * dv + padUv;
                    const float u1 = (tileX + 1) * du - padUv;
                    const float v1 = (tileY + 1) * dv - padUv;
                    pushQuadTex(gx0, gy0, gx1, gy1, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 0.85f);

                    const char* dragName = findItemName((int)mouseCursorStack.id);
                    if (dragName) pushTextInvLeft(gx1 + 0.02f, gy1, dragName, 1.1f, true);
                    const std::string dragCount = std::to_string((int)mouseCursorStack.count);
                    pushTextInvLeft(gx1 + 0.02f, gy1 - 0.03f, dragCount.c_str(), 1.0f, false);
                }

                lastInvLmb = lmbDown;
                lastInvRmb = rmbDown;

                verts.insert(verts.end(), textVerts.begin(), textVerts.end());

                glUseProgram(invProg);
                applyUiProgramUniforms(invProg);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, blockTexAtlas);
                glUniform1i(glGetUniformLocation(invProg, "u_BlockAtlas"), 0);
                glBindVertexArray(invVao);
                glBindBuffer(GL_ARRAY_BUFFER, invVbo);
                if (verts.size() > invVboCapacityFloats) {
                    invVboCapacityFloats = verts.size() * 2;
                    glBufferData(GL_ARRAY_BUFFER, invVboCapacityFloats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 9));
                glBindVertexArray(0);
                glUseProgram(0);

                } // End of disabled old code

                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            } else {
                // Keep click edge state in sync when inventory is closed.
                lastInvLmb = lmbDown;
                lastInvRmb = rmbDown;
            }

            // Aim outline + placement preview (wireframe cube)
            if (aimOutlineEnabled && aimProg != 0 && aimVao != 0 && aimHit.hit) {
                const glm::mat4 vp = projection * camera.GetViewMatrix();

                glUseProgram(aimProg);
                if (aimLocVP >= 0) glUniformMatrix4fv(aimLocVP, 1, GL_FALSE, &vp[0][0]);
                GLint aimTimeLoc = glGetUniformLocation(aimProg, "u_Time");
                if (aimTimeLoc >= 0) glUniform1f(aimTimeLoc, (float)glfwGetTime());

                glBindVertexArray(aimVao);
                
                // Enable Blending for glow
                GLboolean wasBlend; glGetBooleanv(GL_BLEND, &wasBlend);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                
                // Allow partial depth (so it wraps smoothly around the block without being blocked totally)
                glDepthFunc(GL_LEQUAL);

                // Target block (Sci-Fi Cyan-Blue instead of flat Red for better modern aesthetics)
                if (aimLocColor >= 0) glUniform3f(aimLocColor, 0.2f, 0.8f, 1.0f);
                if (aimLocPos >= 0) {
                    const glm::vec3 p = glm::vec3(aimHit.blockWorld);
                    glUniform3f(aimLocPos, p.x, p.y, p.z);
                }
                glDrawArrays(GL_TRIANGLES, 0, 36);

                // Placement preview (green) only if empty
                if (chunkManager.GetBlockWorld(aimHit.prevWorld) == 0) {
                    if (aimLocColor >= 0) glUniform3f(aimLocColor, 0.25f, 1.0f, 0.25f);
                    if (aimLocPos >= 0) {
                        const glm::vec3 p = glm::vec3(aimHit.prevWorld);
                        glUniform3f(aimLocPos, p.x, p.y, p.z);
                    }
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }

                // Precision anchor (magenta)
                if (precisionBuild && precisionAnchor) {
                    if (aimLocColor >= 0) glUniform3f(aimLocColor, 1.0f, 0.2f, 0.8f);
                    if (aimLocPos >= 0) {
                        const glm::vec3 p = glm::vec3(precisionAnchorPos);
                        glUniform3f(aimLocPos, p.x, p.y, p.z);
                    }
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }

                if (!wasBlend) glDisable(GL_BLEND);
                glDepthFunc(GL_LESS);

                glBindVertexArray(0);
                glUseProgram(0);
            }

            // Placement face highlight (white face on target block).
            if (placeFaceEnabled && faceProg != 0 && faceVao != 0 && aimHit.hit) {
                if (chunkManager.GetBlockWorld(placeHit.prevWorld) == 0) {
                    const bool faceTargetChanged = (!faceCacheValid) || (placeHit.blockWorld != lastFaceTargetBlock) || (placeHit.prevWorld != lastFaceTargetPrev);
                    if (faceTargetChanged) {
                        const glm::ivec3 normal = snapNormal(placeHit.prevWorld - placeHit.blockWorld);
                        glm::vec3 u(1.0f, 0.0f, 0.0f);
                        glm::vec3 v(0.0f, 1.0f, 0.0f);
                        if (normal.x != 0) {
                            u = glm::vec3(0.0f, 1.0f, 0.0f);
                            v = glm::vec3(0.0f, 0.0f, 1.0f);
                        } else if (normal.y != 0) {
                            u = glm::vec3(1.0f, 0.0f, 0.0f);
                            v = glm::vec3(0.0f, 0.0f, 1.0f);
                        } else {
                            u = glm::vec3(1.0f, 0.0f, 0.0f);
                            v = glm::vec3(0.0f, 1.0f, 0.0f);
                        }

                        const glm::vec3 center = glm::vec3(placeHit.blockWorld) + glm::vec3(0.5f) + glm::vec3(normal) * 0.5008f;
                        const float s = 0.495f;
                        const glm::vec3 p0 = center - u * s - v * s;
                        const glm::vec3 p1 = center + u * s - v * s;
                        const glm::vec3 p2 = center + u * s + v * s;
                        const glm::vec3 p3 = center - u * s + v * s;
                        const float verts[] = {
                            p0.x, p0.y, p0.z,
                            p1.x, p1.y, p1.z,
                            p2.x, p2.y, p2.z,
                            p0.x, p0.y, p0.z,
                            p2.x, p2.y, p2.z,
                            p3.x, p3.y, p3.z
                        };

                        glBindBuffer(GL_ARRAY_BUFFER, faceVbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                        lastFaceTargetBlock = placeHit.blockWorld;
                        lastFaceTargetPrev = placeHit.prevWorld;
                        faceCacheValid = true;
                    }

                    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                    if (!hadBlend) {
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    }

                    glUseProgram(faceProg);
                    if (faceLocVP >= 0) {
                        const glm::mat4 vp = projection * camera.GetViewMatrix();
                        glUniformMatrix4fv(faceLocVP, 1, GL_FALSE, &vp[0][0]);
                    }
                    if (faceLocColor >= 0) glUniform3f(faceLocColor, 0.58f, 0.90f, 1.00f);
                    const GLint faceLocAlpha = glGetUniformLocation(faceProg, "u_Alpha");
                    if (faceLocAlpha >= 0) glUniform1f(faceLocAlpha, placeFaceAlpha);
                    glBindVertexArray(faceVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    glUseProgram(0);

                    if (!hadBlend) glDisable(GL_BLEND);
                }
            } else {
                faceCacheValid = false;
            }

            // Crosshair overlay
            if (!inventoryOpen && !menuVisible && crosshairEnabled && crossProg != 0 && crossVao != 0) {
                const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
                const GLboolean hadBlend = glIsEnabled(GL_BLEND);
                if (hadDepth) glDisable(GL_DEPTH_TEST);
                if (!hadBlend) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
                glUseProgram(crossProg);
                glBindVertexArray(crossVao);
                
                GLint crossAspectLoc = glGetUniformLocation(crossProg, "u_Aspect");
                if (crossAspectLoc >= 0) glUniform1f(crossAspectLoc, (float)Window::GetWidth() / (float)Window::GetHeight());
                
                GLint crossTimeLoc = glGetUniformLocation(crossProg, "u_Time");
                if (crossTimeLoc >= 0) glUniform1f(crossTimeLoc, (float)glfwGetTime());

                glDrawArrays(GL_TRIANGLES, 0, 6);
                
                glBindVertexArray(0);
                glUseProgram(0);
                if (!hadBlend) glDisable(GL_BLEND);
                if (hadDepth) glEnable(GL_DEPTH_TEST);
            }

            // One-time dump so we can verify rendering without manual inspection.
            if (dumpFrameEnabled) {
                DumpFrameBufferPPMOnce(Window::GetWidth(), Window::GetHeight());
            }

            lastMenuLmb = lmbDown;
            Window::SwapBuffers();
        }

        if (bindlessOk && blockTexHandle != 0) {
            Engine::RendererUtil::BindlessTextures::MakeTextureHandleNonResident(blockTexHandle);
        }
        Game::World::Generation::ClearErosionHeightDeltaPatch();
        if (erosionHeightTexA != 0) glDeleteTextures(1, &erosionHeightTexA);
        if (erosionHeightTexB != 0) glDeleteTextures(1, &erosionHeightTexB);
        if (erosionSedTexA != 0) glDeleteTextures(1, &erosionSedTexA);
        if (erosionSedTexB != 0) glDeleteTextures(1, &erosionSedTexB);
        if (erosionVegTex != 0) glDeleteTextures(1, &erosionVegTex);
        if (blockSampler != 0) {
            glDeleteSamplers(1, &blockSampler);
        }
        glDeleteTextures(1, &blockTexAtlas);

        if (crossVbo != 0) glDeleteBuffers(1, &crossVbo);
        if (crossVao != 0) glDeleteVertexArrays(1, &crossVao);
        if (crossProg != 0) glDeleteProgram(crossProg);

        if (hotbarVbo != 0) glDeleteBuffers(1, &hotbarVbo);
        if (hotbarVao != 0) glDeleteVertexArrays(1, &hotbarVao);
        if (hotbarProg != 0) glDeleteProgram(hotbarProg);

        if (skyVao != 0) glDeleteVertexArrays(1, &skyVao);
        if (skyProg != 0) glDeleteProgram(skyProg);

        if (impostorEbo != 0) glDeleteBuffers(1, &impostorEbo);
        if (impostorVbo != 0) glDeleteBuffers(1, &impostorVbo);
        if (impostorVao != 0) glDeleteVertexArrays(1, &impostorVao);
        if (impostorProg != 0) glDeleteProgram(impostorProg);

        for (auto& level : horizonLevels) {
            if (level.ebo != 0) glDeleteBuffers(1, &level.ebo);
            if (level.vbo != 0) glDeleteBuffers(1, &level.vbo);
            if (level.vao != 0) glDeleteVertexArrays(
        if (horizonProg != 0) glDeleteProgram(horizonProg);

        if (invVbo != 0) glDeleteBuffers(1, &invVbo);
        if (invVao != 0) glDeleteVertexArrays(1, &invVao);
        if (invProg != 0) glDeleteProgram(invProg);

        if (aimVbo != 0) glDeleteBuffers(1, &aimVbo);
        if (aimVao != 0) glDeleteVertexArrays(1, &aimVao);
        if (aimProg != 0) glDeleteProgram(aimProg);

        if (faceVbo != 0) glDeleteBuffers(1, &faceVbo);
        if (faceVao != 0) glDeleteVertexArrays(1, &faceVao);
        if (faceProg != 0) glDeleteProgram(faceProg);
    }
}






