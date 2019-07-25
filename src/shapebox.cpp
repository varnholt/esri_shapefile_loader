#include "shapebox.h"

#include "endian.h"
#include "streamreader.h"


void ShapeBox::deserialize(std::ifstream& in)
{
    read(in, &mXMin);
    read(in, &mYMin);
    read(in, &mXMax);
    read(in, &mYmax);
}

