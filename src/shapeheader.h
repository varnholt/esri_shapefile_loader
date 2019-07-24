#pragma once

// Qt
#include <Qt>
#include <QDataStream>

#include <cstdint>

// shapeloader
#include "shape.h"


class ShapeHeader
{

public:

    ShapeHeader() = default;

    void deserialize(QDataStream & in);
    void debug();

    Shape::ShapeType getShapeType() const;
    void setShapeType(const Shape::ShapeType &value);

    double getXMin() const;
    void setXMin(double value);

    double getYMin() const;
    void setYMin(double value);

    double getXMax() const;
    void setXMax(double value);

    double getYMax() const;
    void setYMax(double value);


protected:

    // header (big endian)
    int32_t mFileCode = 0;
    int32_t mUnused1 = 0;
    int32_t mUnused2 = 0;
    int32_t mUnused3 = 0;
    int32_t mUnused4 = 0;
    int32_t mUnused5 = 0;
    int32_t mFileLength = 0;

    // header (little endian)
    int32_t mVersion = 0;
    Shape::ShapeType mShapeType = Shape::ShapeType::ShapeTypeNullShape;

    // bounding box (little endian)
    double mXMin = 0.0;
    double mYMin = 0.0;
    double mXMax = 0.0;
    double mYMax = 0.0;
    double mZMin = 0.0;
    double mZMax = 0.0;
    double mMMin = 0.0;
    double mMMax = 0.0;
};

