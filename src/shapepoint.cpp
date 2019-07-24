// header
#include "shapepoint.h"

// shapeloader
#include "endian.h"


void ShapePoint::deserialize(QDataStream &in)
{
    in >> mX;
    in >> mY;

    mX = Endian::swapEndian(mX);
    mY = Endian::swapEndian(mY);

    // qDebug("point: %f, %f", mX, mY);
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




