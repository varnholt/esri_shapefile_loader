#pragma once

// Qt
#include <QDataStream>

// shapeloader
#include "shape.h"
#include "shapebox.h"
#include "shapepoint.h"

class ShapePolygon : public Shape
{

public:

    ShapePolygon() = default;

    void deserialize(QDataStream &in);
    void debug();
    QList<ShapePolygon> simplify();
    void optimize(float eps);

    ShapeBox mBoundingBox;

    // little endian

    int32_t mNumParts = 0;
    int32_t mNumPoints = 0;
    int32_t* mParts = nullptr;
    ShapePoint* mPoints = nullptr;

};

