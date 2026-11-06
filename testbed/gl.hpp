// OpenGL ES 3.0 everywhere: Emscripten maps it onto WebGL2, natively glad loads it.
#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/gles2.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <utility>

namespace testbed
{

// owns one GL object name and frees it with the matching glDelete* call
template <void (*Delete)(GLuint)>
class GlObject
{
public:
   GlObject() = default;
   explicit GlObject(GLuint name) : name_(name)
   {
   }
   GlObject(GlObject&& other) noexcept : name_(std::exchange(other.name_, 0))
   {
   }
   GlObject& operator=(GlObject&& other) noexcept
   {
      std::swap(name_, other.name_);
      return *this;
   }
   GlObject(const GlObject&) = delete;
   GlObject& operator=(const GlObject&) = delete;
   ~GlObject()
   {
      if (name_ != 0)
      {
         Delete(name_);
      }
   }

   [[nodiscard]] GLuint get() const noexcept
   {
      return name_;
   }

private:
   GLuint name_ = 0;
};

inline void delete_buffer(GLuint name)
{
   glDeleteBuffers(1, &name);
}

inline void delete_vertex_array(GLuint name)
{
   glDeleteVertexArrays(1, &name);
}

inline void delete_program(GLuint name)
{
   glDeleteProgram(name);
}

inline void delete_framebuffer(GLuint name)
{
   glDeleteFramebuffers(1, &name);
}

inline void delete_renderbuffer(GLuint name)
{
   glDeleteRenderbuffers(1, &name);
}

inline void delete_texture(GLuint name)
{
   glDeleteTextures(1, &name);
}

using Buffer = GlObject<delete_buffer>;
using VertexArray = GlObject<delete_vertex_array>;
using Program = GlObject<delete_program>;
using Framebuffer = GlObject<delete_framebuffer>;
using Renderbuffer = GlObject<delete_renderbuffer>;
using Texture = GlObject<delete_texture>;

}  // namespace testbed
