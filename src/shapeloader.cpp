#include "shapeloader.h"

#include "pointrecord.h"
#include "shapeheader.h"
#include "shapepoint.h"
#include "shapepolygon.h"

#include <fstream>


// http://www.esri.com/library/whitepapers/pdfs/shapefile.pdf
// http://en.wikipedia.org/wiki/Shapefile#Shapefile_shape_format_.28.shp.29


void ShapeLoader::load(const std::string& filename)
{
    std::ifstream in;
    in.open(filename, std::ios::binary);

    if (!in.is_open())
    {
        return;
    }

    mHeader.deserialize(in);

    switch (mHeader.getShapeType())
    {
       case Shape::ShapeType::ShapeTypePoint:
       {
          while (in.good())
          {
              PointRecord pr;
              pr.deserialize(in);
              mPoints.push_back(pr.getPoint());
          }

          break;
       }


       case Shape::ShapeType::ShapeTypePolygon:
       {
          ShapePolygon poly;

          while (in.good())
          {
             poly.deserialize(in);

             std::vector<ShapePolygon> parts;

             if (poly.mNumParts > 1)
             {
                parts = poly.simplify();
             }

             mPolygons.push_back(poly);

             for (const auto& part : parts)
             {
                mPolygons.push_back(part);
             }
          }

          break;
       }

       case Shape::ShapeType::ShapeTypeNullShape:
       case Shape::ShapeType::ShapeTypePolyLine:
       case Shape::ShapeType::ShapeTypeMultiPoint:
       case Shape::ShapeType::ShapeTypePointZ:
       case Shape::ShapeType::ShapeTypePolyLineZ:
       case Shape::ShapeType::ShapeTypePolygonZ:
       case Shape::ShapeType::ShapeTypeMultiPointZ:
       case Shape::ShapeType::ShapeTypePointM:
       case Shape::ShapeType::ShapeTypePolyLineM:
       case Shape::ShapeType::ShapeTypePolygonM:
       case Shape::ShapeType::ShapeTypeMultiPointM:
       case Shape::ShapeType::ShapeTypeMultiPatch:
       {
          break;
       }
    }

    for (auto& p : mPolygons)
    {
       p.optimize(0.1f);
    }
}



const std::vector<ShapePolygon>& ShapeLoader::getPolygons()
{
   return mPolygons;
}


const ShapeHeader& ShapeLoader::getShapeHeader()
{
   return mHeader;
}


const std::vector<ShapePoint>& ShapeLoader::getPoints() const
{
    return mPoints;
}


void ShapeLoader::setPoints(const std::vector<ShapePoint>& value)
{
    mPoints = value;
}



