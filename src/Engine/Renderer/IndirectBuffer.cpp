#include "Engine/Renderer/IndirectBuffer.h"
#include <glad/glad.h>

#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

namespace Engine {

    IndirectBuffer::IndirectBuffer() : m_CommandCount(0) {
        // Use glGenBuffers instead of glCreateBuffers (4.5)
        glGenBuffers(1, &m_BufferID);
    }

    IndirectBuffer::~IndirectBuffer() {
        glDeleteBuffers(1, &m_BufferID);
    }

    void IndirectBuffer::Bind() const {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_BufferID);
    }

    void IndirectBuffer::Upload(const std::vector<DrawCommand>& commands) {
        m_CommandCount = commands.size();
        if (m_CommandCount > 0) {
            // Use glBindBuffer + glBufferData instead of glNamedBufferData
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_BufferID);
            glBufferData(GL_DRAW_INDIRECT_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_DYNAMIC_DRAW);
        }
    }
}
