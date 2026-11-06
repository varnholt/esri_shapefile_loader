#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace testbed
{

namespace
{

Layer* load(App& app, std::string_view file)
{
   return app.add_layer(app.data_dir / file);
}

void world_population(App& app)
{
   if (auto* countries = load(app, "naturalearth/ne_50m_admin_0_countries.shp"))
   {
      countries->style.mode = ColorMode::Graduated;
      countries->style.field = countries->find_field("POP_EST");
      countries->style.ramp = 1;
      countries->style.log_scale = true;
      countries->style.line = {0.02f, 0.02f, 0.03f, 0.55f};
   }
   if (auto* lakes = load(app, "naturalearth/ne_110m_lakes.shp"))
   {
      lakes->style.fill = {0.06f, 0.13f, 0.21f, 1.0f};
      lakes->style.show_lines = false;
   }
   if (auto* rivers = load(app, "naturalearth/ne_110m_rivers_lake_centerlines.shp"))
   {
      rivers->style.fill = {0.35f, 0.6f, 0.95f, 0.8f};
   }
   if (auto* places = load(app, "naturalearth/ne_110m_populated_places_simple.shp"))
   {
      places->style.fill = {0.55f, 0.85f, 1.0f, 0.9f};
      places->style.point_size = 3.0f;
      places->style.size_field = places->find_field("pop_max");
      places->style.label_field = places->find_field("name");
      places->style.label_count = 30;
   }
   app.view = ViewMode::Globe;
   app.globe.longitude = 60.0f;
   app.globe.latitude = 25.0f;
}

void time_zones(App& app)
{
   if (auto* zones = load(app, "naturalearth/ne_10m_time_zones.shp"))
   {
      zones->style.mode = ColorMode::Graduated;
      zones->style.field = zones->find_field("zone");
      zones->style.ramp = 4;
      zones->style.opacity = 0.85f;
      zones->style.line = {0.0f, 0.0f, 0.0f, 0.5f};
   }
   if (auto* countries = load(app, "naturalearth/ne_50m_admin_0_countries.shp"))
   {
      countries->style.show_fill = false;
      countries->style.line = {1.0f, 1.0f, 1.0f, 0.4f};
   }
   if (auto* places = load(app, "naturalearth/ne_110m_populated_places_simple.shp"))
   {
      places->style.fill = {1.0f, 1.0f, 1.0f, 0.9f};
      places->style.point_size = 2.5f;
      places->style.size_field = places->find_field("pop_max");
      places->style.label_field = places->find_field("name");
      places->style.label_count = 25;
   }
   app.view = ViewMode::Globe;
   app.globe.longitude = -40.0f;
   app.globe.latitude = 20.0f;
}

void continents(App& app)
{
   if (auto* countries = load(app, "naturalearth/ne_50m_admin_0_countries.shp"))
   {
      countries->style.mode = ColorMode::Categories;
      countries->style.field = countries->find_field("CONTINENT");
      countries->style.line = {0.05f, 0.05f, 0.07f, 0.7f};
   }
   if (auto* places = load(app, "naturalearth/ne_110m_populated_places_simple.shp"))
   {
      places->style.fill = {1.0f, 1.0f, 1.0f, 0.9f};
      places->style.point_size = 2.5f;
      places->style.size_field = places->find_field("pop_max");
      places->style.label_field = places->find_field("name");
      places->style.label_count = 45;
   }
   app.view = ViewMode::Map;
}

void income(App& app)
{
   if (auto* countries = load(app, "naturalearth/ne_50m_admin_0_countries.shp"))
   {
      countries->style.mode = ColorMode::Categories;
      countries->style.field = countries->find_field("INCOME_GRP");
      countries->style.line = {0.05f, 0.05f, 0.07f, 0.7f};
   }
   app.view = ViewMode::Map;
}

void cambridge(App& app)
{
   if (auto* buildings = load(app, "cambridge/cambridge_kendall_buildings.shp"))
   {
      buildings->style.mode = ColorMode::Graduated;
      buildings->style.field = buildings->find_field("HEIGHT_FT");
      buildings->style.ramp = 0;
      buildings->style.label_field = buildings->find_field("NAME");
      buildings->style.label_count = 14;
   }
   app.view = ViewMode::Scene;
   app.scene_camera.yaw = -2.0f;
   app.scene_camera.pitch = 0.6f;
   app.scene_camera.distance = 0.5f;
}

constexpr std::array<SceneInfo, 5> scene_list{{
   {"World population",
    "Natural Earth 1:50m countries by population estimate (log scale), 1:110m lakes, rivers and populated places sized by "
    "population.",
    world_population},
   {"Time zones", "Natural Earth 1:10m time zones by UTC offset, with 1:50m country borders.", time_zones},
   {"Continents", "Natural Earth 1:50m countries by continent, with the largest cities.", continents},
   {"Income groups", "Natural Earth 1:50m countries by World Bank income group.", income},
   {"Cambridge 3D buildings",
    "709 MultiPatch buildings around Kendall Square and MIT from the City of Cambridge 3D model, in Massachusetts state "
    "plane feet, coloured by height.",
    cambridge},
}};

double distance_to_segment(double px, double py, double ax, double ay, double bx, double by)
{
   const double dx = bx - ax;
   const double dy = by - ay;
   const double length2 = dx * dx + dy * dy;
   const double t = length2 > 0.0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length2, 0.0, 1.0) : 0.0;
   const double ex = ax + t * dx - px;
   const double ey = ay + t * dy - py;
   return std::sqrt(ex * ex + ey * ey);
}

/// \brief the shape of `layer` at (x, y) in its file coordinates, within `tolerance` for points and lines.
std::optional<std::size_t> pick_2d(const Layer& layer, double x, double y, double tolerance)
{
   const tinyshp::Point point{x, y};
   std::optional<std::size_t> best;
   double best_distance = std::numeric_limits<double>::max();
   for (std::size_t s = 0; s < layer.file._shapes.size(); ++s)
   {
      const auto& shape = layer.file._shapes[s];
      if (shape.isNull())
      {
         continue;
      }
      const auto& b = shape._bounds;
      const double grow = layer.is_point() ? tolerance * 4.0 : tolerance;
      if (x < b._x_min - grow || x > b._x_max + grow || y < b._y_min - grow || y > b._y_max + grow)
      {
         continue;
      }
      if (layer.is_polygon())
      {
         if (tinyshp::contains(shape, point))
         {
            return s;  // polygons do not overlap within a layer, the first hit is it
         }
      }
      else if (layer.is_line())
      {
         for (std::size_t p = 0; p < shape.partCount(); ++p)
         {
            const auto [first, last] = shape.partRange(p);
            for (std::size_t i = first; i + 1 < last; ++i)
            {
               const auto& a = shape._points[i];
               const auto& c = shape._points[i + 1];
               const double d = distance_to_segment(x, y, a._x, a._y, c._x, c._y);
               if (d < tolerance && d < best_distance)
               {
                  best = s;
                  best_distance = d;
               }
            }
         }
      }
      else
      {
         const double radius = tolerance * std::max(1.0, static_cast<double>(layer.sizes[s]) / 4.0);
         for (const auto& p : shape._points)
         {
            const double d = std::hypot(p._x - x, p._y - y);
            if (d < radius && d < best_distance)
            {
               best = s;
               best_distance = d;
            }
         }
      }
   }
   return best;
}

bool ray_box(Vec3 origin, Vec3 direction, Vec3 low, Vec3 high)
{
   float t_near = 0.0f;
   float t_far = 1e30f;
   const std::array<float, 3> o{origin.x, origin.y, origin.z};
   const std::array<float, 3> d{direction.x, direction.y, direction.z};
   const std::array<float, 3> lo{low.x, low.y, low.z};
   const std::array<float, 3> hi{high.x, high.y, high.z};
   for (std::size_t k = 0; k < 3; ++k)
   {
      if (std::abs(d[k]) < 1e-12f)
      {
         if (o[k] < lo[k] || o[k] > hi[k])
         {
            return false;
         }
         continue;
      }
      float t0 = (lo[k] - o[k]) / d[k];
      float t1 = (hi[k] - o[k]) / d[k];
      if (t0 > t1)
      {
         std::swap(t0, t1);
      }
      t_near = std::max(t_near, t0);
      t_far = std::min(t_far, t1);
      if (t_near > t_far)
      {
         return false;
      }
   }
   return true;
}

/// \brief Moeller-Trumbore; the distance along the ray or nothing.
std::optional<float> ray_triangle(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c)
{
   const Vec3 e1 = b - a;
   const Vec3 e2 = c - a;
   const Vec3 p = cross(direction, e2);
   const float det = dot(e1, p);
   if (std::abs(det) < 1e-12f)
   {
      return std::nullopt;
   }
   const float inv = 1.0f / det;
   const Vec3 s = origin - a;
   const float u = dot(s, p) * inv;
   if (u < 0.0f || u > 1.0f)
   {
      return std::nullopt;
   }
   const Vec3 q = cross(s, e1);
   const float v = dot(direction, q) * inv;
   if (v < 0.0f || u + v > 1.0f)
   {
      return std::nullopt;
   }
   const float t = dot(e2, q) * inv;
   return t > 0.0f ? std::optional{t} : std::nullopt;
}

}  // namespace

std::span<const SceneInfo> scenes()
{
   return scene_list;
}

float App::framebuffer_scale() const
{
   int window_width = 1;
   int framebuffer_width = 1;
   int unused = 0;
   glfwGetWindowSize(window, &window_width, &unused);
   glfwGetFramebufferSize(window, &framebuffer_width, &unused);
   return window_width > 0 ? static_cast<float>(framebuffer_width) / static_cast<float>(window_width) : 1.0f;
}

Layer* App::add_layer(const std::filesystem::path& shp)
{
   const auto start = std::chrono::steady_clock::now();
   auto file = tinyshp::loadFile(shp);
   std::string warning;
   if (!file && file.error()._code == tinyshp::ErrorCode::RecordCountMismatch)
   {
      // keep the geometry, which is still useful
      warning = tinyshp::describe(file.error()) + "; attributes skipped";
      file = tinyshp::loadFile(shp, {._load_attributes = false});
   }
   const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
   const auto name = shp.filename().string();
   if (!file)
   {
      messages.push_back(std::format("{}: {}", name, tinyshp::describe(file.error())));
      return nullptr;
   }

   auto layer = std::make_unique<Layer>();
   layer->name = name;
   layer->file = std::move(*file);
   layer->load_milliseconds = milliseconds;
   layer->warning = warning;
   build(*layer);
   messages.push_back(std::format(
      "{}: {} {} records in {:.1f} ms{}", name, layer->file._shapes.size(), tinyshp::describe(layer->file._header._type), milliseconds,
      warning.empty() ? "" : " (" + warning + ")"
   ));
   layers.push_back(std::move(layer));
   return layers.back().get();
}

void App::load_scene(int index)
{
   layers.clear();
   messages.clear();
   hover = {};
   scene = index;
   scene_camera = {};
   globe = {};
   const auto& info = scenes()[static_cast<std::size_t>(index)];
   scene_description = info.description;
   info.setup(*this);
   for (auto& layer : layers)
   {
      apply_style(*layer);
   }
   fit(false);
   renderer->invalidate_globe();
}

void App::open(const std::vector<std::filesystem::path>& paths)
{
   // every .shp in the given files and directories; the other parts are found next to it
   std::set<std::filesystem::path> shps;
   const auto consider = [&](const std::filesystem::path& path)
   {
      auto extension = path.extension().string();
      std::ranges::transform(extension, extension.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
      if (extension == ".shp")
      {
         shps.insert(path);
      }
      else if (extension == ".dbf" || extension == ".prj" || extension == ".shx" || extension == ".cpg")
      {
         // a dropped .dbf without its .shp still points at it
         for (const auto* candidate : {".shp", ".SHP"})
         {
            auto shp = path;
            shp.replace_extension(candidate);
            std::error_code error;
            if (std::filesystem::is_regular_file(shp, error))
            {
               shps.insert(shp);
               break;
            }
         }
      }
   };
   for (const auto& path : paths)
   {
      std::error_code error;
      if (std::filesystem::is_directory(path, error))
      {
         for (const auto& entry : std::filesystem::recursive_directory_iterator(path, error))
         {
            consider(entry.path());
         }
      }
      else
      {
         consider(path);
      }
   }

   layers.clear();
   messages.clear();
   hover = {};
   scene = -1;
   scene_description = "Your files.";
   if (shps.empty())
   {
      messages.emplace_back("no .shp found; a shapefile needs at least its .shp, ideally with the .dbf and .prj");
   }
   for (const auto& shp : shps)
   {
      if (auto* layer = add_layer(shp))
      {
         // readable defaults: graduated by the numeric field with the most distinct values (ids aside), labels from a
         // name field
         const auto& fields = layer->file._attributes ? layer->file._attributes->_fields : std::vector<tinyshp::Field>{};
         std::size_t most = 1;
         for (std::size_t f = 0; f < fields.size() && (layer->is_polygon() || layer->has_z); ++f)
         {
            auto name = fields[f]._name;
            std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
            const auto type = fields[f]._type;
            if ((type != tinyshp::FieldType::Number && type != tinyshp::FieldType::Float) || name.find("ID") != std::string::npos)
            {
               continue;
            }
            std::set<double> distinct;
            for (std::size_t s = 0; s < layer->file._shapes.size(); ++s)
            {
               if (const auto value = layer->number(s, static_cast<int>(f)))
               {
                  distinct.insert(*value);
               }
            }
            if (distinct.size() > most)
            {
               most = distinct.size();
               layer->style.mode = ColorMode::Graduated;
               layer->style.field = static_cast<int>(f);
               // heavy tailed values like populations read better on a log scale
               layer->style.log_scale = *distinct.begin() > 0.0 && *distinct.rbegin() / *distinct.begin() > 1000.0;
            }
         }
         for (const auto* name : {"NAME", "NAME_EN", "LABEL", "TITLE"})
         {
            if (layer->style.label_field < 0)
            {
               layer->style.label_field = layer->find_field(name);
            }
         }
         apply_style(*layer);
      }
   }
   fit(true);
   renderer->invalidate_globe();
}

bool App::can_globe() const
{
   return std::ranges::any_of(layers, [](const auto& layer) { return layer->geographic; });
}

bool App::can_scene() const
{
   return std::ranges::any_of(layers, [](const auto& layer) { return layer->has_z; });
}

void App::fit(bool choose_view)
{
   if (choose_view)
   {
      view = can_scene() ? ViewMode::Scene : can_globe() ? ViewMode::Globe : ViewMode::Map;
   }

   // the map: everything visible, in file coordinates
   double x0 = 1e300;
   double y0 = 1e300;
   double x1 = -1e300;
   double y1 = -1e300;
   for (const auto& layer : layers)
   {
      if (!layer->file._shapes.empty())
      {
         const auto& b = layer->file._header._bounds;
         x0 = std::min(x0, b._x_min);
         y0 = std::min(y0, b._y_min);
         x1 = std::max(x1, b._x_max);
         y1 = std::max(y1, b._y_max);
      }
   }
   if (x0 > x1)
   {
      x0 = -180;
      y0 = -90;
      x1 = 180;
      y1 = 90;
   }
   int width = 1280;
   int height = 720;
   if (window != nullptr)
   {
      glfwGetFramebufferSize(window, &width, &height);
   }
   map.center_x = (x0 + x1) * 0.5;
   map.center_y = (y0 + y1) * 0.5;
   map.units_per_pixel = std::max({(x1 - x0) / std::max(width, 1), (y1 - y0) / std::max(height, 1), 1e-9}) * 1.08;

   if (choose_view && view == ViewMode::Globe)
   {
      globe.longitude = static_cast<float>(map.center_x);
      globe.latitude = std::clamp(static_cast<float>(map.center_y), -60.0f, 60.0f);
      globe.distance = (x1 - x0) > 120.0 ? 3.4f : 2.2f;
   }

   const auto bounds = Renderer::scene_bounds(layers);
   scene_camera.target = {(bounds[0] + bounds[3]) * 0.5f, (bounds[1] + bounds[4]) * 0.5f, bounds[2] * scene_camera.z_scale};
}

void App::restyle(Layer& layer)
{
   apply_style(layer);
   renderer->invalidate_globe();
}

void App::update_hover()
{
   hover = {};
   hover_position.reset();
   if (mouse_x < 0.0f || !renderer)
   {
      return;
   }
   const auto& projection = renderer->projection();
   const float scale = framebuffer_scale();
   const float px = mouse_x * scale;
   const float py = mouse_y * scale;

   if (view == ViewMode::Map)
   {
      const double x = map.center_x + (px - projection.width * 0.5) * map.units_per_pixel;
      const double y = map.center_y - (py - projection.height * 0.5) * map.units_per_pixel;
      hover_position = std::array<double, 3>{x, y, 0.0};
      for (auto it = layers.rbegin(); it != layers.rend(); ++it)
      {
         const auto& layer = **it;
         if (layer.visible && (layer.style.show_fill || !layer.is_polygon()))
         {
            if (const auto shape = pick_2d(layer, x, y, 5.0 * scale * map.units_per_pixel))
            {
               hover = {&layer, *shape};
               return;
            }
         }
      }
      return;
   }

   if (view == ViewMode::Globe)
   {
      const Vec3 origin = projection.eye;
      const Vec3 direction = projection.ray(px, py);
      const float b = dot(origin, direction);
      const float c = dot(origin, origin) - 1.0f;
      const float discriminant = b * b - c;
      if (discriminant < 0.0f)
      {
         return;
      }
      const Vec3 p = origin + direction * (-b - std::sqrt(discriminant));
      const double longitude = std::atan2(p.x, p.z) * 180.0 / pi;
      const double latitude = std::asin(std::clamp(p.y, -1.0f, 1.0f)) * 180.0 / pi;
      hover_position = std::array<double, 3>{longitude, latitude, 0.0};
      const double degrees_per_pixel = 2.0 * projection.tan_half_fov * (globe.distance - 1.0) / projection.height * 180.0 / pi;
      for (auto it = layers.rbegin(); it != layers.rend(); ++it)
      {
         const auto& layer = **it;
         if (layer.visible && layer.geographic && (layer.style.show_fill || !layer.is_polygon()))
         {
            if (const auto shape = pick_2d(layer, longitude, latitude, 5.0 * scale * degrees_per_pixel))
            {
               hover = {&layer, *shape};
               return;
            }
         }
      }
      return;
   }

   // 3D: the nearest triangle under the mouse
   const Vec3 origin = projection.eye;
   const Vec3 direction = projection.ray(px, py);
   const Layer* reference = nullptr;
   float nearest = 1e30f;
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
      const float zs = scene_camera.z_scale;
      const auto vertex = [&](uint32_t i)
      {
         const float* v = &layer->mesh_vertices[static_cast<std::size_t>(i) * 7];
         return Vec3{v[0] + offset.x, v[1] + offset.y, (v[2] + offset.z) * zs};
      };
      for (std::size_t s = 0; s < layer->shape_triangles.size(); ++s)
      {
         const auto& range = layer->shape_triangles[s];
         const auto& b = layer->shape_bounds3[s];
         if (range.count == 0 ||
             !ray_box(origin, direction, {b[0] + offset.x, b[1] + offset.y, (b[2] + offset.z) * zs}, {b[3] + offset.x, b[4] + offset.y, (b[5] + offset.z) * zs}))
         {
            continue;
         }
         for (uint32_t i = range.first; i + 2 < range.first + range.count; i += 3)
         {
            if (const auto t = ray_triangle(origin, direction, vertex(i), vertex(i + 1), vertex(i + 2)); t && *t < nearest)
            {
               nearest = *t;
               hover = {layer.get(), s};
            }
         }
      }
   }
   if (hover.layer != nullptr)
   {
      const Vec3 p = origin + direction * nearest;
      hover_position = std::array<double, 3>{p.x + reference->origin_x, p.y + reference->origin_y, p.z / scene_camera.z_scale + reference->origin_z};
   }
}

}  // namespace testbed
