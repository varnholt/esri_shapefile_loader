// libFuzzer entry point: every input must load or fail cleanly, without sanitizer reports.
//   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DTINYSHP_BUILD_FUZZER=ON
//   python tests/fuzz/make_corpus.py data corpus
//   ./build-fuzz/tests/fuzz/tinyshp_fuzz corpus -max_total_time=60
//
// The first two bytes split the rest of the input into a .shp and a .dbf, so both parsers and their pairing are fuzzed.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <tinyshp/tinyshp.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
   if (size < 2)
   {
      return 0;
   }
   const auto bytes = std::as_bytes(std::span{data, size}).subspan(2);
   const std::size_t split = std::min<std::size_t>((std::size_t{data[0]} << 8 | data[1]) * 4, bytes.size());

   const auto file = tinyshp::load(tinyshp::Sources{bytes.first(split), bytes.subspan(split), {}, {}});
   if (file)
   {
      for (const auto& shape : file->_shapes)
      {
         if ((!shape._z.empty() && shape._z.size() != shape._points.size()) ||
             (!shape._m.empty() && shape._m.size() != shape._points.size()))
         {
            __builtin_trap();
         }
         for (std::size_t i = 0; i < shape.partCount(); ++i)
         {
            (void)shape.part(i);
         }
         (void)tinyshp::polygons(shape);
      }
   }
   (void)tinyshp::load(bytes);
   return 0;
}
