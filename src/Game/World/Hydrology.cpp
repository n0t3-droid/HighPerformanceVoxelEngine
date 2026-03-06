#include "Hydrology.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>

namespace Game { namespace World {

    namespace {
        constexpr int kDirCount = 8;
        constexpr int kDx[kDirCount] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        constexpr int kDz[kDirCount] = { 0, 1, 1, 1, 0, -1, -1, -1 };

        inline bool IsValid(int x, int z, int width, int height) {
            return x >= 0 && z >= 0 && x < width && z < height;
        }

        inline int Idx(int x, int z, int width) {
            return z * width + x;
        }

        inline bool IsBoundary(int x, int z, int width, int height) {
            return x == 0 || z == 0 || x == (width - 1) || z == (height - 1);
        }

        struct FloodNode {
            float h = 0.0f;
            int index = 0;
            bool operator>(const FloodNode& other) const { return h > other.h; }
        };
    }

    bool Hydrology::PriorityFloodFill(const std::vector<float>& heightIn, int width, int height, std::vector<float>& outFilled) {
        if (width <= 1 || height <= 1) return false;
        const int cellCount = width * height;
        if ((int)heightIn.size() < cellCount) return false;

        outFilled = heightIn;
        std::vector<uint8_t> closed((size_t)cellCount, 0);
        std::priority_queue<FloodNode, std::vector<FloodNode>, std::greater<FloodNode>> open;

        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                if (!IsBoundary(x, z, width, height)) continue;
                const int i = Idx(x, z, width);
                if (closed[(size_t)i]) continue;
                closed[(size_t)i] = 1;
                open.push(FloodNode{outFilled[(size_t)i], i});
            }
        }

        while (!open.empty()) {
            const FloodNode cur = open.top();
            open.pop();
            const int cx = cur.index % width;
            const int cz = cur.index / width;

            for (int d = 0; d < kDirCount; ++d) {
                const int nx = cx + kDx[d];
                const int nz = cz + kDz[d];
                if (!IsValid(nx, nz, width, height)) continue;
                const int ni = Idx(nx, nz, width);
                if (closed[(size_t)ni]) continue;

                closed[(size_t)ni] = 1;
                const float raised = std::max(outFilled[(size_t)ni], cur.h);
                outFilled[(size_t)ni] = raised;
                open.push(FloodNode{raised, ni});
            }
        }

        return true;
    }

    bool Hydrology::ComputeFlowDirectionD8(const std::vector<float>& heightField, int width, int height, std::vector<int8_t>& outDirD8) {
        if (width <= 1 || height <= 1) return false;
        const int cellCount = width * height;
        if ((int)heightField.size() < cellCount) return false;

        outDirD8.assign((size_t)cellCount, (int8_t)-1);

        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const int i = Idx(x, z, width);
                const float h0 = heightField[(size_t)i];
                float bestDrop = 0.0f;
                int8_t bestDir = -1;

                for (int d = 0; d < kDirCount; ++d) {
                    const int nx = x + kDx[d];
                    const int nz = z + kDz[d];
                    if (!IsValid(nx, nz, width, height)) continue;
                    const int ni = Idx(nx, nz, width);
                    const float drop = h0 - heightField[(size_t)ni];
                    if (drop > bestDrop) {
                        bestDrop = drop;
                        bestDir = (int8_t)d;
                    }
                }

                outDirD8[(size_t)i] = bestDir;
            }
        }

        return true;
    }

    bool Hydrology::ComputeFlowAccumulation(const std::vector<int8_t>& dirD8,
                                            const std::vector<float>& heightField,
                                            int width,
                                            int height,
                                            std::vector<float>& outAccum) {
        if (width <= 1 || height <= 1) return false;
        const int cellCount = width * height;
        if ((int)dirD8.size() < cellCount || (int)heightField.size() < cellCount) return false;

        outAccum.assign((size_t)cellCount, 1.0f);

        std::vector<int> order((size_t)cellCount);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return heightField[(size_t)a] > heightField[(size_t)b];
        });

        for (int i : order) {
            const int8_t d = dirD8[(size_t)i];
            if (d < 0 || d >= kDirCount) continue;

            const int x = i % width;
            const int z = i / width;
            const int nx = x + kDx[d];
            const int nz = z + kDz[d];
            if (!IsValid(nx, nz, width, height)) continue;

            const int ni = Idx(nx, nz, width);
            outAccum[(size_t)ni] += outAccum[(size_t)i];
        }

        return true;
    }

    bool Hydrology::BuildHydrologyMaps(const std::vector<float>& heightIn, int width, int height, HydrologyMaps& outMaps) {
        outMaps = HydrologyMaps{};

        std::vector<float> filled;
        if (!PriorityFloodFill(heightIn, width, height, filled)) return false;

        std::vector<int8_t> dir;
        if (!ComputeFlowDirectionD8(filled, width, height, dir)) return false;

        std::vector<float> accum;
        if (!ComputeFlowAccumulation(dir, filled, width, height, accum)) return false;

        outMaps.width = width;
        outMaps.height = height;
        outMaps.filledHeight = std::move(filled);
        outMaps.flowDirD8 = std::move(dir);
        outMaps.flowAccum = std::move(accum);
        return true;
    }

}}