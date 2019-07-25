#pragma once

#include "shape.h"

class ShapeBox : public Shape
{

public:

    ShapeBox() = default;
    virtual void deserialize(std::ifstream& in);

protected:

    // little endian
    double mXMin = 0.0;
    double mYMin = 0.0;
    double mXMax = 0.0;
    double mYmax = 0.0;
};

