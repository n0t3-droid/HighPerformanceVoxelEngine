#include "TextureArray.h"
#include <iostream>
#include <GLFW/glfw3.h>

// Include stb_image for texture loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Bindless texture function pointers (loaded at runtime)
typedef GLuint64 (APIENTRY *PFNGLGETTEXTUREHANDLEARBPROC)(GLuint texture);
typedef void (APIENTRY *PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
typedef void (APIENTRY *PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)(GLuint64 handle);

static PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB = nullptr;
static PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB = nullptr;
static PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB = nullptr;

static bool s_BindlessLoaded = false;

static bool LoadBindlessExtensions() {
    if (s_BindlessLoaded) return true;
    
    glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)glfwGetProcAddress("glGetTextureHandleARB");
    glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)glfwGetProcAddress("glMakeTextureHandleResidentARB");
    glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)glfwGetProcAddress("glMakeTextureHandleNonResidentARB");
    
    if (glGetTextureHandleARB && glMakeTextureHandleResidentARB && glMakeTextureHandleNonResidentARB) {
        s_BindlessLoaded = true;
        std::cout << "Bindless textures: SUPPORTED" << std::endl;
        return true;
    }
    
    std::cerr << "WARNING: Bindless textures not supported on this GPU!" << std::endl;
    return false;
}

namespace Engine {

    TextureArray::TextureArray() {
        // Attempt to load bindless extensions
        LoadBindlessExtensions();
    }

    TextureArray::~TextureArray() {
        MakeAllNonResident();
        
        // Delete all textures
        for (GLuint id : m_TextureIDs) {
            glDeleteTextures(1, &id);
        }
        m_TextureIDs.clear();
        m_Handles.clear();
        m_IsResident.clear();
    }

    int TextureArray::LoadTexture(const std::string& filepath) {
        // Load image with stb_image
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 4); // Force RGBA
        
        if (!data) {
            std::cerr << "Failed to load texture: " << filepath << std::endl;
            return -1;
        }

        // Create OpenGL texture
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        // Set parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Pixelated look for voxels
        
        // Upload texture data
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        stbi_image_free(data);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        // Store texture ID
        m_TextureIDs.push_back(textureID);
        
        // Get bindless handle if supported
        GLuint64 handle = 0;
        if (s_BindlessLoaded && glGetTextureHandleARB) {
            handle = glGetTextureHandleARB(textureID);
        }
        m_Handles.push_back(handle);
        m_IsResident.push_back(false);
        
        std::cout << "Loaded texture: " << filepath << " (ID: " << textureID << ", Handle: " << handle << ")" << std::endl;
        
        return static_cast<int>(m_TextureIDs.size() - 1);
    }

    bool TextureArray::LoadTextureArray(const std::vector<std::string>& filepaths) {
        bool allSuccess = true;
        for (const auto& path : filepaths) {
            if (LoadTexture(path) < 0) {
                allSuccess = false;
            }
        }
        return allSuccess;
    }

    GLuint64 TextureArray::GetHandle(size_t index) const {
        if (index >= m_Handles.size()) {
            std::cerr << "TextureArray::GetHandle: Index out of bounds" << std::endl;
            return 0;
        }
        return m_Handles[index];
    }

    void TextureArray::MakeAllResident() {
        if (!s_BindlessLoaded) return;
        
        for (size_t i = 0; i < m_Handles.size(); ++i) {
            if (!m_IsResident[i] && m_Handles[i] != 0) {
                glMakeTextureHandleResidentARB(m_Handles[i]);
                m_IsResident[i] = true;
            }
        }
    }

    void TextureArray::MakeAllNonResident() {
        if (!s_BindlessLoaded) return;
        
        for (size_t i = 0; i < m_Handles.size(); ++i) {
            if (m_IsResident[i] && m_Handles[i] != 0) {
                glMakeTextureHandleNonResidentARB(m_Handles[i]);
                m_IsResident[i] = false;
            }
        }
    }

} // namespace Engine
