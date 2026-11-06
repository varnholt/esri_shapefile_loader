// prints what a shapefile contains: header, record counts, projection, fields and the first records.
//
//   shp_info file.shp [records to show, default 5]

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <map>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include <tinyshp/tinyshp.hpp>

int main(int argc, char* argv[])
{
   const std::span args(argv, static_cast<std::size_t>(argc));
   if (args.size() < 2)
   {
      std::println(stderr, "usage: shp_info file.shp [records to show]");
      return 2;
   }
   std::size_t show = 5;
   if (args.size() > 2)
   {
      const std::string_view count = args[2];
      std::from_chars(count.data(), count.data() + count.size(), show);
   }

   const auto file = tinyshp::loadFile(args[1]);
   if (!file)
   {
      std::println(stderr, "{}: {}", args[1], tinyshp::describe(file.error()));
      return 1;
   }

   const auto& header = file->_header;
   std::println("{}, {} records, {} bytes", tinyshp::describe(header._type), file->_shapes.size(), header._file_length);
   std::println(
      "bounds x {} .. {}, y {} .. {}", header._bounds._x_min, header._bounds._x_max, header._bounds._y_min, header._bounds._y_max
   );
   if (tinyshp::hasZ(header._type))
   {
      std::println("z {} .. {}", header._z_range._min, header._z_range._max);
   }

   std::size_t nulls = 0;
   std::size_t parts = 0;
   std::size_t points = 0;
   std::map<tinyshp::PartType, std::size_t> part_types;
   for (const auto& shape : file->_shapes)
   {
      nulls += shape.isNull() ? 1 : 0;
      parts += shape.partCount();
      points += shape._points.size();
      for (const auto type : shape._part_types)
      {
         ++part_types[type];
      }
   }
   std::println("{} parts, {} points, {} null records", parts, points, nulls);
   for (const auto& [type, count] : part_types)
   {
      static constexpr std::string_view names[] = {"triangle strip", "triangle fan", "outer ring", "inner ring", "first ring", "ring"};
      std::println("  {:>6} {}", count, names[static_cast<std::size_t>(type)]);
   }

   if (!file->_projection.empty())
   {
      std::println("projection: {}{}", file->_projection.substr(0, 100), file->_projection.size() > 100 ? "..." : "");
   }

   if (!file->_attributes)
   {
      std::println("no .dbf");
      return 0;
   }
   const auto& table = *file->_attributes;
   std::println("{} fields:", table._fields.size());
   for (const auto& field : table._fields)
   {
      std::println("  {:<12} {} {:>3}.{}", field._name, static_cast<char>(field._type), field._length, field._decimals);
   }

   for (std::size_t i = 0; i < std::min(show, file->_shapes.size()); ++i)
   {
      const auto& shape = file->_shapes[i];
      std::println("record {}: {} with {} points", shape._record_number, tinyshp::describe(shape._type), shape._points.size());
      for (std::size_t f = 0; f < table._fields.size() && f < 12; ++f)
      {
         const auto text = tinyshp::toString(table._records[i]._values[f]);
         if (!text.empty())
         {
            std::println("  {:<12} {}", table._fields[f]._name, text);
         }
      }
   }
   return 0;
}
