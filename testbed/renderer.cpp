#include "renderer.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <print>
#include <stdexcept>
#include <string>

namespace testbed
{

namespace
{

constexpr int globe_texture_width = 4096;
constexpr int globe_texture_height = 2048;

const Rgba background{0.055f, 0.063f, 0.082f, 1.0f};
const Rgba highlight_color{1.0f, 0.86f, 0.25f, 1.0f};

// mode 0: flat map, (pos + offset) * scale; mode 1: longitude and latitude on a sphere; mode 2: 3D
constexpr const char* geo_vertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in float a_size;
uniform int u_mode;
uniform vec2 u_offset;
uniform vec2 u_scale;
uniform mat4 u_mvp;
uniform float u_radius;
uniform float u_point_scale;
out vec4 v_color;
void main()
{
   v_color = a_color;
   gl_PointSize = a_size * u_point_scale;
   if (u_mode == 0)
   {
      gl_Position = vec4((a_pos.xy + u_offset) * u_scale, 0.0, 1.0);
   }
   else if (u_mode == 1)
   {
      float lon = radians(a_pos.x);
      float lat = radians(a_pos.y);
      vec3 p = vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon)) * u_radius;
      gl_Position = u_mvp * vec4(p, 1.0);
   }
   else
   {
      gl_Position = u_mvp * vec4(a_pos, 1.0);
   }
}
)";

constexpr const char* geo_fragment = R"(#version 300 es
precision mediump float;
in vec4 v_color;
uniform bool u_round;
out vec4 o_color;
void main()
{
   o_color = v_color;
   if (u_round)
   {
      // a soft disc with a dark rim
      vec2 c = gl_PointCoord * 2.0 - 1.0;
      float r = length(c);
      if (r > 1.0)
      {
         discard;
      }
      float rim = smoothstep(0.6, 0.85, r);
      o_color = vec4(mix(v_color.rgb, v_color.rgb * 0.25, rim), v_color.a * (1.0 - smoothstep(0.85, 1.0, r)));
   }
}
)";

constexpr const char* globe_vertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec3 v_normal;
out vec2 v_uv;
void main()
{
   v_normal = a_pos;
   v_uv = a_uv;
   gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

constexpr const char* globe_fragment = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
in vec2 v_uv;
uniform sampler2D u_map;
uniform vec3 u_light;
out vec4 o_color;
void main()
{
   vec3 n = normalize(v_normal);
   vec4 land = texture(u_map, v_uv);
   vec3 ocean = mix(vec3(0.035, 0.075, 0.13), vec3(0.06, 0.13, 0.21), n.y * 0.5 + 0.5);
   vec3 color = mix(ocean, land.rgb, land.a);
   float diffuse = max(dot(n, u_light), 0.0);
   color *= 0.45 + 0.65 * diffuse;
   o_color = vec4(color, 1.0);
}
)";

constexpr const char* halo_vertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
uniform vec2 u_center;
uniform vec2 u_radius;
out vec2 v_local;
void main()
{
   v_local = a_pos.xy * 1.12;
   gl_Position = vec4(u_center + a_pos.xy * 1.12 * u_radius, 0.0, 1.0);
}
)";

constexpr const char* halo_fragment = R"(#version 300 es
precision mediump float;
in vec2 v_local;
out vec4 o_color;
void main()
{
   float r = length(v_local);
   // a thin atmosphere: bright at the limb, gone a tenth of a radius out
   float glow = pow(clamp((1.12 - r) / 0.12, 0.0, 1.0), 2.5) * step(0.97, r);
   o_color = vec4(0.4, 0.65, 1.0, 0.55 * glow);
}
)";

constexpr const char* mesh_vertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in float a_id;
uniform mat4 u_mvp;
uniform vec3 u_offset;
uniform float u_z_scale;
uniform float u_highlight;
uniform sampler2D u_colors;
out vec3 v_normal;
out vec4 v_color;
out float v_height;
void main()
{
   int id = int(a_id + 0.5);
   v_color = texelFetch(u_colors, ivec2(id % 1024, id / 1024), 0);
   if (abs(a_id - u_highlight) < 0.5)
   {
      v_color = vec4(mix(v_color.rgb, vec3(1.0, 0.86, 0.25), 0.75), 1.0);
   }
   v_normal = normalize(vec3(a_normal.xy, a_normal.z / max(u_z_scale, 0.01)));
   vec3 p = a_pos + u_offset;
   p.z *= u_z_scale;
   v_height = p.z;
   gl_Position = u_mvp * vec4(p, 1.0);
}
)";

constexpr const char* mesh_fragment = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
in vec4 v_color;
in float v_height;
uniform vec3 u_light;
out vec4 o_color;
void main()
{
   // faces in shapefiles have no reliable winding, so light both sides
   vec3 n = normalize(v_normal);
   float diffuse = abs(dot(n, u_light));
   float sky = 0.55 + 0.45 * abs(n.z);
   vec3 color = v_color.rgb * (0.30 * sky + 0.75 * diffuse);
   o_color = vec4(color, v_color.a);
}
)";

GLuint compile(GLenum type, const char* source)
{
   const GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);
   GLint ok = 0;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (ok == 0)
   {
      std::string log(1024, '\0');
      glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
      glDeleteShader(shader);
      throw std::runtime_error("shader: " + log);
   }
   return shader;
}

Program link(const char* vertex, const char* fragment)
{
   const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
   const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
   Program program{glCreateProgram()};
   glAttachShader(program.get(), vs);
   glAttachShader(program.get(), fs);
   glLinkProgram(program.get());
   glDeleteShader(vs);
   glDeleteShader(fs);
   GLint ok = 0;
   glGetProgramiv(program.get(), GL_LINK_STATUS, &ok);
   if (ok == 0)
   {
      throw std::runtime_error("shader program failed to link");
   }
   return program;
}

void make_buffer(VertexArray& vao, Buffer& vbo)
{
   GLuint name = 0;
   glGenVertexArrays(1, &name);
   vao = VertexArray{name};
   glGenBuffers(1, &name);
   vbo = Buffer{name};
}

void color_attribute(Rgba color)
{
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(2);
   glVertexAttrib4f(1, color.r, color.g, color.b, color.a);
   glVertexAttrib1f(2, 1.0f);
}

}  // namespace

Renderer::Renderer()
{
   geo_ = link(geo_vertex, geo_fragment);
   geo_mode_ = glGetUniformLocation(geo_.get(), "u_mode");
   geo_offset_ = glGetUniformLocation(geo_.get(), "u_offset");
   geo_scale_ = glGetUniformLocation(geo_.get(), "u_scale");
   geo_mvp_ = glGetUniformLocation(geo_.get(), "u_mvp");
   geo_radius_ = glGetUniformLocation(geo_.get(), "u_radius");
   geo_round_ = glGetUniformLocation(geo_.get(), "u_round");
   geo_point_scale_ = glGetUniformLocation(geo_.get(), "u_point_scale");

   globe_ = link(globe_vertex, globe_fragment);
   globe_mvp_ = glGetUniformLocation(globe_.get(), "u_mvp");
   globe_light_ = glGetUniformLocation(globe_.get(), "u_light");

   halo_ = link(halo_vertex, halo_fragment);
   halo_center_ = glGetUniformLocation(halo_.get(), "u_center");
   halo_radius_ = glGetUniformLocation(halo_.get(), "u_radius");

   mesh_ = link(mesh_vertex, mesh_fragment);
   mesh_mvp_ = glGetUniformLocation(mesh_.get(), "u_mvp");
   mesh_offset_ = glGetUniformLocation(mesh_.get(), "u_offset");
   mesh_z_scale_ = glGetUniformLocation(mesh_.get(), "u_z_scale");
   mesh_highlight_ = glGetUniformLocation(mesh_.get(), "u_highlight");
   mesh_light_ = glGetUniformLocation(mesh_.get(), "u_light");
   mesh_colors_ = glGetUniformLocation(mesh_.get(), "u_colors");

   // a uv sphere with a seam column, so the texture coordinates never wrap inside a triangle
   {
      constexpr int columns = 160;
      constexpr int rows = 80;
      std::vector<float> vertices;
      std::vector<uint32_t> indices;
      for (int row = 0; row <= rows; ++row)
      {
         const float v = static_cast<float>(row) / rows;
         for (int column = 0; column <= columns; ++column)
         {
            const float u = static_cast<float>(column) / columns;
            const Vec3 p = sphere_point(u * 360.0f - 180.0f, v * 180.0f - 90.0f);
            vertices.insert(vertices.end(), {p.x, p.y, p.z, u, v});
         }
      }
      for (int row = 0; row < rows; ++row)
      {
         for (int column = 0; column < columns; ++column)
         {
            const auto a = static_cast<uint32_t>(row * (columns + 1) + column);
            const auto b = a + static_cast<uint32_t>(columns + 1);
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
         }
      }
      make_buffer(sphere_vao_, sphere_vbo_);
      glBindVertexArray(sphere_vao_.get());
      glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo_.get());
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
      GLuint name = 0;
      glGenBuffers(1, &name);
      sphere_ibo_ = Buffer{name};
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, name);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
      sphere_index_count_ = static_cast<GLsizei>(indices.size());
      glBindVertexArray(0);
   }

   // graticule every 15 degrees, in pieces of 2 degrees
   {
      std::vector<float> lines;
      for (int lon = -180; lon < 180; lon += 15)
      {
         for (int lat = -90; lat < 90; lat += 2)
         {
            lines.insert(lines.end(), {static_cast<float>(lon), static_cast<float>(lat), static_cast<float>(lon), static_cast<float>(lat + 2)});
         }
      }
      for (int lat = -75; lat <= 75; lat += 15)
      {
         for (int lon = -180; lon < 180; lon += 2)
         {
            lines.insert(lines.end(), {static_cast<float>(lon), static_cast<float>(lat), static_cast<float>(lon + 2), static_cast<float>(lat)});
         }
      }
      make_buffer(graticule_vao_, graticule_vbo_);
      glBindVertexArray(graticule_vao_.get());
      glBindBuffer(GL_ARRAY_BUFFER, graticule_vbo_.get());
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
      graticule_count_ = static_cast<GLsizei>(lines.size() / 2);
      glBindVertexArray(0);
   }

   make_buffer(dynamic_vao_, dynamic_vbo_);
   glBindVertexArray(dynamic_vao_.get());
   glBindBuffer(GL_ARRAY_BUFFER, dynamic_vbo_.get());
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
   glBindVertexArray(0);
}

void Renderer::use_geo(int mode, std::array<float, 2> offset, std::array<float, 2> scale, const Mat4& mvp, float radius)
{
   glUseProgram(geo_.get());
   glUniform1i(geo_mode_, mode);
   glUniform2f(geo_offset_, offset[0], offset[1]);
   glUniform2f(geo_scale_, scale[0], scale[1]);
   glUniformMatrix4fv(geo_mvp_, 1, GL_FALSE, mvp.data());
   glUniform1f(geo_radius_, radius);
   glUniform1i(geo_round_, 0);
   glUniform1f(geo_point_scale_, pixel_scale);
}

void Renderer::draw_dynamic(GLenum mode, const std::vector<float>& xyz, Rgba color)
{
   glBindVertexArray(dynamic_vao_.get());
   glBindBuffer(GL_ARRAY_BUFFER, dynamic_vbo_.get());
   glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(xyz.size() * sizeof(float)), xyz.data(), GL_STREAM_DRAW);
   color_attribute(color);
   glDrawArrays(mode, 0, static_cast<GLsizei>(xyz.size() / 3));
}

void Renderer::draw_fills(const Layer& layer, std::array<float, 2> offset, std::array<float, 2> scale, const std::array<double, 4>* view)
{
   if (layer.ring_vertices.empty())
   {
      return;
   }
   glBindVertexArray(layer.ring_vao.get());
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0xff);
   for (std::size_t s = 0; s < layer.shape_rings.size(); ++s)
   {
      const auto& rings = layer.shape_rings[s];
      if (rings.count == 0 || layer.colors[s].a <= 0.0f)
      {
         continue;
      }
      if (view != nullptr)
      {
         // skip shapes outside the visible part of the map
         const auto& b = layer.file._shapes[s]._bounds;
         if (b._x_max < (*view)[0] || b._x_min > (*view)[2] || b._y_max < (*view)[1] || b._y_min > (*view)[3])
         {
            continue;
         }
      }
      // even-odd coverage into the stencil: every fan toggles the pixels it covers
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glStencilFunc(GL_ALWAYS, 0, 0xff);
      glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
      for (uint32_t r = rings.first; r < rings.first + rings.count; ++r)
      {
         glDrawArrays(GL_TRIANGLE_FAN, static_cast<GLint>(layer.rings[r].first), static_cast<GLsizei>(layer.rings[r].count));
      }
      // then colour the covered pixels inside the bounding box and clear the stencil again
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glStencilFunc(GL_NOTEQUAL, 0, 0xff);
      glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
      color_attribute(layer.colors[s]);
      glDrawArrays(GL_TRIANGLE_FAN, static_cast<GLint>(layer.shape_box[s]), 4);
   }
   glDisable(GL_STENCIL_TEST);
   (void)offset;
   (void)scale;
}

void Renderer::draw_map(int width, int height, const MapCamera& camera, Layers layers, const Highlight& highlight, bool graticule)
{
   glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
   glViewport(0, 0, width, height);
   glClearColor(background.r, background.g, background.b, 1.0f);
   glClearStencil(0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   projection_ = {};
   projection_.width = width;
   projection_.height = height;

   const std::array<float, 2> scale{
      static_cast<float>(2.0 / (width * camera.units_per_pixel)), static_cast<float>(2.0 / (height * camera.units_per_pixel))
   };
   const double half_w = width * camera.units_per_pixel * 0.5;
   const double half_h = height * camera.units_per_pixel * 0.5;

   const auto offset_of = [&](const Layer& layer) -> std::array<float, 2>
   { return {static_cast<float>(layer.origin_x - camera.center_x), static_cast<float>(layer.origin_y - camera.center_y)}; };

   // the ocean behind geographic data
   if (std::ranges::any_of(layers, [](const auto& layer) { return layer->visible && layer->geographic; }))
   {
      use_geo(0, {static_cast<float>(-camera.center_x), static_cast<float>(-camera.center_y)}, scale, identity(), 1.0f);
      draw_dynamic(GL_TRIANGLE_FAN, {-180, -90, 0, 180, -90, 0, 180, 90, 0, -180, 90, 0}, {0.047f, 0.094f, 0.15f, 1.0f});
   }
   if (graticule)
   {
      use_geo(0, {static_cast<float>(-camera.center_x), static_cast<float>(-camera.center_y)}, scale, identity(), 1.0f);
      glBindVertexArray(graticule_vao_.get());
      color_attribute({0.3f, 0.4f, 0.55f, 0.25f});
      glDrawArrays(GL_LINES, 0, graticule_count_);
   }

   for (const auto& layer : layers)
   {
      if (!layer->visible)
      {
         continue;
      }
      const auto offset = offset_of(*layer);
      use_geo(0, offset, scale, identity(), 1.0f);
      const std::array<double, 4> view{camera.center_x - half_w, camera.center_y - half_h, camera.center_x + half_w, camera.center_y + half_h};
      if (layer->style.show_fill)
      {
         draw_fills(*layer, offset, scale, &view);
      }
      if (layer->style.show_lines && !layer->line_vertices.empty())
      {
         glBindVertexArray(layer->line_vao.get());
         color_attribute(layer->is_line() ? layer->style.fill : layer->style.line);
         glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(layer->line_vertices.size() / 2));
      }
      if (layer->point_count > 0)
      {
         glBindVertexArray(layer->point_vao.get());
         glEnableVertexAttribArray(1);
         glEnableVertexAttribArray(2);
         glUniform1i(geo_round_, 1);
         glDrawArrays(GL_POINTS, 0, layer->point_count);
         glUniform1i(geo_round_, 0);
      }
   }

   if (highlight.layer != nullptr && highlight.layer->visible && highlight.shape < highlight.layer->shape_lines.size())
   {
      const auto& range = highlight.layer->shape_lines[highlight.shape];
      if (range.count > 0)
      {
         use_geo(0, offset_of(*highlight.layer), scale, identity(), 1.0f);
         glBindVertexArray(highlight.layer->line_vao.get());
         color_attribute(highlight_color);
         glDrawArrays(GL_LINES, static_cast<GLint>(range.first), static_cast<GLsizei>(range.count));
      }
   }
   glBindVertexArray(0);
}

void Renderer::bake_globe(Layers layers)
{
   GLuint name = 0;
   if (globe_fbo_.get() == 0)
   {
      glGenTextures(1, &name);
      globe_texture_ = Texture{name};
      glBindTexture(GL_TEXTURE_2D, name);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, globe_texture_width, globe_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      glGenRenderbuffers(1, &name);
      globe_stencil_ = Renderbuffer{name};
      glBindRenderbuffer(GL_RENDERBUFFER, name);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, globe_texture_width, globe_texture_height);

      glGenFramebuffers(1, &name);
      globe_fbo_ = Framebuffer{name};
      glBindFramebuffer(GL_FRAMEBUFFER, name);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, globe_texture_.get(), 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, globe_stencil_.get());
   }

   glBindFramebuffer(GL_FRAMEBUFFER, globe_fbo_.get());
   glViewport(0, 0, globe_texture_width, globe_texture_height);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClearStencil(0);
   glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   glDisable(GL_DEPTH_TEST);
   glEnable(GL_BLEND);
   // keep coverage in alpha so the ocean shows through where nothing was drawn
   glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
   for (const auto& layer : layers)
   {
      if (layer->visible && layer->geographic && layer->style.show_fill)
      {
         use_geo(0, {0.0f, 0.0f}, {1.0f / 180.0f, 1.0f / 90.0f}, identity(), 1.0f);
         draw_fills(*layer, {0.0f, 0.0f}, {1.0f / 180.0f, 1.0f / 90.0f}, nullptr);
      }
   }
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glBindTexture(GL_TEXTURE_2D, globe_texture_.get());
   glGenerateMipmap(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
   globe_dirty_ = false;
}

void Renderer::draw_globe(int width, int height, const GlobeCamera& camera, Layers layers, const Highlight& highlight, bool graticule)
{
   if (globe_dirty_)
   {
      bake_globe(layers);
   }

   glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
   glViewport(0, 0, width, height);
   glClearColor(background.r, background.g, background.b, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
   const Vec3 eye = sphere_point(camera.longitude, camera.latitude) * camera.distance;
   const float fov = 0.75f;
   const Mat4 view = look_at(eye, {0, 0, 0}, {0, 1, 0});
   const Mat4 mvp = perspective(fov, aspect, 0.05f, 50.0f) * view;

   projection_.view_projection = mvp;
   projection_.eye = eye;
   projection_.forward = normalize(Vec3{} - eye);
   projection_.right = normalize(cross(projection_.forward, {0, 1, 0}));
   projection_.up = cross(projection_.right, projection_.forward);
   projection_.tan_half_fov = std::tan(fov * 0.5f);
   projection_.width = width;
   projection_.height = height;

   // atmosphere glow behind the globe
   {
      const auto center = project(mvp, {0, 0, 0});
      const float apparent = std::asin(1.0f / camera.distance);
      const float radius_y = std::tan(apparent) / std::tan(fov * 0.5f);
      glDisable(GL_DEPTH_TEST);
      glUseProgram(halo_.get());
      glUniform2f(halo_center_, center[0], center[1]);
      glUniform2f(halo_radius_, radius_y / aspect, radius_y);
      draw_dynamic(GL_TRIANGLE_FAN, {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0}, {});
   }

   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   // the sun follows the camera a little, from the upper left
   const Vec3 light = normalize(eye * 0.8f + projection_.up * 1.2f - projection_.right * 0.9f);
   glUseProgram(globe_.get());
   glUniformMatrix4fv(globe_mvp_, 1, GL_FALSE, mvp.data());
   glUniform3f(globe_light_, light.x, light.y, light.z);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, globe_texture_.get());
   glBindVertexArray(sphere_vao_.get());
   glDrawElements(GL_TRIANGLES, sphere_index_count_, GL_UNSIGNED_INT, nullptr);

   // lines and points on top, slightly above the surface so the sphere's depth hides the far side only
   glDepthMask(GL_FALSE);
   if (graticule)
   {
      use_geo(1, {0, 0}, {1, 1}, mvp, 1.001f);
      glBindVertexArray(graticule_vao_.get());
      color_attribute({0.45f, 0.6f, 0.8f, 0.18f});
      glDrawArrays(GL_LINES, 0, graticule_count_);
   }
   for (const auto& layer : layers)
   {
      if (!layer->visible || !layer->geographic)
      {
         continue;
      }
      use_geo(1, {0, 0}, {1, 1}, mvp, 1.0015f);
      if (layer->style.show_lines && !layer->line_vertices.empty())
      {
         glBindVertexArray(layer->line_vao.get());
         color_attribute(layer->is_line() ? layer->style.fill : layer->style.line);
         glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(layer->line_vertices.size() / 2));
      }
      if (layer->point_count > 0)
      {
         glBindVertexArray(layer->point_vao.get());
         glEnableVertexAttribArray(1);
         glEnableVertexAttribArray(2);
         glUniform1i(geo_round_, 1);
         glDrawArrays(GL_POINTS, 0, layer->point_count);
         glUniform1i(geo_round_, 0);
      }
   }
   if (highlight.layer != nullptr && highlight.layer->geographic && highlight.shape < highlight.layer->shape_lines.size())
   {
      const auto& range = highlight.layer->shape_lines[highlight.shape];
      if (range.count > 0)
      {
         use_geo(1, {0, 0}, {1, 1}, mvp, 1.002f);
         glBindVertexArray(highlight.layer->line_vao.get());
         color_attribute(highlight_color);
         glDrawArrays(GL_LINES, static_cast<GLint>(range.first), static_cast<GLsizei>(range.count));
      }
   }
   glDepthMask(GL_TRUE);
   glBindVertexArray(0);
}

std::array<float, 6> Renderer::scene_bounds(Layers layers)
{
   std::array<float, 6> bounds{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
   const Layer* reference = nullptr;
   for (const auto& layer : layers)
   {
      if (!layer->has_z || !layer->visible)
      {
         continue;
      }
      reference = reference != nullptr ? reference : layer.get();
      const float dx = static_cast<float>(layer->origin_x - reference->origin_x);
      const float dy = static_cast<float>(layer->origin_y - reference->origin_y);
      const float dz = static_cast<float>(layer->origin_z - reference->origin_z);
      for (const auto& b : layer->shape_bounds3)
      {
         if (b[0] > b[3])
         {
            continue;
         }
         bounds[0] = std::min(bounds[0], b[0] + dx);
         bounds[1] = std::min(bounds[1], b[1] + dy);
         bounds[2] = std::min(bounds[2], b[2] + dz);
         bounds[3] = std::max(bounds[3], b[3] + dx);
         bounds[4] = std::max(bounds[4], b[4] + dy);
         bounds[5] = std::max(bounds[5], b[5] + dz);
      }
   }
   if (bounds[0] > bounds[3])
   {
      return {-1, -1, 0, 1, 1, 1};
   }
   return bounds;
}

void Renderer::draw_scene(int width, int height, const SceneCamera& camera, Layers layers, const Highlight& highlight)
{
   glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
   glViewport(0, 0, width, height);
   glClearColor(background.r, background.g, background.b, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   const auto bounds = scene_bounds(layers);
   const Vec3 extent{bounds[3] - bounds[0], bounds[4] - bounds[1], (bounds[5] - bounds[2]) * camera.z_scale};
   const float radius = std::max(0.5f * length(extent), 1e-3f);

   const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
   const float fov = 0.8f;
   const float distance = camera.distance * radius * 2.2f;
   const Vec3 eye = camera.target + Vec3{std::cos(camera.pitch) * std::cos(camera.yaw), std::cos(camera.pitch) * std::sin(camera.yaw), std::sin(camera.pitch)} * distance;
   const Mat4 mvp = perspective(fov, aspect, distance * 0.01f, distance * 10.0f) * look_at(eye, camera.target, {0, 0, 1});

   projection_.view_projection = mvp;
   projection_.eye = eye;
   projection_.forward = normalize(camera.target - eye);
   projection_.right = normalize(cross(projection_.forward, {0, 0, 1}));
   projection_.up = cross(projection_.right, projection_.forward);
   projection_.tan_half_fov = std::tan(fov * 0.5f);
   projection_.width = width;
   projection_.height = height;

   // ground with a grid every power of ten that gives 10 to 100 lines
   {
      use_geo(2, {0, 0}, {1, 1}, mvp, 1.0f);
      const float pad = radius * 0.6f;
      const float x0 = bounds[0] - pad;
      const float y0 = bounds[1] - pad;
      const float x1 = bounds[3] + pad;
      const float y1 = bounds[4] + pad;
      const float z = bounds[2] * camera.z_scale - radius * 0.002f;
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
      draw_dynamic(GL_TRIANGLE_FAN, {x0, y0, z, x1, y0, z, x1, y1, z, x0, y1, z}, {0.09f, 0.10f, 0.12f, 1.0f});
      glDisable(GL_POLYGON_OFFSET_FILL);
      const float step = std::pow(10.0f, std::floor(std::log10(std::max(x1 - x0, y1 - y0) / 10.0f)));
      std::vector<float> grid;
      for (float x = std::ceil(x0 / step) * step; x <= x1; x += step)
      {
         grid.insert(grid.end(), {x, y0, z, x, y1, z});
      }
      for (float y = std::ceil(y0 / step) * step; y <= y1; y += step)
      {
         grid.insert(grid.end(), {x0, y, z, x1, y, z});
      }
      draw_dynamic(GL_LINES, grid, {0.2f, 0.24f, 0.3f, 0.6f});
   }

   const Vec3 light = normalize({0.45f, -0.35f, 0.82f});
   const Layer* reference = nullptr;
   for (const auto& layer : layers)
   {
      if (!layer->visible || !layer->has_z)
      {
         continue;
      }
      reference = reference != nullptr ? reference : layer.get();
      const Vec3 offset{
         static_cast<float>(layer->origin_x - reference->origin_x),
         static_cast<float>(layer->origin_y - reference->origin_y),
         static_cast<float>(layer->origin_z - reference->origin_z)
      };
      const float highlighted = highlight.layer == layer.get() ? static_cast<float>(highlight.shape) : -1.0f;

      if (!layer->mesh_vertices.empty() && layer->style.show_fill)
      {
         glUseProgram(mesh_.get());
         glUniformMatrix4fv(mesh_mvp_, 1, GL_FALSE, mvp.data());
         glUniform3f(mesh_offset_, offset.x, offset.y, offset.z);
         glUniform1f(mesh_z_scale_, camera.z_scale);
         glUniform1f(mesh_highlight_, highlighted);
         glUniform3f(mesh_light_, light.x, light.y, light.z);
         glUniform1i(mesh_colors_, 0);
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, layer->color_texture.get());
         glBindVertexArray(layer->mesh_vao.get());
         glEnable(GL_POLYGON_OFFSET_FILL);
         glPolygonOffset(1.0f, 1.0f);
         glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(layer->mesh_vertices.size() / 7));
         glDisable(GL_POLYGON_OFFSET_FILL);
      }

      // the edges and 3D polylines go through the geo program, with the offset and z scale in the matrix
      Mat4 model = identity();
      model[10] = camera.z_scale;
      model[12] = offset.x;
      model[13] = offset.y;
      model[14] = offset.z * camera.z_scale;
      const Mat4 layer_mvp = mvp * model;
      const bool lines_only = layer->mesh_vertices.empty();
      if (!layer->edge_vertices.empty() && (layer->style.edges || lines_only))
      {
         use_geo(2, {0, 0}, {1, 1}, layer_mvp, 1.0f);
         glBindVertexArray(layer->edge_vao.get());
         glDisableVertexAttribArray(1);
         color_attribute(lines_only ? layer->style.fill : Rgba{0.0f, 0.0f, 0.0f, 0.35f});
         glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(layer->edge_vertices.size() / 4));
      }
      if (layer->point_count > 0)
      {
         use_geo(2, {0, 0}, {1, 1}, layer_mvp, 1.0f);
         glBindVertexArray(layer->point_vao.get());
         glEnableVertexAttribArray(1);
         glEnableVertexAttribArray(2);
         glUniform1i(geo_round_, 1);
         glDrawArrays(GL_POINTS, 0, layer->point_count);
         glUniform1i(geo_round_, 0);
      }
   }
   glBindVertexArray(0);
}

bool Renderer::screenshot(const std::filesystem::path& path, int width, int height, const std::function<void()>& draw)
{
   GLuint name = 0;
   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);

   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_depth{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, width, height);

   glGenFramebuffers(1, &name);
   const Framebuffer msaa{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color.get());
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaa_depth.get());

   glGenRenderbuffers(1, &name);
   const Renderbuffer resolved_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);

   glGenFramebuffers(1, &name);
   const Framebuffer resolved{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolved_color.get());

   glBindFramebuffer(GL_FRAMEBUFFER, msaa.get());
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      std::println(stderr, "offscreen framebuffer is incomplete");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
   }

   target_framebuffer_ = msaa.get();
   draw();
   target_framebuffer_ = 0;

   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa.get());
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved.get());
   glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, resolved.get());
   std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
   glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   for (std::size_t i = 3; i < pixels.size(); i += 4)
   {
      pixels[i] = 255;  // the background is opaque; blended alpha would make the png see-through
   }
   stbi_flip_vertically_on_write(1);
   return stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

}  // namespace testbed
