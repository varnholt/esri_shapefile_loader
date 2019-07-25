#pragma once

#include <fstream>

class Shape
{
public:

    Shape() = default;
    virtual ~Shape() = default;

    enum class ShapeType
    {
        ShapeTypeNullShape   = 0,
        ShapeTypePoint       = 1,
        ShapeTypePolyLine    = 3,
        ShapeTypePolygon     = 5,
        ShapeTypeMultiPoint  = 8,
        ShapeTypePointZ      = 11,
        ShapeTypePolyLineZ   = 13,
        ShapeTypePolygonZ    = 15,
        ShapeTypeMultiPointZ = 18,
        ShapeTypePointM      = 21,
        ShapeTypePolyLineM   = 23,
        ShapeTypePolygonM    = 25,
        ShapeTypeMultiPointM = 28,
        ShapeTypeMultiPatch  = 31
    };

    virtual void deserialize(std::ifstream& in) = 0;


protected:

    ShapeType mShapeType = ShapeType::ShapeTypeNullShape;
};

