#include <QApplication>

// shapeloader
#include "shapeloader.h"
#include "widget.h"


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    ShapeLoader earth;
    // earth.load("data/ne_110m_ocean.shp");
    // earth.load("data/ne_50m_land.shp");
    earth.load("data/ne_110m_land.shp");

    ShapeLoader places;
    places.load("data/ne_110m_populated_places_simple.shp");

    auto w = new Widget();

    w->setPolyShapeHeader(earth.getShapeHeader());
    w->setPolygons(earth.getPolygons());

    w->setPointShapeHeader(places.getShapeHeader());
    w->setPoints(places.getPoints());

    w->resize(960, 540);
    w->show();

    return a.exec();
}

