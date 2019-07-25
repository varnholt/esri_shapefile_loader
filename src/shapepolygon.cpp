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
    mParts = new int32_t[mNumParts + 1];
    for( int i = 0; i < mNumParts; i++ )
    {
        int32_t val = 0;
        read(in, &val);
        mParts[i] = val;
    }

    mParts[mNumParts]= mNumPoints;

    // read points
    mPoints = new ShapePoint[mNumPoints];
    for (int i = 0; i < mNumPoints; i++ )
    {
        mPoints[i].deserialize(in);
    }
}


std::vector<ShapePolygon> ShapePolygon::simplify()
{
   std::vector<ShapePolygon> parts;

   // keep 1st part and create new polygons from other parts
   if (mNumParts>1)
   {
      for( int part=1; part<mNumParts; part++ )
      {
         int32_t start = mParts[part];
         int32_t stop = mParts[part+1] - 1;

         ShapePolygon poly;
         poly.mNumParts= 1;
         poly.mNumPoints= 0;
         poly.mParts= new int32_t[1];
         poly.mPoints= new ShapePoint[stop - start];
         poly.mParts= 0;

         for (int v=start; v<stop; v++)
            poly.mPoints[poly.mNumPoints++]= mPoints[v];

         parts.push_back(poly);
      }

      mNumPoints= mParts[1]-1;
      mNumParts= 1;
   }

   return parts;
}


Vector2 nearestLinePoint(const Vector2& p, const Vector2& l1, const Vector2& l2)
{
    float A = p.x - l1.x;
    float B = p.y - l1.y;
    float C = l2.x - l1.x;
    float D = l2.y - l1.y;

    float dot = A * C + B * D;
    float len_sq = C * C + D * D;

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
       xx = l1.x + param * C;
       yy = l1.y + param * D;
    }

    return Vector2(xx, yy);
}


//! optimize polygon (remove redundant vertices)
void ShapePolygon::optimize(float eps)
{
   std::vector<Vector2> vertices;
   std::vector<int32_t> s;

   for (auto i = 0; i < mNumPoints; i++)
   {
      vertices.push_back(Vector2(mPoints[i].getX(), mPoints[i].getY()));
      s.push_back(i);
   }

   std::vector<int32_t> opt;

   const auto size = s.size();
   auto i1= s.at(size-2);
   auto i2= s.at(0);
   Vector2 v1 = vertices[i1];
   Vector2 v2 = vertices[i2];
   Vector2 v3;

   Vector2 prev= v1;
   Vector2 keep= v1;

   for (int i=1; i<size; i++)
   {
      int i3= s.at(i);
      v3= vertices[ i3 ];

      Vector2 l1 = Vector2::normalize(v3 - v1);
      Vector2 l2 = Vector2::normalize(keep - prev);

      // distance v2 to (v1,v3)
      Vector2 n = nearestLinePoint(v2, keep, v3);
      float l = v2.distance2(keep);
      float d = n.distance2(v2) * l*l;

      if (d > eps || l1.dot(l2) < 0.5)
      {
         opt.push_back( i2 );
         prev = keep;
         keep = v2;
      }

      v1 = v2;
      v2 = v3;

      i1 = i2;
      i2 = i3;
   }

   // remove linear neighbours
   for (int i=1; i<opt.size(); )
   {
      if (i == 0)
      {
         v1 = vertices[opt.at(opt.size()-1)];
      }
      else
      {
         v1 = vertices[opt.at(i-1)];
      }

      v2 = vertices[opt.at(i)];

      if (i==opt.size()-1)
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
      mPoints[i].setX(vertices[idx].x);
      mPoints[i].setY(vertices[idx].y);
   }

   mNumPoints = opt.size();
}


