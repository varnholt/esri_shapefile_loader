// tinyshp testbed: shows shapefiles on a map, a globe or in 3D, with their attributes.
//
//   tinyshp_testbed [file.shp | folder ...] [--scene NAME|INDEX] [--view map|globe|3d]
//                   [--screenshot out.png [--ui] [--mouse X,Y]] [--size WxH]
//
// Mouse: drag to pan or rotate, right drag to pan in 3D, wheel to zoom, hover for attributes.
// Keys: 1/2/3 map, globe, 3D; F fit; L labels; Space spin; Esc quit. Drop shapefiles or folders on the window.

#include "app.hpp"
#include "gl.hpp"
#include "renderer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace
{

using testbed::App;
using testbed::ViewMode;

App* app_from(GLFWwindow* window)
{
   return static_cast<App*>(glfwGetWindowUserPointer(window));
}

// command line arguments and dropped paths are utf-8
std::filesystem::path utf8_path(std::string_view text)
{
   return std::filesystem::path{std::u8string(text.begin(), text.end())};
}

// the browser hands uploads to this through a directory in the in-memory file system
#ifdef __EMSCRIPTEN__
App* web_app = nullptr;
#endif

void on_key(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
   if (action == GLFW_RELEASE || ImGui::GetIO().WantCaptureKeyboard)
   {
      return;
   }
   auto& app = *app_from(window);
   switch (key)
   {
      case GLFW_KEY_1: app.view = ViewMode::Map; break;
      case GLFW_KEY_2: app.view = app.can_globe() ? ViewMode::Globe : app.view; break;
      case GLFW_KEY_3: app.view = app.can_scene() ? ViewMode::Scene : app.view; break;
      case GLFW_KEY_F: app.fit(false); break;
      case GLFW_KEY_L: app.labels = !app.labels; break;
      case GLFW_KEY_SPACE: app.globe.spin = !app.globe.spin; break;
      case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
      default: break;
   }
}

void on_mouse_button(GLFWwindow* window, int button, int action, int /*mods*/)
{
   auto& app = *app_from(window);
   const bool press = action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse;
   if (button == GLFW_MOUSE_BUTTON_LEFT)
   {
      app.dragging = press;
   }
   else if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE)
   {
      app.panning = press;
   }
   if (press && app.view == ViewMode::Globe && button == GLFW_MOUSE_BUTTON_LEFT)
   {
      app.globe.spin = false;
   }
   glfwGetCursorPos(window, &app.last_x, &app.last_y);
}

void on_cursor(GLFWwindow* window, double x, double y)
{
   auto& app = *app_from(window);
   const double dx = x - app.last_x;
   const double dy = y - app.last_y;
   app.last_x = x;
   app.last_y = y;
   app.mouse_x = static_cast<float>(x);
   app.mouse_y = static_cast<float>(y);
   const double scale = app.framebuffer_scale();

   if (app.view == ViewMode::Map && (app.dragging || app.panning))
   {
      app.map.center_x -= dx * scale * app.map.units_per_pixel;
      app.map.center_y += dy * scale * app.map.units_per_pixel;
   }
   else if (app.view == ViewMode::Globe && (app.dragging || app.panning))
   {
      // degrees per pixel shrink as the camera comes closer
      const float speed = 0.25f * (app.globe.distance - 1.0f);
      app.globe.longitude -= static_cast<float>(dx) * speed;
      app.globe.latitude = std::clamp(app.globe.latitude + static_cast<float>(dy) * speed, -85.0f, 85.0f);
   }
   else if (app.view == ViewMode::Scene && app.dragging)
   {
      app.scene_camera.yaw -= static_cast<float>(dx) * 0.006f;
      app.scene_camera.pitch = std::clamp(app.scene_camera.pitch + static_cast<float>(dy) * 0.006f, 0.05f, 1.55f);
   }
   else if (app.view == ViewMode::Scene && app.panning)
   {
      const auto& projection = app.renderer->projection();
      const auto bounds = testbed::Renderer::scene_bounds(app.layers);
      const float size = std::max(bounds[3] - bounds[0], bounds[4] - bounds[1]) * app.scene_camera.distance * 0.0012f;
      testbed::Vec3 right = projection.right;
      testbed::Vec3 forward = testbed::normalize({projection.forward.x, projection.forward.y, 0.0f});
      app.scene_camera.target = app.scene_camera.target - right * (static_cast<float>(dx) * size) + forward * (static_cast<float>(dy) * size);
   }
}

void on_scroll(GLFWwindow* window, double /*x*/, double y)
{
   if (ImGui::GetIO().WantCaptureMouse)
   {
      return;
   }
   auto& app = *app_from(window);
   const double factor = y > 0 ? 0.85 : 1.0 / 0.85;
   if (app.view == ViewMode::Map)
   {
      // zoom about the cursor
      const auto& projection = app.renderer->projection();
      const double scale = app.framebuffer_scale();
      const double mx = (app.mouse_x * scale - projection.width * 0.5) * app.map.units_per_pixel;
      const double my = (projection.height * 0.5 - app.mouse_y * scale) * app.map.units_per_pixel;
      app.map.center_x += mx * (1.0 - factor);
      app.map.center_y += my * (1.0 - factor);
      app.map.units_per_pixel *= factor;
   }
   else if (app.view == ViewMode::Globe)
   {
      app.globe.distance = std::clamp(1.0f + (app.globe.distance - 1.0f) * static_cast<float>(factor), 1.08f, 8.0f);
   }
   else
   {
      app.scene_camera.distance = std::clamp(app.scene_camera.distance * static_cast<float>(factor), 0.03f, 5.0f);
   }
}

#ifndef __EMSCRIPTEN__
// in the browser, web/shell.html handles drops, zips included
void on_drop(GLFWwindow* window, int count, const char** paths)
{
   std::vector<std::filesystem::path> files;
   for (const auto path : std::span(paths, static_cast<std::size_t>(count)))
   {
      files.push_back(utf8_path(path));
   }
   app_from(window)->open(files);
}
#endif

void draw(App& app, int width, int height)
{
   switch (app.view)
   {
      case ViewMode::Map:
         app.renderer->draw_map(width, height, app.map, app.layers, app.hover, app.graticule);
         break;
      case ViewMode::Globe:
         app.renderer->draw_globe(width, height, app.globe, app.layers, app.hover, app.graticule);
         break;
      case ViewMode::Scene:
         app.renderer->draw_scene(width, height, app.scene_camera, app.layers, app.hover);
         break;
   }
}

void frame(void* user)
{
   auto& app = *static_cast<App*>(user);
   glfwPollEvents();

   if (app.view == ViewMode::Globe && app.globe.spin)
   {
      app.globe.longitude = std::fmod(app.globe.longitude + ImGui::GetIO().DeltaTime * 4.0f + 540.0f, 360.0f) - 180.0f;
   }
   app.update_hover();

   ImGui_ImplOpenGL3_NewFrame();
   ImGui_ImplGlfw_NewFrame();
   ImGui::NewFrame();
   testbed::draw_ui(app);
   ImGui::Render();

   int width = 0;
   int height = 0;
   glfwGetFramebufferSize(app.window, &width, &height);
   if (width > 0 && height > 0)
   {
      app.renderer->pixel_scale = app.framebuffer_scale();
      draw(app, width, height);
   }
   ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
   glfwSwapBuffers(app.window);
}

void init_imgui(GLFWwindow* window, bool install_callbacks)
{
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().IniFilename = nullptr;
   ImGui::StyleColorsDark();
   auto& style = ImGui::GetStyle();
   style.WindowRounding = 6.0f;
   style.FrameRounding = 4.0f;
   style.GrabRounding = 4.0f;

   float scale_x = 1.0f;
   float scale_y = 1.0f;
   glfwGetWindowContentScale(window, &scale_x, &scale_y);
   style.ScaleAllSizes(scale_x);
   style.FontScaleDpi = scale_x;

   ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
#ifdef __EMSCRIPTEN__
   ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
   ImGui_ImplOpenGL3_Init("#version 300 es");
}

void shutdown_imgui()
{
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
}

struct Options
{
   std::vector<std::filesystem::path> files;
   std::optional<std::string> scene;
   std::optional<std::string> view;
   std::optional<std::string> screenshot;
   int width = 1280;
   int height = 720;
   bool ui = false;
   std::optional<std::pair<int, int>> mouse;  // --screenshot: hover here, to show the tooltip
};

bool parse_int(std::string_view text, int& value)
{
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
   return error == std::errc{} && end == text.data() + text.size();
}

std::optional<Options> parse_options(std::span<char*> args)
{
   Options options;
   for (std::size_t i = 1; i < args.size(); ++i)
   {
      const std::string_view arg = args[i];
      const auto value = [&]() -> std::optional<std::string>
      {
         if (i + 1 < args.size())
         {
            return std::string{args[++i]};
         }
         std::println(stderr, "{} needs a value", arg);
         return std::nullopt;
      };
      if (arg == "--scene" || arg == "--view" || arg == "--screenshot" || arg == "--size" || arg == "--mouse")
      {
         const auto v = value();
         if (!v)
         {
            return std::nullopt;
         }
         if (arg == "--scene")
         {
            options.scene = *v;
         }
         else if (arg == "--view")
         {
            options.view = *v;
         }
         else if (arg == "--screenshot")
         {
            options.screenshot = *v;
         }
         else if (arg == "--mouse")
         {
            const auto comma = v->find(',');
            int x = 0;
            int y = 0;
            if (comma == std::string::npos || !parse_int(std::string_view{*v}.substr(0, comma), x) ||
                !parse_int(std::string_view{*v}.substr(comma + 1), y))
            {
               std::println(stderr, "--mouse expects X,Y");
               return std::nullopt;
            }
            options.mouse = std::pair{x, y};
         }
         else
         {
            const auto x = v->find('x');
            if (x == std::string::npos || !parse_int(std::string_view{*v}.substr(0, x), options.width) ||
                !parse_int(std::string_view{*v}.substr(x + 1), options.height) || options.width <= 0 || options.height <= 0)
            {
               std::println(stderr, "--size expects WIDTHxHEIGHT");
               return std::nullopt;
            }
         }
      }
      else if (arg == "--ui")
      {
         options.ui = true;
      }
      else if (arg.starts_with("--"))
      {
         std::println(stderr, "unknown option {}", arg);
         return std::nullopt;
      }
      else
      {
         options.files.push_back(utf8_path(arg));
      }
   }
   return options;
}

int run(const Options& options)
{
   App app;
#ifdef __EMSCRIPTEN__
   app.data_dir = "/data";
#else
   app.data_dir = TINYSHP_TESTBED_DATA_DIR;
#endif

   if (glfwInit() == GLFW_FALSE)
   {
      std::println(stderr, "glfwInit failed");
      return 1;
   }
   glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
   glfwWindowHint(GLFW_SAMPLES, 4);
   glfwWindowHint(GLFW_STENCIL_BITS, 8);
   glfwWindowHint(GLFW_DEPTH_BITS, 24);
   if (options.screenshot)
   {
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
   }
   app.window = glfwCreateWindow(options.width, options.height, "tinyshp testbed", nullptr, nullptr);
   if (app.window == nullptr)
   {
      std::println(stderr, "could not create an OpenGL ES 3.0 window");
      glfwTerminate();
      return 1;
   }
   glfwMakeContextCurrent(app.window);
#ifndef __EMSCRIPTEN__
   if (gladLoadGLES2(glfwGetProcAddress) == 0)
   {
      std::println(stderr, "could not load OpenGL ES 3.0 functions");
      glfwTerminate();
      return 1;
   }
   glfwSwapInterval(1);
#endif

   int exit_code = 0;
   try
   {
      app.renderer.emplace();
      if (!options.files.empty())
      {
         app.open(options.files);
      }
      else
      {
         int index = 0;
         if (options.scene)
         {
            const auto list = testbed::scenes();
            const auto it = std::ranges::find_if(list, [&](const auto& s) { return std::string_view{s.name} == *options.scene; });
            if (it != list.end())
            {
               index = static_cast<int>(it - list.begin());
            }
            else if (!parse_int(*options.scene, index) || index < 0 || index >= static_cast<int>(list.size()))
            {
               std::println(stderr, "unknown scene '{}'", *options.scene);
               glfwTerminate();
               return 1;
            }
         }
         app.load_scene(index);
      }
      if (options.view)
      {
         app.view = *options.view == "map" ? ViewMode::Map : *options.view == "3d" ? ViewMode::Scene : ViewMode::Globe;
      }
      for (const auto& message : app.messages)
      {
         std::println("{}", message);
      }

      if (options.screenshot)
      {
         init_imgui(app.window, false);
         app.show_fps = false;
         app.show_panel = options.ui;
         app.globe.spin = false;
         app.fit(false);
         int width = 0;
         int height = 0;
         glfwGetFramebufferSize(app.window, &width, &height);
         // a first frame sets up the projection the labels use, a few more let the panel settle
         for (int i = 0; i < 3; ++i)
         {
            draw(app, width, height);
            if (options.mouse)
            {
               app.mouse_x = static_cast<float>(options.mouse->first) / app.framebuffer_scale();
               app.mouse_y = static_cast<float>(options.mouse->second) / app.framebuffer_scale();
               app.update_hover();
               if (i == 0 && app.hover.layer != nullptr)
               {
                  std::println("hover: {} record {}", app.hover.layer->name, app.hover.layer->file._shapes[app.hover.shape]._record_number);
               }
               ImGui::GetIO().AddMousePosEvent(app.mouse_x, app.mouse_y);
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            testbed::draw_ui(app);
            ImGui::Render();
         }
         const bool written = app.renderer->screenshot(
            *options.screenshot,
            width,
            height,
            [&]
            {
               draw(app, width, height);
               ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
         );
         if (!written)
         {
            std::println(stderr, "could not write {}", *options.screenshot);
            exit_code = 1;
         }
         shutdown_imgui();
      }
      else
      {
         glfwSetWindowUserPointer(app.window, &app);
         glfwSetKeyCallback(app.window, on_key);
         glfwSetMouseButtonCallback(app.window, on_mouse_button);
         glfwSetCursorPosCallback(app.window, on_cursor);
         glfwSetScrollCallback(app.window, on_scroll);
#ifndef __EMSCRIPTEN__
         glfwSetDropCallback(app.window, on_drop);
#endif
         init_imgui(app.window, true);
#ifdef __EMSCRIPTEN__
         web_app = &app;
         EM_ASM(if (Module.onTestbedReady) Module.onTestbedReady(););
         // hands control to the browser; App lives on because main never returns
         emscripten_set_main_loop_arg(frame, &app, 0, true);
#else
         while (glfwWindowShouldClose(app.window) == GLFW_FALSE)
         {
            frame(&app);
         }
         shutdown_imgui();
#endif
      }
   }
   catch (const std::exception& e)
   {
      std::println(stderr, "{}", e.what());
      exit_code = 1;
   }

   app.layers.clear();  // GL objects go before the context
   app.renderer.reset();
   glfwDestroyWindow(app.window);
   glfwTerminate();
   return exit_code;
}

}  // namespace

#ifdef __EMSCRIPTEN__
// called from web/shell.html after it wrote the uploaded files to `directory`
extern "C" EMSCRIPTEN_KEEPALIVE void testbed_open(const char* directory)
{
   if (web_app != nullptr)
   {
      web_app->open({std::filesystem::path{directory}});
   }
}
#endif

int main(int argc, char* argv[])
{
   const auto options = parse_options(std::span(argv, static_cast<std::size_t>(argc)));
   if (!options)
   {
      return 2;
   }
   return run(*options);
}
