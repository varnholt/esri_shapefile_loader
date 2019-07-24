// header
#include "shapebox.h"

// shapeloader
#include "endian.h"


void ShapeBox::deserialize(QDataStream &in)
{
    in >> mXMin;
    in >> mYMin;
    in >> mXMax;
    in >> mYmax;

    mXMin = Endian::swapEndian(mXMin);
    mYMin = Endian::swapEndian(mYMin);
    mXMax = Endian::swapEndian(mXMax);
    mYmax = Endian::swapEndian(mYmax);
}

