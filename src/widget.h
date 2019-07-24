#pragma once

// Qt
#include <QGLWidget>
#include <QTime>
#include <QTimer>

// shapeloader
#include "shape.h"
#include "shapeheader.h"
#include "shapepolygon.h"


class Widget : public QGLWidget
{
    Q_OBJECT

public:

   Widget(QWidget *parent = 0);

   void initializeGL();
   void resizeGL(int w, int h);
   void paintGL();

   const ShapeHeader& getPolyShapeHeader() const;
   void setPolyShapeHeader(const ShapeHeader &value);

   const QList<ShapePolygon>& getPolygons() const;
   void setPolygons(const QList<ShapePolygon> &value);

   const QList<ShapePoint>& getPoints() const;
   void setPoints(const QList<ShapePoint> &value);

   const ShapeHeader& getPointShapeHeader() const;
   void setPointShapeHeader(const ShapeHeader &value);


protected:

   void setOrthoRect(float minx, float maxx, float miny, float maxy);
   void drawSphere();
   void drawPolys();
   void drawPoints();
   void usePolyBoundingBox();
   void usePointBoundingBox();

   ShapeHeader mPolyShapeHeader;
   ShapeHeader mPointShapeHeader;
   QList<ShapePolygon> mPolygons;
   QList<ShapePoint> mPoints;
   QTime mGlobalTime;
   QTimer mUpdatePaintTimer;
};

