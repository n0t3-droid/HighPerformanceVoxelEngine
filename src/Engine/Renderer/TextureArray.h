#pragma once

#include <glad/glad.h>
#include <vector>
#include <string>
#include <cstdint>

// Bindless texture extension defines (if not present in glad)
#ifndef GL_ARB_bindless_texture
#define GL_ARB_bindless_texture 1
typedef uint64_t GLuint64;
#endif

namespace Engine {

    /**
     * @brief TextureArray class for managing bindless textures.
     * 
     * Uses GL_ARB_bindless_texture for GPU-resident texture handles,
     * eliminating texture binding overhead during rendering.
     */
    class TextureArray {
    public:
        TextureArray();
        ~TextureArray();

        // No copy
        TextureArray(const TextureArray&) = delete;
        TextureArray& operator=(const TextureArray&) = delete;

        /**
         * @brief Load a texture from file and add to the array.
         * @param filepath Path to the texture file (PNG, JPG, etc.)
         * @return Index of the texture in the array, or -1 on failure.
         */
        int LoadTexture(const std::string& filepath);

        /**
         * @brief Create a texture array from multiple image files.
         * @param filepaths Vector of file paths to load.
         * @return true on success, false if any texture failed to load.
         */
        bool LoadTextureArray(const std::vector<std::string>& filepaths);

        /**
         * @brief Get the bindless handle for a texture at given index.
         * @param index Texture index.
         * @return GPU-resident texture handle (GLuint64).
         */
        GLuint64 GetHandle(size_t index) const;

        /**
         * @brief Get all texture handles for shader upload.
         * @return Vector of all texture handles.
         */
        const std::vector<GLuint64>& GetAllHandles() const { return m_Handles; }

        /**
         * @brief Get the number of loaded textures.
         */
        size_t GetTextureCount() const { return m_TextureIDs.size(); }

        /**
         * @brief Make all textures resident on the GPU.
         * Must be called before rendering.
         */
        void MakeAllResident();

        /**
         * @brief Make all textures non-resident.
         * Should be called before destruction or when textures are no longer needed.
         */
        void MakeAllNonResident();

    private:
        std::vector<GLuint> m_TextureIDs;      // OpenGL texture IDs
        std::vector<GLuint64> m_Handles;       // Bindless texture handles
        std::vector<bool> m_IsResident;        // Residency state per texture
        
        int m_Width = 0;
        int m_Height = 0;
        bool m_Initialized = false;
    };

} // namespace Engine
