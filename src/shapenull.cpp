// header
#include "shapenull.h"

// shapeloader
#include "endian.h"


void ShapeNull::deserialize(QDataStream &in)
{
    int32_t shapeType;
    in >> shapeType;
    shapeType = Endian::swapEndian(shapeType);

    mShapeType = static_cast<Shape::ShapeType>(shapeType);
}
