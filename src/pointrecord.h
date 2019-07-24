#pragma once

// Qt
#include <QDataStream>

// shapeloader
#include "recordheader.h"
#include "shapepoint.h"

#include <cstdint>

class PointRecord
{

public:

    PointRecord() = default;

    void deserialize(QDataStream& in);

    const ShapePoint& getPoint() const;
    void setPoint(const ShapePoint &value);


protected:

    RecordHeader mRecordHeader;
    ShapePoint mPoint;
    int32_t mShapeType = 0;
};

