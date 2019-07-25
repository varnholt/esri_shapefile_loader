#include "shapenull.h"

#include "endian.h"
#include "streamreader.h"


void ShapeNull::deserialize(std::ifstream& in)
{
    int32_t shapeType;
    read(in, &shapeType);
    mShapeType = static_cast<Shape::ShapeType>(shapeType);
}
