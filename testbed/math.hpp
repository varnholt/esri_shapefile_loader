// Just enough vector and matrix code for the cameras. Column-major, like OpenGL.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace testbed
{

struct Vec3
{
   float x = 0.0f;
   float y = 0.0f;
   float z = 0.0f;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b)
{
   return {a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vec3 operator-(Vec3 a, Vec3 b)
{
   return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vec3 operator*(Vec3 a, float s)
{
   return {a.x * s, a.y * s, a.z * s};
}

constexpr float dot(Vec3 a, Vec3 b)
{
   return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(Vec3 a, Vec3 b)
{
   return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 a)
{
   return std::sqrt(dot(a, a));
}

inline Vec3 normalize(Vec3 a)
{
   const float l = length(a);
   return l > 0.0f ? a * (1.0f / l) : Vec3{0.0f, 0.0f, 1.0f};
}

using Mat4 = std::array<float, 16>;

constexpr Mat4 identity()
{
   return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
{
   Mat4 r{};
   for (std::size_t col = 0; col < 4; ++col)
   {
      for (std::size_t row = 0; row < 4; ++row)
      {
         float sum = 0.0f;
         for (std::size_t k = 0; k < 4; ++k)
         {
            sum += a[k * 4 + row] * b[col * 4 + k];
         }
         r[col * 4 + row] = sum;
      }
   }
   return r;
}

inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane)
{
   const float f = 1.0f / std::tan(fov_y * 0.5f);
   Mat4 m{};
   m[0] = f / aspect;
   m[5] = f;
   m[10] = (far_plane + near_plane) / (near_plane - far_plane);
   m[11] = -1.0f;
   m[14] = 2.0f * far_plane * near_plane / (near_plane - far_plane);
   return m;
}

/// \brief a camera at `eye` looking at `target`, with `up` roughly up.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up)
{
   const Vec3 f = normalize(target - eye);
   const Vec3 s = normalize(cross(f, up));
   const Vec3 u = cross(s, f);
   return {s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
}

/// \brief transforms a point and returns normalized device coordinates; w <= 0 means behind the camera.
inline std::array<float, 4> project(const Mat4& m, Vec3 p)
{
   std::array<float, 4> r{};
   for (std::size_t row = 0; row < 4; ++row)
   {
      r[row] = m[row] * p.x + m[4 + row] * p.y + m[8 + row] * p.z + m[12 + row];
   }
   if (r[3] > 0.0f)
   {
      r[0] /= r[3];
      r[1] /= r[3];
      r[2] /= r[3];
   }
   return r;
}

inline constexpr float pi = 3.14159265358979f;

/// \brief longitude and latitude in degrees to a point on the unit sphere: y is north, z faces longitude 0.
inline Vec3 sphere_point(float longitude, float latitude)
{
   const float lon = longitude * pi / 180.0f;
   const float lat = latitude * pi / 180.0f;
   return {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)};
}

}  // namespace testbed
