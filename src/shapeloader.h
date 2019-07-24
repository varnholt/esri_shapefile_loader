#pragma once

// Qt
#include <QString>

// shapeloader
#include "shape.h"
#include "shapeheader.h"
#include "shapepolygon.h"

class ShapeLoader
{
public:

    ShapeLoader() = default;

    void load(const QString& filename);

    const QList<ShapePolygon>& getPolygons();
    const ShapeHeader& getShapeHeader();
    const QList<ShapePoint>& getPoints() const;
    void setPoints(const QList<ShapePoint> &value);

protected:

    ShapeHeader mHeader;

    QList<ShapePolygon> mPolygons;
    QList<ShapePoint> mPoints;
};

