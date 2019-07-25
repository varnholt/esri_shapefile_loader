#include "shapepoint.h"

#include "endian.h"
#include "streamreader.h"


void ShapePoint::deserialize(std::ifstream& in)
{
    read(in, &mX);
    read(in, &mY);
}


double ShapePoint::getX() const
{
    return mX;
}


void ShapePoint::setX(double value)
{
    mX = value;
}


double ShapePoint::getY() const
{
    return mY;
}


void ShapePoint::setY(double value)
{
    mY = value;
}




