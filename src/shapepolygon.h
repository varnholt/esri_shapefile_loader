#pragma once

#include <fstream>
#include <vector>

#include "shape.h"
#include "shapebox.h"
#include "shapepoint.h"

class ShapePolygon : public Shape
{

public:

    ShapePolygon() = default;

    void deserialize(std::ifstream& in);
    std::vector<ShapePolygon> simplify();
    void optimize(float eps);

    ShapeBox mBoundingBox;

    // little endian

    uint32_t mNumParts = 0;
    uint32_t mNumPoints = 0;
    std::vector<uint32_t> mParts;
    std::vector<ShapePoint> mPoints;

};

