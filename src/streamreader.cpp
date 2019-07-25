#include "streamreader.h"

void read(std::ifstream& in, int32_t* a)
{
    in.read(reinterpret_cast<char*>(a), sizeof(*a));
}

void read(std::ifstream& in, double* a)
{
    in.read(reinterpret_cast<char*>(a), sizeof(*a));
}
