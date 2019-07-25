#include "shapepolygon.h"

#include "endian.h"
#include "recordheader.h"
#include "shapepoint.h"
#include "streamreader.h"
#include "vector2.h"


void ShapePolygon::deserialize(std::ifstream& in)
{
    RecordHeader rh;
    rh.deserialize(in);

    int32_t shapeType;
    read(in, &shapeType);
    mShapeType = static_cast<Shape::ShapeType>(shapeType);

    mBoundingBox.deserialize(in);

    read(in, &mNumParts);
    read(in, &mNumPoints);

    // part pointers
    mParts.resize(mNumParts + 1);
    for (auto i = 0u; i < mNumParts; i++)
    {
        uint32_t val = 0;
        read(in, &val);
        mParts[i] = val;
    }

    mParts[mNumParts] = mNumPoints;

    // read points
    mPoints.resize(mNumPoints);
    for (auto& p : mPoints)
    {
        p.deserialize(in);
    }
}


std::vector<ShapePolygon> ShapePolygon::simplify()
{
   std::vector<ShapePolygon> parts;

   // keep 1st part and create new polygons from other parts
   if (mNumParts > 1)
   {
      for (auto part = 1u; part < mNumParts; part++)
      {
         auto start = mParts[part];
         auto stop = mParts[part + 1] - 1;

         ShapePolygon poly;
         poly.mNumParts= 1;
         poly.mNumPoints= 0;
         poly.mParts = {0};
         poly.mPoints.resize(stop - start);

         for (auto v = start; v < stop; v++)
            poly.mPoints[poly.mNumPoints++] = mPoints[v];

         parts.push_back(poly);
      }

      mNumPoints= mParts[1]-1;
      mNumParts= 1;
   }

   return parts;
}


Vector2 nearestLinePoint(const Vector2& p, const Vector2& l1, const Vector2& l2)
{
    float a = p.x - l1.x;
    float b = p.y - l1.y;
    float c = l2.x - l1.x;
    float d = l2.y - l1.y;

    float dot = a * c + b * d;
    float len_sq = c * c + d * d;

    float xx, yy;

    if (dot < 0.0f)
    {
        xx = l1.x;
        yy = l1.y;
    }
    else if (dot >= len_sq)
    {
        xx = l2.x;
        yy = l2.y;
    }
    else
    {
       float param = dot / len_sq;
       xx = l1.x + param * c;
       yy = l1.y + param * d;
    }

    return Vector2(xx, yy);
}


//! optimize polygon (remove redundant vertices)
void ShapePolygon::optimize(float eps)
{
   std::vector<Vector2> vertices;
   std::vector<uint32_t> s;
   std::vector<uint32_t> opt;

   for (auto i = 0u; i < mNumPoints; i++)
   {
      vertices.push_back(Vector2(static_cast<float>(mPoints[i].getX()), static_cast<float>(mPoints[i].getY())));
      s.push_back(i);
   }

   const auto size = s.size();
   auto i1 = s.at(size - 2);
   auto i2 = s.at(0);
   Vector2 v1 = vertices[i1];
   Vector2 v2 = vertices[i2];
   Vector2 v3;

   Vector2 prev = v1;
   Vector2 keep = v1;

   for (auto i = 1u; i < size; i++)
   {
      auto i3 = s.at(i);
      v3= vertices[ i3 ];

      Vector2 l1 = Vector2::normalize(v3 - v1);
      Vector2 l2 = Vector2::normalize(keep - prev);

      // distance v2 to (v1, v3)
      Vector2 n = nearestLinePoint(v2, keep, v3);
      float l = v2.distance2(keep);
      float d = n.distance2(v2) * l*l;

      if (d > eps || l1.dot(l2) < 0.5f)
      {
         opt.push_back(i2);
         prev = keep;
         keep = v2;
      }

      v1 = v2;
      v2 = v3;

      i1 = i2;
      i2 = i3;
   }

   // remove linear neighbours
   for (auto i = 1u; i < opt.size(); )
   {
      if (i == 0)
      {
         v1 = vertices[opt.at(opt.size() - 1)];
      }
      else
      {
         v1 = vertices[opt.at(i - 1)];
      }

      v2 = vertices[opt.at(i)];

      if (i==opt.size() - 1)
      {
         v3 = vertices[opt.at(0)];
      }
      else
      {
         v3 = vertices[opt.at(i+1)];
      }

      Vector2 l1= Vector2::normalize(v2 - v1);
      Vector2 l2= Vector2::normalize(v3 - v2);
      float a = l1.dot(l2);

      if (a > 0.999f)
      {
         opt.erase(opt.begin() + i - 1);
      }
      else
      {
         i++;
      }
   }

   for (auto i = 0u; i < opt.size(); i++)
   {
      auto idx = opt[i];
      mPoints[i].setX(static_cast<double>(vertices[idx].x));
      mPoints[i].setY(static_cast<double>(vertices[idx].y));
   }

   mNumPoints = static_cast<uint32_t>(opt.size());
}

