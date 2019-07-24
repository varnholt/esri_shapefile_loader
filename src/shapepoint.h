#pragma once

// Qt
#include <QDataStream>

// shapeloader
#include "shape.h"


class ShapePoint : public Shape
{

public:

    ShapePoint() = default;

    void deserialize(QDataStream& in);

    double getX() const;
    void setX(double value);

    double getY() const;
    void setY(double value);


protected:

    double mX = 0.0;
    double mY = 0.0;

};

