#pragma once

#include "shape.h"

class ShapeNull : public Shape
{

public:

    ShapeNull() = default;
    virtual void deserialize(std::ifstream& in);
};

