#include "shapeheader.h"

// endian
#include "endian.h"


void ShapeHeader::deserialize(QDataStream & in)
{
    int32_t shapeType = 0;

    // big endian part
    in >> mFileCode;
    in >> mUnused1;
    in >> mUnused2;
    in >> mUnused3;
    in >> mUnused4;
    in >> mUnused5;
    in >> mFileLength;

    // little endian part
    in >> mVersion;
    in >> shapeType;

    in >> mXMin;
    in >> mYMin;
    in >> mXMax;
    in >> mYMax;
    in >> mZMin;
    in >> mZMax;
    in >> mMMin;
    in >> mMMax;

    // swap endian
    mVersion = Endian::swapEndian<int32_t>(mVersion);
    shapeType = Endian::swapEndian<int32_t>(shapeType);

    // bounding box
    mXMin = Endian::swapEndian<double>(mXMin);
    mXMax = Endian::swapEndian<double>(mXMax);

    mYMin = Endian::swapEndian<double>(mYMin);
    mYMax = Endian::swapEndian<double>(mYMax);

    mZMin = Endian::swapEndian<double>(mZMin);
    mZMax = Endian::swapEndian<double>(mZMax);

    mMMin = Endian::swapEndian<double>(mMMin);
    mMMax = Endian::swapEndian<double>(mMMax);

    mShapeType = static_cast<Shape::ShapeType>(shapeType);
}


void ShapeHeader::debug()
{
    qDebug(
        "shape type %d\n"
        "bounding rect: x: {%f, %f} y: {%f, %f}, z: {%f, %f}, m: {%f, %f}",
        mShapeType,
        mXMin, mXMax,
        mYMin, mYMax,
        mZMin, mZMax,
        mMMin, mMMax
    );
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

