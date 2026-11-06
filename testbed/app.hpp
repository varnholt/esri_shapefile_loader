#pragma once

#include "gl.hpp"
#include "layer.hpp"
#include "renderer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace testbed
{

struct App;

struct SceneInfo
{
   const char* name;
   const char* description;
   void (*setup)(App&);
};

/// \brief the built-in scenes, from the data directory.
std::span<const SceneInfo> scenes();

struct App
{
   GLFWwindow* window = nullptr;
   std::optional<Renderer> renderer;
   std::filesystem::path data_dir;

   std::vector<std::unique_ptr<Layer>> layers;
   int scene = 0;  // index into scenes(), -1 for files the user opened
   std::string scene_description;
   std::vector<std::string> messages;  // load times and errors, shown in the panel

   ViewMode view = ViewMode::Globe;
   MapCamera map;
   GlobeCamera globe;
   SceneCamera scene_camera;
   bool graticule = true;
   bool labels = true;
   bool show_fps = true;
   bool show_panel = true;

   // input
   bool dragging = false;
   bool panning = false;
   double last_x = 0.0;
   double last_y = 0.0;
   float mouse_x = -1.0f;  // window coordinates
   float mouse_y = -1.0f;
   Highlight hover;
   std::optional<std::array<double, 3>> hover_position;  // where the mouse hits the data, in its coordinates

   void load_scene(int index);

   /// \brief replaces the layers with the shapefiles in `paths` (files or directories), for drag and drop and uploads.
   void open(const std::vector<std::filesystem::path>& paths);

   /// \brief loads one .shp with its .dbf, .prj and .cpg; reports to `messages`.
   Layer* add_layer(const std::filesystem::path& shp);

   /// \brief frames the cameras on the visible layers and picks a view that fits the data.
   void fit(bool choose_view);

   void restyle(Layer& layer);
   void update_hover();

   [[nodiscard]] bool can_globe() const;
   [[nodiscard]] bool can_scene() const;

   /// \brief framebuffer pixels per window coordinate.
   [[nodiscard]] float framebuffer_scale() const;
};

void draw_ui(App& app);

}  // namespace testbed
