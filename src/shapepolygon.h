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

    int32_t mNumParts = 0;
    int32_t mNumPoints = 0;
    int32_t* mParts = nullptr;
    ShapePoint* mPoints = nullptr;

};

