// Colour ramps for graduated styles and a categorical palette.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace testbed
{

struct Rgba
{
   float r = 1.0f;
   float g = 1.0f;
   float b = 1.0f;
   float a = 1.0f;
};

struct Ramp
{
   std::string_view name;
   std::span<const Rgba> stops;
};

namespace detail
{

// sampled from matplotlib's viridis and magma, ColorBrewer and a cyclic hue wheel
inline constexpr std::array<Rgba, 6> viridis{{{0.267f, 0.005f, 0.329f, 1}, {0.254f, 0.265f, 0.530f, 1}, {0.164f, 0.471f, 0.558f, 1},
                                             {0.135f, 0.659f, 0.518f, 1}, {0.478f, 0.821f, 0.318f, 1}, {0.993f, 0.906f, 0.144f, 1}}};
inline constexpr std::array<Rgba, 6> magma{{{0.001f, 0.000f, 0.014f, 1}, {0.232f, 0.060f, 0.437f, 1}, {0.550f, 0.161f, 0.506f, 1},
                                           {0.868f, 0.288f, 0.409f, 1}, {0.994f, 0.624f, 0.427f, 1}, {0.987f, 0.991f, 0.750f, 1}}};
inline constexpr std::array<Rgba, 5> ocean{{{0.937f, 0.953f, 1.000f, 1}, {0.776f, 0.859f, 0.937f, 1}, {0.420f, 0.682f, 0.839f, 1},
                                           {0.129f, 0.443f, 0.710f, 1}, {0.031f, 0.188f, 0.420f, 1}}};
inline constexpr std::array<Rgba, 5> heat{{{1.000f, 0.969f, 0.737f, 1}, {0.996f, 0.769f, 0.310f, 1}, {0.996f, 0.600f, 0.161f, 1},
                                          {0.851f, 0.373f, 0.055f, 1}, {0.400f, 0.145f, 0.024f, 1}}};
inline constexpr std::array<Rgba, 7> wheel{{{0.90f, 0.30f, 0.35f, 1}, {0.95f, 0.65f, 0.25f, 1}, {0.85f, 0.85f, 0.30f, 1},
                                           {0.35f, 0.80f, 0.45f, 1}, {0.25f, 0.65f, 0.90f, 1}, {0.60f, 0.40f, 0.90f, 1},
                                           {0.90f, 0.30f, 0.35f, 1}}};

// Tableau 10
inline constexpr std::array<Rgba, 10> categories{{{0.306f, 0.475f, 0.655f, 1}, {0.949f, 0.557f, 0.169f, 1}, {0.882f, 0.341f, 0.349f, 1},
                                                 {0.463f, 0.718f, 0.698f, 1}, {0.349f, 0.631f, 0.310f, 1}, {0.929f, 0.788f, 0.282f, 1},
                                                 {0.690f, 0.478f, 0.631f, 1}, {1.000f, 0.616f, 0.655f, 1}, {0.612f, 0.459f, 0.373f, 1},
                                                 {0.729f, 0.690f, 0.675f, 1}}};

}  // namespace detail

inline constexpr std::array<Ramp, 5> ramps{{{"viridis", detail::viridis},
                                            {"magma", detail::magma},
                                            {"ocean", detail::ocean},
                                            {"heat", detail::heat},
                                            {"hue wheel", detail::wheel}}};

inline Rgba sample(const Ramp& ramp, float t)
{
   t = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(ramp.stops.size() - 1);
   const auto i = std::min(static_cast<std::size_t>(t), ramp.stops.size() - 2);
   const float f = t - static_cast<float>(i);
   const Rgba& a = ramp.stops[i];
   const Rgba& b = ramp.stops[i + 1];
   return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1.0f};
}

inline Rgba category(std::size_t index)
{
   return detail::categories[index % detail::categories.size()];
}

}  // namespace testbed
