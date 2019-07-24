#pragma once

// Qt
#include <QDataStream>
#include <cstdint>


class RecordHeader
{
public:

    RecordHeader() = default;
    virtual ~RecordHeader() = default;
    virtual void deserialize(QDataStream& in);

protected:

    // big endian
    int32_t mRecordNumber;
    int32_t mContentLength;
};

