// header
#include "widget.h"

#include <math.h>


Widget::Widget(QWidget *parent)
    : QGLWidget(parent)
{

   connect(
      &mUpdatePaintTimer,
      SIGNAL(timeout()),
      this,
      SLOT(updateGL())
   );

   mUpdatePaintTimer.start(16);
}


void Widget::initializeGL()
{
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

   glDisable(GL_DEPTH_TEST);
   glDepthMask(false);

   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   mGlobalTime.start();
}


void Widget::setOrthoRect(float minx, float maxx, float miny, float maxy)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(
       minx,
       maxx,
       miny,
       maxy,
       -10.0f,
       10.0f
    );

    glMatrixMode(GL_MODELVIEW);
}


void Widget::usePointBoundingBox()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(
       mPointShapeHeader.getXMin(),
       mPointShapeHeader.getXMax(),
       mPointShapeHeader.getYMin(),
       mPointShapeHeader.getYMax(),
       -10.0f,
       10.0f
    );

    glMatrixMode(GL_MODELVIEW);
}


const ShapeHeader& Widget::getPointShapeHeader() const
{
    return mPointShapeHeader;
}


void Widget::setPointShapeHeader(const ShapeHeader &value)
{
    mPointShapeHeader = value;
}


void Widget::resizeGL(int w, int h)
{
   glViewport(0,0,w,h);
}


void Widget::drawPolys()
{
   setOrthoRect(
         mPolyShapeHeader.getXMin(),
         mPolyShapeHeader.getXMax(),
         mPolyShapeHeader.getYMin(),
         mPolyShapeHeader.getYMax()
   );
   setOrthoRect( -180, 180, -90, 90 );
   glLoadIdentity();

    for (int polyShape = 0; polyShape < mPolygons.size(); polyShape++)
    {
       int numParts = mPolygons[polyShape].mNumParts;

       // qDebug("numParts: %d", numParts);

       for (
          int part = 0;
          part < numParts;
          part++
       )
       {
          int start = 0;
          int stop = mPolygons[polyShape].mNumPoints - 1;

          glBegin(GL_LINE_LOOP);

          for (int point = start; point < stop; point++)
          {
             ShapePoint shapePoint = mPolygons[polyShape].mPoints[point];

             float x = shapePoint.getX();
             float y = shapePoint.getY();

             // qDebug("x: %f, y: %f", x, y);

             glVertex2f(x, y);
          }

          // qDebug("---");

          glEnd();
       }
    }
}


void Widget::drawPoints()
{
    bool twod = false;

    if (twod)
    {
       setOrthoRect(
             mPointShapeHeader.getXMin(),
             mPointShapeHeader.getXMax(),
             mPointShapeHeader.getYMin(),
             mPointShapeHeader.getYMax()
       );

       setOrthoRect( -180, 180, -90, 90 );
    }

    glColor4f(0.72f, 0.0f, 0.0f, 0.5f);
    glPointSize(4.0f);
    glBegin(GL_POINTS);

    for (int point = 0; point < mPoints.size(); point++)
    {
       ShapePoint p = mPoints[point];

        if (twod)
        {
            glVertex2f(p.getX(), p.getY());
        }
        else
        {
           //
           float lat = p.getX() * M_PI / 180.0;
           float lon = p.getY() / 90.0f;

           float r= 1.0f;
           float a= cos(lon);//sqrtf( 1.0f - lon*lon );
           float x = r * sin(lat) * a;
           float z = r * cos(lat) * a;
           float y = r * sin(lon);

           glVertex3f(x,y,z);
        }
    }

    glEnd();
}


void Widget::drawSphere()
{
   float time= mGlobalTime.elapsed() / 1000.0f;

   // gluPerspective:
   float scale= 0.5f;
   float aspect= (float)width() / (float)height();
   float zNear= 0.5f;
   float zFar= 1000.0f;
   float ymin = -zNear * scale;
   float ymax = -ymin;
   float xmax = ymax * aspect;
   float xmin = ymin * aspect;
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum( xmin, xmax, ymin, ymax, zNear, zFar );

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -3.0);
   glRotatef(time*10.0f, 0,1,0);

   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE);

   glEnable(GL_LINE_SMOOTH);
   glLineWidth(2.0f);


   // latitude
   glColor4f(0.05f, 0.15f, 0.2f, 0.5f);
   for (int y = -90; y <= 90; y += 10)
   {
      glBegin(GL_LINE_LOOP);
      for (int x = 0; x < 360; x += 10)
      {
         float r = 1.0f;

         float lat = (float)x * M_PI / 180.0f;
         float lon = (float)y / 90.0f;

         float a = sqrtf(1.0f - lon * lon);
         float sx = r * sin(lat) * a;
         float sz = r * cos(lat) * a;
         float sy = r * sin(lon);

         glVertex3f(sx, sy, sz);
      }
      glEnd();
   }

   // longitude
   for (int y = -90; y <= 90; y+= 5)
   {
      float lon = (float)y * M_PI / 90.0f;
      float dx = cos(lon);
      float dy = sin(lon);

      glBegin(GL_LINE_LOOP);
      for (int x = 0; x < 360; x += 5)
      {
         float r = 1.0f;

         float lat = (float)x * M_PI / 180.0f;

         float sx = r *  dx * sin(lat);
         float sz = r * -dy * sin(lat);
         float sy = r * cos(lat);

         glVertex3f(sx, sy, sz);
      }
      glEnd();
   }


   // continent polys
   glLineWidth(1.5f);
   glColor4f(0.3f, 0.3f, 0.2f, 1.0f);
   for (int polyShape = 0; polyShape < mPolygons.size(); polyShape++)
   {
      int numParts = mPolygons[polyShape].mNumParts;

      for (
         int part = 0;
         part < numParts;
         part++
      )
      {
         int start = 0;
         int stop = mPolygons[polyShape].mNumPoints - 1;

         glBegin(GL_LINE_LOOP);

         for (int point = start; point < stop; point++)
         {
            ShapePoint shapePoint = mPolygons[polyShape].mPoints[point];

            float lat = shapePoint.getX() * M_PI / 180.0;
            float lon = shapePoint.getY() / 90.0f;

            float r= 1.0f;
            float a= cos(lon);//sqrtf( 1.0f - lon*lon );
            float x = r * sin(lat) * a;
            float z = r * cos(lat) * a;
            float y = r * sin(lon);

            // qDebug("x: %f, y: %f", x, y);

            glVertex3f(x, y, z);
         }

         // qDebug("---");

         glEnd();
      }
   }

/*
   // paint dots
   glPointSize(2.0f);
   glDisable(GL_BLEND);
   glColor4f(1,0,0,1);
   glBegin(GL_POINTS);
   for (int polyShape = 0; polyShape < mPolygons.size(); polyShape++)
   {
      const ShapePolygon& shape= mPolygons[polyShape];
      for (int p=0; p<shape.mNumPoints; p++)
      {
         const ShapePoint& point = shape.mPoints[p];

         float lat = point.getX() * M_PI / 180.0;
         float lon = point.getY() / 90.0f;

         float r= 1.0f;
         float a= cos(lon);//sqrtf( 1.0f - lon*lon );
         float x = r * sin(lat) * a;
         float z = r * cos(lat) * a;
         float y = r * sin(lon);

         // qDebug("x: %f, y: %f", x, y);

         glVertex3f(x, y, z);
      }
   }
   glEnd();
*/
}

void Widget::paintGL()
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glEnable(GL_LINE_SMOOTH);
   glColor3f(0.2f, 0.65f, 0.8f);

   // blending must be enabled to make antialiased lines work
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE);

   // drawPolys();
   drawSphere();
   drawPoints();
}


const ShapeHeader &Widget::getPolyShapeHeader() const
{
   return mPolyShapeHeader;
}


void Widget::setPolyShapeHeader(const ShapeHeader &value)
{
   mPolyShapeHeader = value;
}


const QList<ShapePolygon> &Widget::getPolygons() const
{
   return mPolygons;
}


void Widget::setPolygons(const QList<ShapePolygon> &value)
{
   mPolygons = value;
}


const QList<ShapePoint> &Widget::getPoints() const
{
    return mPoints;
}


void Widget::setPoints(const QList<ShapePoint> &value)
{
    mPoints = value;
}

