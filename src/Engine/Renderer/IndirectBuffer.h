#pragma once

#include <vector>
#include <glad/glad.h>

namespace Engine {

    struct DrawCommand {
        unsigned int  count;
        unsigned int  instanceCount;
        unsigned int  firstIndex;
        unsigned int  baseVertex;
        unsigned int  baseInstance;
    };

    class IndirectBuffer {
    public:
        IndirectBuffer();
        ~IndirectBuffer();

        void Bind() const;
        void Upload(const std::vector<DrawCommand>& commands);
        
        unsigned int GetID() const { return m_BufferID; }
        size_t GetCommandCount() const { return m_CommandCount; }

    private:
        unsigned int m_BufferID;
        size_t m_CommandCount;
    };

}
