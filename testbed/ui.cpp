#include "app.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace testbed
{

namespace
{

void text(const std::string& line)
{
   ImGui::TextUnformatted(line.c_str());
}

ImU32 to_imgui(Rgba color, float alpha = 1.0f)
{
   return ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, color.a * alpha});
}

std::string compact(double value)
{
   const double magnitude = std::abs(value);
   if (magnitude >= 1e9)
   {
      return std::format("{:.1f}B", value / 1e9);
   }
   if (magnitude >= 1e6)
   {
      return std::format("{:.1f}M", value / 1e6);
   }
   if (magnitude >= 1e4)
   {
      return std::format("{:.0f}k", value / 1e3);
   }
   return std::format("{:.4g}", value);
}

/// \brief a combo over the .dbf fields; returns true if the choice changed. -1 is "none".
bool field_combo(const char* label, const Layer& layer, int& field, bool numeric_only)
{
   const auto& fields = layer.file._attributes->_fields;
   const std::string current = field >= 0 ? fields[static_cast<std::size_t>(field)]._name : "(none)";
   bool changed = false;
   if (ImGui::BeginCombo(label, current.c_str()))
   {
      if (ImGui::Selectable("(none)", field < 0))
      {
         changed = field >= 0;
         field = -1;
      }
      for (std::size_t f = 0; f < fields.size(); ++f)
      {
         const bool numeric = fields[f]._type == tinyshp::FieldType::Number || fields[f]._type == tinyshp::FieldType::Float ||
                              fields[f]._type == tinyshp::FieldType::Integer || fields[f]._type == tinyshp::FieldType::Double;
         if (numeric_only && !numeric)
         {
            continue;
         }
         const auto name = std::format("{} ({})", fields[f]._name, static_cast<char>(fields[f]._type));
         if (ImGui::Selectable(name.c_str(), static_cast<int>(f) == field))
         {
            changed = changed || field != static_cast<int>(f);
            field = static_cast<int>(f);
         }
      }
      ImGui::EndCombo();
   }
   return changed;
}

void legend(const Layer& layer)
{
   const auto& style = layer.style;
   if (style.mode == ColorMode::Graduated && style.field >= 0)
   {
      const auto& ramp = ramps[static_cast<std::size_t>(style.ramp)];
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const float width = 220.0f * ImGui::GetStyle().FontScaleDpi;
      const float height = ImGui::GetTextLineHeight();
      auto* draw = ImGui::GetWindowDrawList();
      constexpr int steps = 32;
      for (int i = 0; i < steps; ++i)
      {
         const float t = static_cast<float>(i) / (steps - 1);
         const float x0 = start.x + width * static_cast<float>(i) / steps;
         draw->AddRectFilled({x0, start.y}, {x0 + width / steps + 1.0f, start.y + height}, to_imgui(sample(ramp, style.invert ? 1.0f - t : t)));
      }
      ImGui::Dummy({width, height});
      text(std::format("{} .. {}{}", compact(layer.legend.minimum), compact(layer.legend.maximum), style.log_scale ? " (log)" : ""));
   }
   else if (style.mode == ColorMode::Categories && style.field >= 0)
   {
      for (const auto& [value, color] : layer.legend.categories)
      {
         ImGui::ColorButton(("##" + value).c_str(), {color.r, color.g, color.b, 1.0f}, ImGuiColorEditFlags_NoTooltip, {12, 12});
         ImGui::SameLine();
         text(value);
      }
      if (layer.legend.more_categories > 0)
      {
         text(std::format("and {} more, in grey", layer.legend.more_categories));
      }
   }
}

void layer_panel(App& app, Layer& layer, std::size_t index)
{
   ImGui::PushID(static_cast<int>(index));
   if (ImGui::Checkbox("##visible", &layer.visible))
   {
      app.renderer->invalidate_globe();
   }
   ImGui::SameLine();
   const auto title = std::format(
      "{}  ({} {}{})", layer.name, layer.file._shapes.size(), tinyshp::describe(layer.file._header._type), layer.geographic ? "" : ", projected"
   );
   if (ImGui::TreeNode("##layer", "%s", title.c_str()))
   {
      auto& style = layer.style;
      bool changed = false;
      if (!layer.warning.empty())
      {
         ImGui::TextColored({1.0f, 0.7f, 0.3f, 1.0f}, "%s", layer.warning.c_str());
      }
      text(std::format("loaded in {:.1f} ms, {} fields", layer.load_milliseconds, layer.field_count()));

      static constexpr const char* modes[] = {"single colour", "categories", "graduated"};
      int mode = static_cast<int>(style.mode);
      if (layer.field_count() > 0 && ImGui::Combo("colour by", &mode, modes, 3))
      {
         style.mode = static_cast<ColorMode>(mode);
         changed = true;
      }
      if (style.mode == ColorMode::Single)
      {
         changed = ImGui::ColorEdit4("colour", &style.fill.r, ImGuiColorEditFlags_NoInputs) || changed;
      }
      else
      {
         changed = field_combo("field", layer, style.field, style.mode == ColorMode::Graduated) || changed;
      }
      if (style.mode == ColorMode::Graduated)
      {
         if (ImGui::BeginCombo("ramp", std::string{ramps[static_cast<std::size_t>(style.ramp)].name}.c_str()))
         {
            for (std::size_t r = 0; r < ramps.size(); ++r)
            {
               if (ImGui::Selectable(std::string{ramps[r].name}.c_str(), static_cast<int>(r) == style.ramp))
               {
                  style.ramp = static_cast<int>(r);
                  changed = true;
               }
            }
            ImGui::EndCombo();
         }
         changed = ImGui::Checkbox("log scale", &style.log_scale) || changed;
         ImGui::SameLine();
         changed = ImGui::Checkbox("invert", &style.invert) || changed;
      }
      legend(layer);

      if (layer.is_polygon() || layer.has_z)
      {
         changed = ImGui::Checkbox("fill", &style.show_fill) || changed;
         ImGui::SameLine();
      }
      if (layer.has_z && !layer.mesh_vertices.empty())
      {
         ImGui::Checkbox("edges", &style.edges);
      }
      else if (!layer.is_point())
      {
         changed = ImGui::Checkbox("outlines", &style.show_lines) || changed;
         ImGui::SameLine();
         changed = ImGui::ColorEdit4("##line", &style.line.r, ImGuiColorEditFlags_NoInputs) || changed;
      }
      changed = ImGui::SliderFloat("opacity", &style.opacity, 0.0f, 1.0f) || changed;

      if (layer.is_point())
      {
         changed = ImGui::SliderFloat("point size", &style.point_size, 1.0f, 20.0f) || changed;
         if (layer.field_count() > 0)
         {
            changed = field_combo("size by", layer, style.size_field, true) || changed;
         }
      }
      if (layer.field_count() > 0 && (layer.is_point() || layer.has_z))
      {
         field_combo("labels", layer, style.label_field, false);
         if (style.label_field >= 0)
         {
            ImGui::SliderInt("label count", &style.label_count, 0, 200);
         }
      }
      if (changed)
      {
         app.restyle(layer);
      }
      ImGui::TreePop();
   }
   ImGui::PopID();
}

/// \brief screen position (window coordinates) of a point of a layer, or nothing if it is hidden.
std::optional<ImVec2> to_screen(const App& app, const Layer& layer, double x, double y, double z)
{
   const auto& projection = app.renderer->projection();
   const float scale = app.framebuffer_scale();
   if (app.view == ViewMode::Map)
   {
      const double sx = (x - app.map.center_x) / app.map.units_per_pixel + projection.width * 0.5;
      const double sy = projection.height * 0.5 - (y - app.map.center_y) / app.map.units_per_pixel;
      return ImVec2{static_cast<float>(sx) / scale, static_cast<float>(sy) / scale};
   }
   Vec3 p;
   if (app.view == ViewMode::Globe)
   {
      if (!layer.geographic)
      {
         return std::nullopt;
      }
      p = sphere_point(static_cast<float>(x), static_cast<float>(y)) * 1.002f;
      if (dot(p, projection.eye - p) <= 0.0f)
      {
         return std::nullopt;  // on the far side
      }
   }
   else
   {
      const Layer* reference = nullptr;
      for (const auto& candidate : app.layers)
      {
         if (candidate->has_z && candidate->visible)
         {
            reference = candidate.get();
            break;
         }
      }
      if (reference == nullptr || !layer.has_z)
      {
         return std::nullopt;
      }
      p = {static_cast<float>(x - reference->origin_x), static_cast<float>(y - reference->origin_y),
           static_cast<float>(z - reference->origin_z) * app.scene_camera.z_scale};
   }
   const auto ndc = project(projection.view_projection, p);
   if (ndc[3] <= 0.0f || std::abs(ndc[0]) > 1.1f || std::abs(ndc[1]) > 1.1f)
   {
      return std::nullopt;
   }
   return ImVec2{(ndc[0] * 0.5f + 0.5f) * static_cast<float>(projection.width) / scale,
                 (0.5f - ndc[1] * 0.5f) * static_cast<float>(projection.height) / scale};
}

void draw_labels(const App& app)
{
   auto* draw = ImGui::GetBackgroundDrawList();
   std::vector<ImVec4> taken;  // label rectangles, so labels do not overlap
   for (const auto& layer : app.layers)
   {
      const auto& style = layer->style;
      if (!layer->visible || style.label_field < 0)
      {
         continue;
      }
      // biggest first
      std::vector<std::size_t> order;
      for (std::size_t s = 0; s < layer->file._shapes.size(); ++s)
      {
         if (!layer->file._shapes[s].isNull())
         {
            order.push_back(s);
         }
      }
      // the biggest points or the tallest shapes get labels first
      if (layer->is_point())
      {
         std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return layer->sizes[a] > layer->sizes[b]; });
      }
      else
      {
         std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return layer->priority[a] > layer->priority[b]; });
      }
      order.resize(std::min(order.size(), static_cast<std::size_t>(std::max(style.label_count, 0))));
      for (const auto s : order)
      {
         const auto label = layer->text(s, style.label_field);
         if (label.empty())
         {
            continue;
         }
         const auto& anchor = layer->anchors[s];
         const auto position = to_screen(app, *layer, anchor[0], anchor[1], anchor[2]);
         if (!position)
         {
            continue;
         }
         const ImVec2 size = ImGui::CalcTextSize(label.c_str());
         // beside points, centred on areas
         const ImVec2 at = layer->is_point() ? ImVec2{position->x + 5.0f, position->y - size.y * 0.5f}
                                              : ImVec2{position->x - size.x * 0.5f, position->y - size.y * 0.5f};
         const ImVec4 rect{at.x - 2, at.y - 1, at.x + size.x + 2, at.y + size.y + 1};
         const bool overlaps = std::ranges::any_of(taken, [&](const ImVec4& r) { return rect.x < r.z && rect.z > r.x && rect.y < r.w && rect.w > r.y; });
         if (overlaps)
         {
            continue;
         }
         taken.push_back(rect);
         draw->AddText({at.x + 1, at.y + 1}, IM_COL32(0, 0, 0, 200), label.c_str());
         draw->AddText(at, IM_COL32(235, 240, 250, 255), label.c_str());
      }
   }
}

void tooltip(const App& app)
{
   const auto* layer = app.hover.layer;
   if (layer == nullptr || ImGui::GetIO().WantCaptureMouse)
   {
      return;
   }
   const auto& shape = layer->file._shapes[app.hover.shape];
   ImGui::BeginTooltip();
   ImGui::TextColored({1.0f, 0.86f, 0.25f, 1.0f}, "%s  record %d", layer->name.c_str(), shape._record_number);
   text(std::format("{}: {} parts, {} points", tinyshp::describe(shape._type), shape.partCount(), shape._points.size()));
   if (app.hover_position)
   {
      const auto& p = *app.hover_position;
      text(layer->geographic ? std::format("{:.3f}, {:.3f}", p[1], p[0])
                             : std::format("x {:.1f}  y {:.1f}{}", p[0], p[1], layer->has_z ? std::format("  z {:.1f}", p[2]) : ""));
   }
   if (layer->file._attributes)
   {
      ImGui::Separator();
      const auto& table = *layer->file._attributes;
      int shown = 0;
      for (std::size_t f = 0; f < table._fields.size() && shown < 14; ++f)
      {
         const auto value = tinyshp::toString(table._records[app.hover.shape]._values[f]);
         if (value.empty())
         {
            continue;
         }
         ImGui::TextDisabled("%s", table._fields[f]._name.c_str());
         ImGui::SameLine(110.0f * ImGui::GetStyle().FontScaleDpi);
         text(value.size() > 60 ? value.substr(0, 57) + "..." : value);
         ++shown;
      }
      if (shown == 14)
      {
         ImGui::TextDisabled("...");
      }
   }
   ImGui::EndTooltip();
}

}  // namespace

void draw_ui(App& app)
{
   if (app.labels)
   {
      draw_labels(app);
   }
   tooltip(app);
   if (!app.show_panel)
   {
      return;
   }

   ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowSizeConstraints({340.0f * ImGui::GetStyle().FontScaleDpi, 0.0f}, {600.0f * ImGui::GetStyle().FontScaleDpi, 10000.0f});
   ImGui::SetNextWindowBgAlpha(0.88f);
   ImGui::Begin("tinyshp", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

   const auto list = scenes();
   const char* current = app.scene >= 0 ? list[static_cast<std::size_t>(app.scene)].name : "Your files";
   if (ImGui::BeginCombo("scene", current))
   {
      for (std::size_t i = 0; i < list.size(); ++i)
      {
         if (ImGui::Selectable(list[i].name, static_cast<int>(i) == app.scene))
         {
            app.load_scene(static_cast<int>(i));
         }
      }
      ImGui::EndCombo();
   }
   ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f * ImGui::GetStyle().FontScaleDpi);
   ImGui::TextDisabled("%s", app.scene_description.c_str());
   ImGui::PopTextWrapPos();

   int view = static_cast<int>(app.view);
   ImGui::RadioButton("map", &view, 0);
   ImGui::SameLine();
   ImGui::BeginDisabled(!app.can_globe());
   ImGui::RadioButton("globe", &view, 1);
   ImGui::EndDisabled();
   ImGui::SameLine();
   ImGui::BeginDisabled(!app.can_scene());
   ImGui::RadioButton("3D", &view, 2);
   ImGui::EndDisabled();
   ImGui::SameLine();
   if (ImGui::Button("fit"))
   {
      app.fit(false);
   }
   app.view = static_cast<ViewMode>(view);

   if (app.view == ViewMode::Globe)
   {
      ImGui::Checkbox("spin", &app.globe.spin);
      ImGui::SameLine();
   }
   if (app.view != ViewMode::Scene)
   {
      ImGui::Checkbox("graticule", &app.graticule);
      ImGui::SameLine();
   }
   ImGui::Checkbox("labels", &app.labels);
   if (app.view == ViewMode::Scene)
   {
      ImGui::SliderFloat("z scale", &app.scene_camera.z_scale, 0.1f, 10.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
   }

   ImGui::SeparatorText("layers");
   for (std::size_t i = 0; i < app.layers.size(); ++i)
   {
      layer_panel(app, *app.layers[i], i);
   }

   ImGui::SeparatorText("your data");
#ifdef __EMSCRIPTEN__
   ImGui::TextWrapped("Open or drop .shp files with their .dbf and .prj, or a .zip; they stay in your browser.");
#else
   ImGui::TextWrapped("Drop .shp files (with their .dbf and .prj) or folders on the window.");
#endif
   for (const auto& message : app.messages)
   {
      ImGui::TextDisabled("%s", message.c_str());
   }

   if (app.show_fps)
   {
      ImGui::Separator();
      text(std::format("{:.0f} fps", ImGui::GetIO().Framerate));
   }
   ImGui::End();
}

}  // namespace testbed
