#include "pointrecord.h"

#include "endian.h"
#include "streamreader.h"


void PointRecord::deserialize(std::ifstream& in)
{
    // read record header
    mRecordHeader.deserialize(in);

    // read shape type
    int32_t shapeType;
    read(in, &shapeType);
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



