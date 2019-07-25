#pragma once

#include <cstdint>
#include <fstream>

class RecordHeader
{
public:

    RecordHeader() = default;
    virtual ~RecordHeader() = default;
    virtual void deserialize(std::ifstream& in);

protected:

    // big endian
    int32_t mRecordNumber;
    int32_t mContentLength;
};

