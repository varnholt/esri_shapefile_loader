#include "shapeloader.h"

// Qt
#include <QFile>

// shapeloader
#include "pointrecord.h"
#include "shapeheader.h"
#include "shapepoint.h"
#include "shapepolygon.h"


// http://www.esri.com/library/whitepapers/pdfs/shapefile.pdf
// http://en.wikipedia.org/wiki/Shapefile#Shapefile_shape_format_.28.shp.29


void ShapeLoader::load(const QString &filename)
{
    QFile file(filename);

    if (file.open(QIODevice::ReadOnly))
    {
        QDataStream in(&file);

        mHeader.deserialize(in);
        mHeader.debug();

        switch (mHeader.getShapeType())
        {
           case Shape::ShapeType::ShapeTypePoint:
           {

              while (!in.atEnd())
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

              while (!in.atEnd())
              {
                 poly.deserialize(in);
                 poly.debug();

                 QList<ShapePolygon> parts;

                 if (poly.mNumParts>1)
                 {
                    parts = poly.simplify();
                 }

                 mPolygons.push_back(poly);

                 foreach(const ShapePolygon& part, parts)
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

        auto totalverts = 0;
        for (auto i = 0; i < mPolygons.size(); i++)
        {
           mPolygons[i].optimize(0.1f);
           totalverts += mPolygons[i].mNumPoints;
        }

        qDebug("total vertices: %d", totalverts);
    }
}



const QList<ShapePolygon> &ShapeLoader::getPolygons()
{
   return mPolygons;
}


const ShapeHeader &ShapeLoader::getShapeHeader()
{
   return mHeader;
}


const QList<ShapePoint> &ShapeLoader::getPoints() const
{
    return mPoints;
}


void ShapeLoader::setPoints(const QList<ShapePoint> &value)
{
    mPoints = value;
}



