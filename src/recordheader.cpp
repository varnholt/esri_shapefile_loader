#include "recordheader.h"

#include "streamreader.h"


void RecordHeader::deserialize(std::ifstream& in)
{
    read(in, &mRecordNumber);
    read(in, &mContentLength);
}
