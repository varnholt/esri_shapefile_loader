#include "recordheader.h"



void RecordHeader::deserialize(QDataStream &in)
{
    in >> mRecordNumber;
    in >> mContentLength;
}
