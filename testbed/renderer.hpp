// Draws layers three ways: a flat map, a globe and a 3D scene.
//
// Polygons are filled with the stencil buffer (even-odd triangle fans, then the bounding box), which needs no
// triangulation and copes with holes and the self-intersecting rings real files contain. The globe draws the same fills
// into an equirectangular texture once and wraps it around a sphere; lines and points are drawn in 3D on top so they
// stay sharp. The 3D scene draws the triangulated faces of MultiPatch and PolygonZ records.
#pragma once

#include "gl.hpp"
#include "layer.hpp"
#include "math.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace testbed
{

enum class ViewMode
{
   Map,
   Globe,
   Scene,
};

struct MapCamera
{
   double center_x = 0.0;
   double center_y = 0.0;
   double units_per_pixel = 0.2;
};

struct GlobeCamera
{
   float longitude = 10.0f;
   float latitude = 25.0f;
   float distance = 3.4f;  // from the centre, in globe radii
   bool spin = true;
};

struct SceneCamera
{
   float yaw = -2.2f;
   float pitch = 0.55f;
   float distance = 1.0f;  // in scene radii
   Vec3 target{};
   float z_scale = 1.0f;
};

/// \brief the shape under the mouse, drawn highlighted.
struct Highlight
{
   const Layer* layer = nullptr;
   std::size_t shape = 0;
};

/// \brief eye, matrices and pixel size of the last frame, for picking and labels.
struct Projection
{
   Mat4 view_projection = identity();
   Vec3 eye{};
   Vec3 forward{0, 0, -1};
   Vec3 right{1, 0, 0};
   Vec3 up{0, 1, 0};
   float tan_half_fov = 0.4f;
   int width = 1;
   int height = 1;

   /// \brief direction of the ray through a pixel (y down, like the mouse).
   [[nodiscard]] Vec3 ray(float x, float y) const
   {
      const float aspect = static_cast<float>(width) / static_cast<float>(height);
      const float nx = (2.0f * x / static_cast<float>(width) - 1.0f) * tan_half_fov * aspect;
      const float ny = (1.0f - 2.0f * y / static_cast<float>(height)) * tan_half_fov;
      return normalize(forward + right * nx + up * ny);
   }
};

using Layers = std::span<const std::unique_ptr<Layer>>;

class Renderer
{
public:
   Renderer();

   void draw_map(int width, int height, const MapCamera& camera, Layers layers, const Highlight& highlight, bool graticule);
   void draw_globe(int width, int height, const GlobeCamera& camera, Layers layers, const Highlight& highlight, bool graticule);
   void draw_scene(int width, int height, const SceneCamera& camera, Layers layers, const Highlight& highlight);

   /// \brief the fills on the globe are baked; call this when layers or styles change.
   void invalidate_globe()
   {
      globe_dirty_ = true;
   }

   [[nodiscard]] const Projection& projection() const
   {
      return projection_;
   }

   /// \brief bounds of the 3D layers relative to the first one's origin: min xyz, max xyz.
   static std::array<float, 6> scene_bounds(Layers layers);

   /// \brief renders offscreen with 4x MSAA via `draw` and writes a PNG.
   bool screenshot(const std::filesystem::path& path, int width, int height, const std::function<void()>& draw);

   /// \brief scale for point sizes and line widths on high dpi screens.
   float pixel_scale = 1.0f;

private:
   void bake_globe(Layers layers);
   void draw_fills(const Layer& layer, std::array<float, 2> offset, std::array<float, 2> scale, const std::array<double, 4>* view);
   void use_geo(int mode, std::array<float, 2> offset, std::array<float, 2> scale, const Mat4& mvp, float radius);
   void draw_dynamic(GLenum mode, const std::vector<float>& xyz, Rgba color);

   Program geo_;
   GLint geo_mode_ = -1;
   GLint geo_offset_ = -1;
   GLint geo_scale_ = -1;
   GLint geo_mvp_ = -1;
   GLint geo_radius_ = -1;
   GLint geo_round_ = -1;
   GLint geo_point_scale_ = -1;

   Program globe_;
   GLint globe_mvp_ = -1;
   GLint globe_light_ = -1;

   Program halo_;
   GLint halo_center_ = -1;
   GLint halo_radius_ = -1;

   Program mesh_;
   GLint mesh_mvp_ = -1;
   GLint mesh_offset_ = -1;
   GLint mesh_z_scale_ = -1;
   GLint mesh_highlight_ = -1;
   GLint mesh_light_ = -1;
   GLint mesh_colors_ = -1;

   VertexArray sphere_vao_;
   Buffer sphere_vbo_;
   Buffer sphere_ibo_;
   GLsizei sphere_index_count_ = 0;

   VertexArray graticule_vao_;
   Buffer graticule_vbo_;
   GLsizei graticule_count_ = 0;

   VertexArray dynamic_vao_;
   Buffer dynamic_vbo_;

   Framebuffer globe_fbo_;
   Texture globe_texture_;
   Renderbuffer globe_stencil_;
   bool globe_dirty_ = true;

   GLuint target_framebuffer_ = 0;  // where frames go: 0 or the screenshot framebuffer
   Projection projection_;
};

}  // namespace testbed
