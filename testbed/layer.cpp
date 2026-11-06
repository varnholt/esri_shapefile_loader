#include "layer.hpp"

#include "triangulate.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <string_view>
#include <variant>

namespace testbed
{

namespace
{

constexpr int color_texture_width = 1024;

/// \brief longest piece of a geographic line, in degrees, so it bends with the globe.
constexpr double max_piece_degrees = 2.0;

void upload(Buffer& buffer, VertexArray& vao, const std::vector<float>& data, std::span<const std::pair<GLint, std::size_t>> attributes)
{
   GLuint name = 0;
   if (vao.get() == 0)
   {
      glGenVertexArrays(1, &name);
      vao = VertexArray{name};
      glGenBuffers(1, &name);
      buffer = Buffer{name};
   }
   glBindVertexArray(vao.get());
   glBindBuffer(GL_ARRAY_BUFFER, buffer.get());
   glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);

   std::size_t stride = 0;
   for (const auto& [size, location] : attributes)
   {
      (void)location;
      stride += static_cast<std::size_t>(size);
   }
   std::size_t offset = 0;
   for (const auto& [size, location] : attributes)
   {
      const auto index = static_cast<GLuint>(location);
      glEnableVertexAttribArray(index);
      glVertexAttribPointer(
         index,
         size,
         GL_FLOAT,
         GL_FALSE,
         static_cast<GLsizei>(stride * sizeof(float)),
         reinterpret_cast<const void*>(offset * sizeof(float))
      );
      offset += static_cast<std::size_t>(size);
   }
   glBindVertexArray(0);
}

void add_line(Layer& layer, const tinyshp::Shape& shape, std::size_t first, std::size_t last)
{
   const auto x = [&](std::size_t i) { return shape._points[i]._x - layer.origin_x; };
   const auto y = [&](std::size_t i) { return shape._points[i]._y - layer.origin_y; };
   for (std::size_t i = first; i + 1 < last; ++i)
   {
      const double dx = x(i + 1) - x(i);
      const double dy = y(i + 1) - y(i);
      const int pieces = layer.geographic ? std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)) / max_piece_degrees))) : 1;
      for (int k = 0; k < pieces; ++k)
      {
         const double t0 = static_cast<double>(k) / pieces;
         const double t1 = static_cast<double>(k + 1) / pieces;
         layer.line_vertices.insert(
            layer.line_vertices.end(),
            {static_cast<float>(x(i) + dx * t0),
             static_cast<float>(y(i) + dy * t0),
             static_cast<float>(x(i) + dx * t1),
             static_cast<float>(y(i) + dy * t1)}
         );
      }
   }
}

using Point3 = std::array<double, 3>;

Point3 point3(const Layer& layer, const tinyshp::Shape& shape, std::size_t i)
{
   const double z = shape._z.empty() ? 0.0 : shape._z[i];
   return {shape._points[i]._x - layer.origin_x, shape._points[i]._y - layer.origin_y, z - layer.origin_z};
}

void add_triangle(Layer& layer, Point3 a, Point3 b, Point3 c, float id)
{
   const Vec3 fa{static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2])};
   const Vec3 fb{static_cast<float>(b[0]), static_cast<float>(b[1]), static_cast<float>(b[2])};
   const Vec3 fc{static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
   const Vec3 n = normalize(cross(fb - fa, fc - fa));
   for (const auto& p : {fa, fb, fc})
   {
      layer.mesh_vertices.insert(layer.mesh_vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, id});
   }
}

void add_edges(Layer& layer, std::span<const Point3> ring, float id)
{
   for (std::size_t i = 0; i + 1 < ring.size(); ++i)
   {
      for (const auto& p : {ring[i], ring[i + 1]})
      {
         layer.edge_vertices.insert(
            layer.edge_vertices.end(), {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), id}
         );
      }
   }
}

/// \brief triangles and outlines of a MultiPatch or PolygonZ record.
void add_solid(Layer& layer, const tinyshp::Shape& shape, float id)
{
   const auto base = tinyshp::baseType(shape._type);
   if (base == tinyshp::ShapeType::MultiPatch)
   {
      for (std::size_t p = 0; p < shape.partCount() && p < shape._part_types.size(); ++p)
      {
         const auto [first, last] = shape.partRange(p);
         if (shape._part_types[p] == tinyshp::PartType::TriangleStrip)
         {
            for (std::size_t i = first; i + 2 < last; ++i)
            {
               const bool odd = (i - first) % 2 == 1;
               add_triangle(layer, point3(layer, shape, i), point3(layer, shape, odd ? i + 2 : i + 1), point3(layer, shape, odd ? i + 1 : i + 2), id);
            }
         }
         else if (shape._part_types[p] == tinyshp::PartType::TriangleFan)
         {
            for (std::size_t i = first + 1; i + 1 < last; ++i)
            {
               add_triangle(layer, point3(layer, shape, first), point3(layer, shape, i), point3(layer, shape, i + 1), id);
            }
            std::vector<Point3> outline;
            for (std::size_t i = first; i < last; ++i)
            {
               outline.push_back(point3(layer, shape, i));
            }
            outline.push_back(outline.front());
            add_edges(layer, outline, id);
         }
      }
   }

   // rings with their holes; polygons() handles the part types of MultiPatch and the orientation of PolygonZ
   for (const auto& polygon : tinyshp::polygons(shape))
   {
      const auto ring3 = [&](std::span<const tinyshp::Point> ring)
      {
         const auto first = static_cast<std::size_t>(ring.data() - shape._points.data());
         std::vector<Point3> points;
         for (std::size_t i = 0; i < ring.size(); ++i)
         {
            points.push_back(point3(layer, shape, first + i));
         }
         return points;
      };
      const auto outer = ring3(polygon._outer);
      std::vector<std::vector<Point3>> holes;
      for (const auto& hole : polygon._holes)
      {
         holes.push_back(ring3(hole));
      }

      std::vector<Point3> all = outer;
      if (all.size() > 1 && all.front() == all.back())
      {
         all.pop_back();
      }
      std::vector<std::span<const Point3>> hole_spans;
      for (const auto& hole : holes)
      {
         hole_spans.emplace_back(hole);
         all.insert(all.end(), hole.begin(), hole.end() - (hole.size() > 1 && hole.front() == hole.back() ? 1 : 0));
      }
      const auto triangles = triangulate_face(outer, hole_spans);

      // keep the winding of the outer ring, which is how the face points
      const auto normal = newell_normal(outer);
      for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
      {
         Point3 a = all[triangles[t]];
         Point3 b = all[triangles[t + 1]];
         Point3 c = all[triangles[t + 2]];
         const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
         const std::array<double, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
         const std::array<double, 3> n{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
         if (n[0] * normal[0] + n[1] * normal[1] + n[2] * normal[2] < 0.0)
         {
            std::swap(b, c);
         }
         add_triangle(layer, a, b, c, id);
      }
      add_edges(layer, outer, id);
      for (const auto& hole : holes)
      {
         add_edges(layer, hole, id);
      }
   }
}

}  // namespace

bool looks_geographic(const tinyshp::Shapefile& file)
{
   const auto& b = file._header._bounds;
   return b._x_min >= -180.5 && b._x_max <= 180.5 && b._y_min >= -90.5 && b._y_max <= 90.5 && !file._shapes.empty();
}

std::optional<double> Layer::number(std::size_t shape, int field) const
{
   if (!file._attributes || field < 0 || static_cast<std::size_t>(field) >= field_count())
   {
      return std::nullopt;
   }
   const auto& value = file._attributes->_records[shape]._values[static_cast<std::size_t>(field)];
   if (const auto* number = std::get_if<double>(&value))
   {
      return *number;
   }
   return std::nullopt;
}

std::string Layer::text(std::size_t shape, int field) const
{
   if (!file._attributes || field < 0 || static_cast<std::size_t>(field) >= field_count())
   {
      return {};
   }
   return tinyshp::toString(file._attributes->_records[shape]._values[static_cast<std::size_t>(field)]);
}

int Layer::find_field(std::string_view field_name) const
{
   if (!file._attributes)
   {
      return -1;
   }
   const auto index = file._attributes->fieldIndex(field_name);
   return index ? static_cast<int>(*index) : -1;
}

void build(Layer& layer)
{
   const auto& header = layer.file._header;
   layer.has_z = tinyshp::hasZ(header._type);
   layer.geographic = layer.file.isGeographic() || (layer.file._projection.empty() && looks_geographic(layer.file));
   if (!layer.geographic)
   {
      layer.origin_x = (header._bounds._x_min + header._bounds._x_max) * 0.5;
      layer.origin_y = (header._bounds._y_min + header._bounds._y_max) * 0.5;
   }
   layer.origin_z = layer.has_z ? header._z_range._min : 0.0;

   const auto base = layer.base_type();
   const auto shape_count = layer.file._shapes.size();
   layer.shape_rings.assign(shape_count, {});
   layer.shape_box.assign(shape_count, 0);
   layer.shape_lines.assign(shape_count, {});
   layer.shape_triangles.assign(shape_count, {});
   layer.shape_edges.assign(shape_count, {});
   layer.shape_bounds3.assign(shape_count, {});
   layer.anchors.assign(shape_count, {});
   layer.priority.assign(shape_count, 0.0);

   for (std::size_t s = 0; s < shape_count; ++s)
   {
      const auto& shape = layer.file._shapes[s];
      if (shape.isNull())
      {
         continue;
      }

      // label anchor: the centroid of the largest ring, the middle of the longest line, the top of a 3D shape
      const auto& b = shape._bounds;
      layer.anchors[s] = {shape._points[0]._x, shape._points[0]._y, shape._z.empty() ? 0.0 : shape._z[0]};
      if (layer.has_z && base != tinyshp::ShapeType::Point && base != tinyshp::ShapeType::MultiPoint)
      {
         layer.anchors[s] = {(b._x_min + b._x_max) * 0.5, (b._y_min + b._y_max) * 0.5, shape._z_range._max};
         layer.priority[s] = shape._z_range._max;
      }
      else if (base == tinyshp::ShapeType::Polygon)
      {
         for (std::size_t p = 0; p < shape.partCount(); ++p)
         {
            const auto ring = shape.part(p);
            const double area = std::abs(tinyshp::signedArea(ring));
            if (area <= layer.priority[s] || ring.size() < 3)
            {
               continue;
            }
            // centroid relative to the first point, for precision
            double cx = 0.0;
            double cy = 0.0;
            double twice = 0.0;
            for (std::size_t i = 0; i + 1 < ring.size(); ++i)
            {
               const double ax = ring[i]._x - ring[0]._x;
               const double ay = ring[i]._y - ring[0]._y;
               const double bx = ring[i + 1]._x - ring[0]._x;
               const double by = ring[i + 1]._y - ring[0]._y;
               const double cross_product = ax * by - bx * ay;
               twice += cross_product;
               cx += (ax + bx) * cross_product;
               cy += (ay + by) * cross_product;
            }
            if (std::abs(twice) > 0.0)
            {
               layer.priority[s] = area;
               layer.anchors[s] = {ring[0]._x + cx / (3.0 * twice), ring[0]._y + cy / (3.0 * twice), 0.0};
            }
         }
      }
      else if (base == tinyshp::ShapeType::PolyLine)
      {
         for (std::size_t p = 0; p < shape.partCount(); ++p)
         {
            const auto line = shape.part(p);
            double length_sum = 0.0;
            for (std::size_t i = 0; i + 1 < line.size(); ++i)
            {
               length_sum += std::hypot(line[i + 1]._x - line[i]._x, line[i + 1]._y - line[i]._y);
            }
            if (length_sum > layer.priority[s])
            {
               layer.priority[s] = length_sum;
               const auto& middle = line[line.size() / 2];
               layer.anchors[s] = {middle._x, middle._y, 0.0};
            }
         }
      }

      if (base == tinyshp::ShapeType::Polygon)
      {
         layer.shape_rings[s].first = static_cast<uint32_t>(layer.rings.size());
         for (std::size_t p = 0; p < shape.partCount(); ++p)
         {
            const auto [first, last] = shape.partRange(p);
            if (last - first < 3)
            {
               continue;
            }
            layer.rings.push_back({static_cast<uint32_t>(layer.ring_vertices.size() / 2), static_cast<uint32_t>(last - first)});
            for (std::size_t i = first; i < last; ++i)
            {
               layer.ring_vertices.push_back(static_cast<float>(shape._points[i]._x - layer.origin_x));
               layer.ring_vertices.push_back(static_cast<float>(shape._points[i]._y - layer.origin_y));
            }
         }
         layer.shape_rings[s].count = static_cast<uint32_t>(layer.rings.size()) - layer.shape_rings[s].first;

         // the bounding box, drawn as a fan to cover the stencil
         layer.shape_box[s] = static_cast<uint32_t>(layer.ring_vertices.size() / 2);
         for (const auto& [x, y] : {std::pair{b._x_min, b._y_min}, {b._x_max, b._y_min}, {b._x_max, b._y_max}, {b._x_min, b._y_max}})
         {
            layer.ring_vertices.push_back(static_cast<float>(x - layer.origin_x));
            layer.ring_vertices.push_back(static_cast<float>(y - layer.origin_y));
         }
      }

      if (base == tinyshp::ShapeType::Polygon || base == tinyshp::ShapeType::PolyLine)
      {
         layer.shape_lines[s].first = static_cast<uint32_t>(layer.line_vertices.size() / 2);
         for (std::size_t p = 0; p < shape.partCount(); ++p)
         {
            const auto [first, last] = shape.partRange(p);
            add_line(layer, shape, first, last);
         }
         layer.shape_lines[s].count = static_cast<uint32_t>(layer.line_vertices.size() / 2) - layer.shape_lines[s].first;
      }

      if (layer.has_z)
      {
         const float id = static_cast<float>(s);
         layer.shape_triangles[s].first = static_cast<uint32_t>(layer.mesh_vertices.size() / 7);
         layer.shape_edges[s].first = static_cast<uint32_t>(layer.edge_vertices.size() / 4);
         if (base == tinyshp::ShapeType::MultiPatch || base == tinyshp::ShapeType::Polygon)
         {
            add_solid(layer, shape, id);
         }
         else if (base == tinyshp::ShapeType::PolyLine)
         {
            for (std::size_t p = 0; p < shape.partCount(); ++p)
            {
               const auto [first, last] = shape.partRange(p);
               std::vector<Point3> line;
               for (std::size_t i = first; i < last; ++i)
               {
                  line.push_back(point3(layer, shape, i));
               }
               add_edges(layer, line, id);
            }
         }
         layer.shape_triangles[s].count = static_cast<uint32_t>(layer.mesh_vertices.size() / 7) - layer.shape_triangles[s].first;
         layer.shape_edges[s].count = static_cast<uint32_t>(layer.edge_vertices.size() / 4) - layer.shape_edges[s].first;

         auto& bounds = layer.shape_bounds3[s];
         bounds = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
         for (std::size_t i = 0; i < shape._points.size(); ++i)
         {
            const auto p = point3(layer, shape, i);
            for (std::size_t k = 0; k < 3; ++k)
            {
               bounds[k] = std::min(bounds[k], static_cast<float>(p[k]));
               bounds[k + 3] = std::max(bounds[k + 3], static_cast<float>(p[k]));
            }
         }
      }
   }

   using Attributes = std::vector<std::pair<GLint, std::size_t>>;
   if (!layer.ring_vertices.empty())
   {
      upload(layer.ring_vbo, layer.ring_vao, layer.ring_vertices, Attributes{{2, 0}});
   }
   if (!layer.line_vertices.empty())
   {
      upload(layer.line_vbo, layer.line_vao, layer.line_vertices, Attributes{{2, 0}});
   }
   if (!layer.mesh_vertices.empty())
   {
      upload(layer.mesh_vbo, layer.mesh_vao, layer.mesh_vertices, Attributes{{3, 0}, {3, 1}, {1, 2}});
   }
   if (!layer.edge_vertices.empty())
   {
      upload(layer.edge_vbo, layer.edge_vao, layer.edge_vertices, Attributes{{3, 0}, {1, 2}});
   }

   apply_style(layer);
}

void apply_style(Layer& layer)
{
   const auto& style = layer.style;
   const auto shape_count = layer.file._shapes.size();
   const Rgba missing{0.35f, 0.36f, 0.38f, 1.0f};
   layer.colors.assign(shape_count, style.fill);
   layer.legend = {};

   if (style.mode == ColorMode::Graduated && style.field >= 0)
   {
      std::vector<std::optional<double>> values(shape_count);
      double low = 1e300;
      double high = -1e300;
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         auto value = layer.number(s, style.field);
         if (value && style.log_scale)
         {
            value = *value > 0.0 ? std::optional{std::log10(*value)} : std::nullopt;
         }
         if (value)
         {
            low = std::min(low, *value);
            high = std::max(high, *value);
         }
         values[s] = value;
      }
      const auto& ramp = ramps[static_cast<std::size_t>(std::clamp(style.ramp, 0, static_cast<int>(ramps.size()) - 1))];
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         if (!values[s])
         {
            layer.colors[s] = missing;
            continue;
         }
         float t = high > low ? static_cast<float>((*values[s] - low) / (high - low)) : 0.5f;
         layer.colors[s] = sample(ramp, style.invert ? 1.0f - t : t);
      }
      layer.legend.minimum = style.log_scale ? std::pow(10.0, low) : low;
      layer.legend.maximum = style.log_scale ? std::pow(10.0, high) : high;
   }
   else if (style.mode == ColorMode::Categories && style.field >= 0)
   {
      std::map<std::string, std::size_t> counts;
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         ++counts[layer.text(s, style.field)];
      }
      std::vector<std::pair<std::string, std::size_t>> ordered(counts.begin(), counts.end());
      std::ranges::stable_sort(ordered, [](const auto& a, const auto& b) { return a.second > b.second; });
      std::map<std::string, Rgba> palette;
      for (std::size_t i = 0; i < ordered.size(); ++i)
      {
         // ten colours; the rarest values share grey
         const auto color = i < 10 ? category(i) : missing;
         palette[ordered[i].first] = color;
         if (i < 10)
         {
            layer.legend.categories.emplace_back(ordered[i].first.empty() ? "(empty)" : ordered[i].first, color);
         }
      }
      layer.legend.more_categories = ordered.size() > 10 ? ordered.size() - 10 : 0;
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         layer.colors[s] = palette[layer.text(s, style.field)];
      }
   }
   for (auto& color : layer.colors)
   {
      color.a *= style.opacity;
   }

   // point sizes: area proportional to the size field
   layer.sizes.assign(shape_count, style.point_size);
   if (style.size_field >= 0)
   {
      double high = 0.0;
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         high = std::max(high, layer.number(s, style.size_field).value_or(0.0));
      }
      for (std::size_t s = 0; s < shape_count && high > 0.0; ++s)
      {
         const double t = std::sqrt(std::max(0.0, layer.number(s, style.size_field).value_or(0.0)) / high);
         layer.sizes[s] = style.point_size * static_cast<float>(0.5 + 3.0 * t);
      }
   }

   if (layer.is_point())
   {
      std::vector<float> points;
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         const auto& shape = layer.file._shapes[s];
         const auto& c = layer.colors[s];
         for (std::size_t i = 0; i < shape._points.size(); ++i)
         {
            const double z = shape._z.empty() ? 0.0 : shape._z[i] - layer.origin_z;
            points.insert(
               points.end(),
               {static_cast<float>(shape._points[i]._x - layer.origin_x),
                static_cast<float>(shape._points[i]._y - layer.origin_y),
                static_cast<float>(z),
                c.r,
                c.g,
                c.b,
                c.a,
                layer.sizes[s]}
            );
         }
      }
      layer.point_count = static_cast<GLsizei>(points.size() / 8);
      if (!points.empty())
      {
         upload(layer.point_vbo, layer.point_vao, points, std::vector<std::pair<GLint, std::size_t>>{{3, 0}, {4, 1}, {1, 2}});
      }
   }

   if (layer.has_z && shape_count > 0)
   {
      const auto rows = (shape_count + color_texture_width - 1) / color_texture_width;
      std::vector<uint8_t> texels(rows * color_texture_width * 4, 0);
      for (std::size_t s = 0; s < shape_count; ++s)
      {
         const auto& c = layer.colors[s];
         texels[s * 4 + 0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
         texels[s * 4 + 1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
         texels[s * 4 + 2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
         texels[s * 4 + 3] = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f);
      }
      if (layer.color_texture.get() == 0)
      {
         GLuint name = 0;
         glGenTextures(1, &name);
         layer.color_texture = Texture{name};
      }
      glBindTexture(GL_TEXTURE_2D, layer.color_texture.get());
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, color_texture_width, static_cast<GLsizei>(rows), 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
}

}  // namespace testbed
