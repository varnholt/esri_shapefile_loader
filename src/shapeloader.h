#pragma once

#include <string>

#include "shape.h"
#include "shapeheader.h"
#include "shapepolygon.h"


class ShapeLoader
{

public:

    ShapeLoader() = default;

    void load(const std::string& filename);

    const std::vector<ShapePolygon>& getPolygons();
    const ShapeHeader& getShapeHeader();
    const std::vector<ShapePoint>& getPoints() const;
    void setPoints(const std::vector<ShapePoint> &value);


protected:

    ShapeHeader mHeader;

    std::vector<ShapePolygon> mPolygons;
    std::vector<ShapePoint> mPoints;
};

