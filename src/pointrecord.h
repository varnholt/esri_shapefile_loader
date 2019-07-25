#pragma once

#include <cstdint>
#include <fstream>

#include "recordheader.h"
#include "shapepoint.h"


class PointRecord
{

public:

    PointRecord() = default;

    void deserialize(std::ifstream& in);

    const ShapePoint& getPoint() const;
    void setPoint(const ShapePoint &value);


protected:

    RecordHeader mRecordHeader;
    ShapePoint mPoint;
    int32_t mShapeType = 0;
};

