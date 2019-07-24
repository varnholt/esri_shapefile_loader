// header
#include "pointrecord.h"

// shapeloader
#include "endian.h"


void PointRecord::deserialize(QDataStream &in)
{
    // read record header
    mRecordHeader.deserialize(in);

    // read shape type
    int32_t shapeType;
    in >> shapeType;
    shapeType = Endian::swapEndian(shapeType);
    mShapeType = shapeType;

    // read point
    mPoint.deserialize(in);
}


const ShapePoint& PointRecord::getPoint() const
{
    return mPoint;
}


void PointRecord::setPoint(const ShapePoint &value)
{
    mPoint = value;
}



