// Ear clipping for the planar faces of 3D shapes (MultiPatch rings, PolygonZ), with holes.
//
// Building faces are small (walls are quads, roofs a few dozen points), so the simple O(n^2) algorithm is plenty:
// project the face onto the plane its normal points along most, bridge each hole into the outer ring, clip ears.
#pragma once

#include "math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace testbed
{

struct Point2
{
   double x = 0.0;
   double y = 0.0;
};

/// \brief Newell's method: the normal of a planar (or nearly planar) ring, length = twice the area.
inline std::array<double, 3> newell_normal(std::span<const std::array<double, 3>> ring)
{
   std::array<double, 3> n{};
   for (std::size_t i = 0; i < ring.size(); ++i)
   {
      const auto& a = ring[i];
      const auto& b = ring[(i + 1) % ring.size()];
      n[0] += (a[1] - b[1]) * (a[2] + b[2]);
      n[1] += (a[2] - b[2]) * (a[0] + b[0]);
      n[2] += (a[0] - b[0]) * (a[1] + b[1]);
   }
   return n;
}

namespace triangulation_detail
{

inline double cross(Point2 o, Point2 a, Point2 b)
{
   return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

inline double area(std::span<const Point2> points, std::span<const uint32_t> ring)
{
   double sum = 0.0;
   for (std::size_t i = 0; i < ring.size(); ++i)
   {
      const auto& a = points[ring[i]];
      const auto& b = points[ring[(i + 1) % ring.size()]];
      sum += a.x * b.y - b.x * a.y;
   }
   return sum * 0.5;
}

inline bool in_triangle(Point2 p, Point2 a, Point2 b, Point2 c)
{
   return cross(a, b, p) >= 0.0 && cross(b, c, p) >= 0.0 && cross(c, a, p) >= 0.0;
}

inline bool segments_cross(Point2 a, Point2 b, Point2 c, Point2 d)
{
   const double d1 = cross(c, d, a);
   const double d2 = cross(c, d, b);
   const double d3 = cross(a, b, c);
   const double d4 = cross(a, b, d);
   return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

/// \brief splices `hole` (clockwise) into `outer` (counterclockwise) through the shortest bridge that crosses nothing.
inline void bridge(std::span<const Point2> points, std::vector<uint32_t>& outer, std::span<const uint32_t> hole,
                   std::span<const std::vector<uint32_t>> others)
{
   // the hole vertex furthest right sees something of the outer ring to its right
   std::size_t m = 0;
   for (std::size_t i = 1; i < hole.size(); ++i)
   {
      if (points[hole[i]].x > points[hole[m]].x)
      {
         m = i;
      }
   }
   const Point2 mp = points[hole[m]];

   const auto crosses_any = [&](Point2 a, Point2 b, std::span<const uint32_t> ring)
   {
      for (std::size_t i = 0; i < ring.size(); ++i)
      {
         if (segments_cross(a, b, points[ring[i]], points[ring[(i + 1) % ring.size()]]))
         {
            return true;
         }
      }
      return false;
   };

   std::size_t best = 0;
   double best_distance = std::numeric_limits<double>::max();
   for (std::size_t i = 0; i < outer.size(); ++i)
   {
      const Point2 p = points[outer[i]];
      const double distance = (p.x - mp.x) * (p.x - mp.x) + (p.y - mp.y) * (p.y - mp.y);
      if (distance >= best_distance || crosses_any(mp, p, outer) || crosses_any(mp, p, hole))
      {
         continue;
      }
      bool blocked = false;
      for (const auto& other : others)
      {
         blocked = blocked || crosses_any(mp, p, other);
      }
      if (!blocked)
      {
         best = i;
         best_distance = distance;
      }
   }

   // outer[..best], hole[m..], hole[..m], hole[m], outer[best..]
   std::vector<uint32_t> merged(outer.begin(), outer.begin() + static_cast<std::ptrdiff_t>(best) + 1);
   for (std::size_t k = 0; k <= hole.size(); ++k)
   {
      merged.push_back(hole[(m + k) % hole.size()]);
   }
   merged.insert(merged.end(), outer.begin() + static_cast<std::ptrdiff_t>(best), outer.end());
   outer = std::move(merged);
}

}  // namespace triangulation_detail

/// \brief triangulates a 2D polygon with holes; rings index into `points` and may have any orientation. Appends
/// counterclockwise triangles (indices into `points`) to `triangles`.
inline void triangulate(std::span<const Point2> points, std::vector<uint32_t> outer, std::vector<std::vector<uint32_t>> holes,
                        std::vector<uint32_t>& triangles)
{
   namespace d = triangulation_detail;
   if (d::area(points, outer) < 0.0)
   {
      std::ranges::reverse(outer);
   }
   for (auto& hole : holes)
   {
      if (d::area(points, hole) > 0.0)
      {
         std::ranges::reverse(hole);
      }
   }
   // holes furthest right first, so later bridges do not cross earlier ones
   std::ranges::sort(holes, [&](const auto& a, const auto& b) {
      const auto max_x = [&](const auto& ring)
      {
         double x = -std::numeric_limits<double>::max();
         for (const auto i : ring)
         {
            x = std::max(x, points[i].x);
         }
         return x;
      };
      return max_x(a) > max_x(b);
   });
   for (std::size_t h = 0; h < holes.size(); ++h)
   {
      if (holes[h].size() >= 3)
      {
         d::bridge(points, outer, holes[h], std::span{holes}.subspan(h + 1));
      }
   }

   std::vector<uint32_t> ring = std::move(outer);
   std::size_t guard = 0;
   while (ring.size() > 3 && guard < ring.size() * ring.size() + 16)
   {
      ++guard;
      bool clipped = false;
      for (std::size_t i = 0; i < ring.size(); ++i)
      {
         const std::size_t ia = (i + ring.size() - 1) % ring.size();
         const std::size_t ic = (i + 1) % ring.size();
         const Point2 a = points[ring[ia]];
         const Point2 b = points[ring[i]];
         const Point2 c = points[ring[ic]];
         if (d::cross(a, b, c) <= 0.0)
         {
            continue;  // reflex or degenerate
         }
         bool ear = true;
         for (std::size_t j = 0; j < ring.size() && ear; ++j)
         {
            if (j == ia || j == i || j == ic || ring[j] == ring[ia] || ring[j] == ring[i] || ring[j] == ring[ic])
            {
               continue;  // bridge vertices appear twice
            }
            ear = !d::in_triangle(points[ring[j]], a, b, c);
         }
         if (ear)
         {
            triangles.insert(triangles.end(), {ring[ia], ring[i], ring[ic]});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
         }
      }
      if (!clipped)
      {
         // self-intersecting or collinear leftovers: drop the flattest vertex and go on
         std::size_t flattest = 0;
         double smallest = std::numeric_limits<double>::max();
         for (std::size_t i = 0; i < ring.size(); ++i)
         {
            const double turn = std::abs(d::cross(points[ring[(i + ring.size() - 1) % ring.size()]], points[ring[i]],
                                                  points[ring[(i + 1) % ring.size()]]));
            if (turn < smallest)
            {
               smallest = turn;
               flattest = i;
            }
         }
         ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(flattest));
      }
   }
   if (ring.size() == 3 && d::cross(points[ring[0]], points[ring[1]], points[ring[2]]) > 0.0)
   {
      triangles.insert(triangles.end(), {ring[0], ring[1], ring[2]});
   }
}

/// \brief projects 3D rings onto the plane of their normal and triangulates them. Returns indices into the
/// concatenation of all rings (outer first, then holes), without the closing point of each ring.
inline std::vector<uint32_t> triangulate_face(std::span<const std::array<double, 3>> outer,
                                              std::span<const std::span<const std::array<double, 3>>> holes)
{
   const auto n = newell_normal(outer);
   const std::size_t axis = std::abs(n[0]) > std::abs(n[1]) ? (std::abs(n[0]) > std::abs(n[2]) ? 0 : 2)
                                                            : (std::abs(n[1]) > std::abs(n[2]) ? 1 : 2);
   const std::size_t u = axis == 0 ? 1 : 0;
   const std::size_t v = axis == 2 ? 1 : 2;

   std::vector<Point2> points;
   const auto add_ring = [&](std::span<const std::array<double, 3>> ring)
   {
      std::vector<uint32_t> indices;
      const std::size_t count = ring.size() > 1 && ring.front() == ring.back() ? ring.size() - 1 : ring.size();
      for (std::size_t i = 0; i < count; ++i)
      {
         indices.push_back(static_cast<uint32_t>(points.size()));
         points.push_back({ring[i][u], ring[i][v]});
      }
      return indices;
   };

   auto outer_indices = add_ring(outer);
   std::vector<std::vector<uint32_t>> hole_indices;
   for (const auto& hole : holes)
   {
      hole_indices.push_back(add_ring(hole));
   }
   std::vector<uint32_t> triangles;
   if (outer_indices.size() >= 3)
   {
      triangulate(points, std::move(outer_indices), std::move(hole_indices), triangles);
   }
   return triangles;
}

}  // namespace testbed
