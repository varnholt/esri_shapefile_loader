#pragma once

#include <math.h>

struct Vector2
{
   float x,y;

   Vector2() { };

   Vector2(float px, float py) : x(px), y(py) { };

   Vector2 operator - (const Vector2& a) const
   {
      Vector2 n( x - a.x, y - a.y );
      return n;
   }

   Vector2 operator + (const Vector2& a) const
   {
      Vector2 n( x + a.x, y + a.y );
      return n;
   }

   inline float dot(const Vector2& a) const
   {
      return x * a.x + y * a.y;
   }

   inline float distance2(const Vector2& v) const
   {
      return (x-v.x)*(x-v.x) + (y-v.y)*(y-v.y);
   }

   inline float length2() const
   {
      return x*x + y*y;
   }

   inline float length() const
   {
      return sqrtf( length2() );
   }

   static Vector2 normalize(const Vector2& v)
   {
      float t= v.length2();
      if (t != 0.0f)
      {
         t= 1.0f / sqrtf(t);
         return Vector2(v.x*t, v.y*t);
      }
      else
      {
         return Vector2(0.0f, 0.0f);
      }
   }

};

