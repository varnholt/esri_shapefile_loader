#include <array>
#include <cstddef>
#include <print>

#include <tinyshp/tinyshp.hpp>

int main()
{
   // not a shapefile: proves the header compiles, links and reports errors
   constexpr std::array<std::byte, 4> data{std::byte{'G'}, std::byte{'I'}, std::byte{'F'}, std::byte{'8'}};
   const auto file = tinyshp::load(data);
   std::println(
      "tinyshp {}.{}.{}: {}",
      tinyshp::version.major,
      tinyshp::version.minor,
      tinyshp::version.patch,
      file ? "loaded" : tinyshp::describe(file.error())
   );
   return file ? 1 : 0;
}
