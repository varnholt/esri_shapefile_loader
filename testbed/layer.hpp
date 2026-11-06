// A loaded shapefile, its style and the vertex buffers built from it.
#pragma once

#include "gl.hpp"
#include "math.hpp"
#include "palette.hpp"

#include <tinyshp/tinyshp.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace testbed
{

enum class ColorMode
{
   Single,
   Categories,
   Graduated,
};

struct Style
{
   ColorMode mode = ColorMode::Single;
   int field = -1;  // .dbf field for Categories and Graduated
   int ramp = 0;    // index into ramps
   bool log_scale = false;
   bool invert = false;
   Rgba fill{0.55f, 0.62f, 0.70f, 1.0f};
   Rgba line{0.08f, 0.09f, 0.11f, 0.6f};
   bool show_fill = true;
   bool show_lines = true;
   float opacity = 1.0f;
   float point_size = 5.0f;
   int size_field = -1;    // scales points by a numeric field
   int label_field = -1;   // labels points with a field
   int label_count = 40;   // the largest points get labels first
   bool edges = true;      // 3D: draw face outlines
};

/// \brief what the legend shows.
struct Legend
{
   double minimum = 0.0;
   double maximum = 0.0;
   std::vector<std::pair<std::string, Rgba>> categories;  // value and colour, most frequent first
   std::size_t more_categories = 0;
};

struct Range
{
   uint32_t first = 0;
   uint32_t count = 0;
};

struct Layer
{
   std::string name;
   tinyshp::Shapefile file;
   double load_milliseconds = 0.0;
   std::string warning;  // e.g. the .dbf did not match and was skipped

   bool visible = true;
   bool geographic = false;  // coordinates are longitude and latitude
   bool has_z = false;
   Style style;
   Legend legend;

   // coordinates are stored relative to this, so floats keep their precision for projected data in feet or metres
   double origin_x = 0.0;
   double origin_y = 0.0;
   double origin_z = 0.0;

   // per shape, filled in by apply_style()
   std::vector<Rgba> colors;
   std::vector<float> sizes;

   // per shape ranges into the buffers below
   std::vector<Range> shape_rings;      // into rings
   std::vector<Range> rings;            // into ring_vertices, one triangle fan each
   std::vector<uint32_t> shape_box;     // first of the 4 bounding box corners in ring_vertices
   std::vector<Range> shape_lines;      // into line_vertices (pairs for GL_LINES)
   std::vector<Range> shape_triangles;  // into mesh_vertices (3D)
   std::vector<Range> shape_edges;      // into edge_vertices (3D lines)
   std::vector<std::array<float, 6>> shape_bounds3;  // min xyz, max xyz relative to the origin
   std::vector<std::array<double, 3>> anchors;       // where labels go, in file coordinates
   std::vector<double> priority;                     // bigger shapes get labels first: area, length or height

   std::vector<float> ring_vertices;  // x, y
   std::vector<float> line_vertices;  // x, y; geographic lines are split into short pieces so they follow the globe
   std::vector<float> mesh_vertices;  // x, y, z, nx, ny, nz, shape id
   std::vector<float> edge_vertices;  // x, y, z, shape id

   // GL objects
   VertexArray ring_vao;
   Buffer ring_vbo;
   VertexArray line_vao;
   Buffer line_vbo;
   VertexArray point_vao;
   Buffer point_vbo;  // x, y, z, r, g, b, a, size; rebuilt with the style
   GLsizei point_count = 0;
   VertexArray mesh_vao;
   Buffer mesh_vbo;
   VertexArray edge_vao;
   Buffer edge_vbo;
   Texture color_texture;  // per shape colour for the 3D mesh, fetched by shape id

   [[nodiscard]] tinyshp::ShapeType base_type() const
   {
      return tinyshp::baseType(file._header._type);
   }

   [[nodiscard]] bool is_polygon() const
   {
      return base_type() == tinyshp::ShapeType::Polygon;
   }

   [[nodiscard]] bool is_line() const
   {
      return base_type() == tinyshp::ShapeType::PolyLine;
   }

   [[nodiscard]] bool is_point() const
   {
      const auto base = base_type();
      return base == tinyshp::ShapeType::Point || base == tinyshp::ShapeType::MultiPoint;
   }

   [[nodiscard]] std::size_t field_count() const
   {
      return file._attributes ? file._attributes->_fields.size() : 0;
   }

   [[nodiscard]] std::optional<double> number(std::size_t shape, int field) const;
   [[nodiscard]] std::string text(std::size_t shape, int field) const;
   [[nodiscard]] int find_field(std::string_view name) const;
};

/// \brief builds the geometry of a freshly loaded layer and uploads it. Needs a current GL context.
void build(Layer& layer);

/// \brief recomputes colours, sizes, the legend and the buffers that depend on them.
void apply_style(Layer& layer);

/// \brief true if every coordinate is in longitude and latitude range; for files without a .prj.
bool looks_geographic(const tinyshp::Shapefile& file);

}  // namespace testbed
