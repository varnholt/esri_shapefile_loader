#include "shapeheader.h"

#include "endian.h"
#include "streamreader.h"


void ShapeHeader::deserialize(std::ifstream& in)
{
    int32_t shapeType = 0;

    // big endian part
    read(in, &mFileCode);
    read(in, &mUnused1);
    read(in, &mUnused2);
    read(in, &mUnused3);
    read(in, &mUnused4);
    read(in, &mUnused5);
    read(in, &mFileLength);

    // little endian part
    read(in, &mVersion);
    read(in, &shapeType);

    read(in, &mXMin);
    read(in, &mYMin);
    read(in, &mXMax);
    read(in, &mYMax);
    read(in, &mZMin);
    read(in, &mZMax);
    read(in, &mMMin);
    read(in, &mMMax);

    // swap endian
    mFileCode = swapEndian<int32_t>(mFileCode);
    mFileLength = swapEndian<int32_t>(mFileLength);

    mShapeType = static_cast<Shape::ShapeType>(shapeType);
}


Shape::ShapeType ShapeHeader::getShapeType() const
{
    return mShapeType;
}


void ShapeHeader::setShapeType(const Shape::ShapeType &value)
{
    mShapeType = value;
}


double ShapeHeader::getXMin() const
{
    return mXMin;
}


void ShapeHeader::setXMin(double value)
{
    mXMin = value;
}


double ShapeHeader::getYMin() const
{
    return mYMin;
}


void ShapeHeader::setYMin(double value)
{
    mYMin = value;
}


double ShapeHeader::getXMax() const
{
    return mXMax;
}


void ShapeHeader::setXMax(double value)
{
    mXMax = value;
}


double ShapeHeader::getYMax() const
{
    return mYMax;
}


void ShapeHeader::setYMax(double value)
{
    mYMax = value;
}

