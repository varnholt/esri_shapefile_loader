// converts a shapefile and its attributes to GeoJSON (RFC 7946).
//
//   shp2geojson file.shp out.geojson
//
// Points become Point, multipoints MultiPoint, polylines LineString or MultiLineString, polygons and multipatch rings
// MultiPolygon with the outer rings counterclockwise as RFC 7946 wants. z is kept as the third coordinate. GeoJSON is
// longitude and latitude by definition: files in a projected coordinate system are written as they are, with a warning.

#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <variant>

#include <tinyshp/tinyshp.hpp>

namespace
{

std::string number(double value)
{
   return std::isfinite(value) ? std::format("{}", value) : "null";
}

std::string jsonString(std::string_view text)
{
   std::string out = "\"";
   for (const char c : text)
   {
      switch (c)
      {
         case '"':
            out += "\\\"";
            break;
         case '\\':
            out += "\\\\";
            break;
         case '\n':
            out += "\\n";
            break;
         case '\r':
            out += "\\r";
            break;
         case '\t':
            out += "\\t";
            break;
         default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
               out += std::format("\\u{:04x}", static_cast<int>(c));
            }
            else
            {
               out += c;
            }
      }
   }
   return out + "\"";
}

std::string value(const tinyshp::Value& field)
{
   if (std::holds_alternative<std::monostate>(field))
   {
      return "null";
   }
   if (const auto* text = std::get_if<std::string>(&field))
   {
      return jsonString(*text);
   }
   if (const auto* real = std::get_if<double>(&field))
   {
      return number(*real);
   }
   if (const auto* flag = std::get_if<bool>(&field))
   {
      return *flag ? "true" : "false";
   }
   return jsonString(tinyshp::toString(field));  // dates as yyyy-mm-dd
}

/// \brief the position of point i, with z if the shape has it.
std::string position(const tinyshp::Shape& shape, std::size_t i)
{
   const auto& point = shape._points[i];
   if (!shape._z.empty())
   {
      return std::format("[{},{},{}]", number(point._x), number(point._y), number(shape._z[i]));
   }
   return std::format("[{},{}]", number(point._x), number(point._y));
}

/// \brief a ring or line as a coordinate array. rings are given as spans into shape._points and may be reversed.
std::string coordinates(const tinyshp::Shape& shape, std::span<const tinyshp::Point> points, bool reverse = false)
{
   const auto first = static_cast<std::size_t>(points.data() - shape._points.data());
   std::string out = "[";
   for (std::size_t k = 0; k < points.size(); ++k)
   {
      const auto i = first + (reverse ? points.size() - 1 - k : k);
      out += (k > 0 ? "," : "") + position(shape, i);
   }
   return out + "]";
}

std::string geometry(const tinyshp::Shape& shape)
{
   switch (tinyshp::baseType(shape._type))
   {
      case tinyshp::ShapeType::Point:
         return std::format(R"({{"type":"Point","coordinates":{}}})", position(shape, 0));
      case tinyshp::ShapeType::MultiPoint:
         return std::format(R"({{"type":"MultiPoint","coordinates":{}}})", coordinates(shape, shape._points));
      case tinyshp::ShapeType::PolyLine:
      {
         if (shape.partCount() == 1)
         {
            return std::format(R"({{"type":"LineString","coordinates":{}}})", coordinates(shape, shape.part(0)));
         }
         std::string lines;
         for (std::size_t i = 0; i < shape.partCount(); ++i)
         {
            lines += (i > 0 ? "," : "") + coordinates(shape, shape.part(i));
         }
         return std::format(R"({{"type":"MultiLineString","coordinates":[{}]}})", lines);
      }
      case tinyshp::ShapeType::Polygon:
      case tinyshp::ShapeType::MultiPatch:
      {
         // shapefile outer rings are clockwise, geojson's counterclockwise
         std::string polygons;
         for (const auto& polygon : tinyshp::polygons(shape))
         {
            std::string rings = coordinates(shape, polygon._outer, tinyshp::isClockwise(polygon._outer));
            for (const auto& hole : polygon._holes)
            {
               rings += "," + coordinates(shape, hole, !tinyshp::isClockwise(hole));
            }
            polygons += (polygons.empty() ? "[" : ",[") + rings + "]";
         }
         return std::format(R"({{"type":"MultiPolygon","coordinates":[{}]}})", polygons);
      }
      default:
         return "null";
   }
}

}  // namespace

int main(int argc, char* argv[])
{
   const std::span args(argv, static_cast<std::size_t>(argc));
   if (args.size() != 3)
   {
      std::println(stderr, "usage: shp2geojson file.shp out.geojson");
      return 2;
   }
   const auto file = tinyshp::loadFile(args[1]);
   if (!file)
   {
      std::println(stderr, "{}: {}", args[1], tinyshp::describe(file.error()));
      return 1;
   }
   if (!file->_projection.empty() && !file->isGeographic())
   {
      std::println(stderr, "warning: the coordinates are projected, geojson expects longitude and latitude");
   }

   std::ofstream out(args[2], std::ios::binary);
   out << R"({"type":"FeatureCollection","features":[)" << '\n';
   for (std::size_t i = 0; i < file->_shapes.size(); ++i)
   {
      std::string properties;
      if (file->_attributes)
      {
         const auto& table = *file->_attributes;
         for (std::size_t f = 0; f < table._fields.size(); ++f)
         {
            properties += (f > 0 ? "," : "") + jsonString(table._fields[f]._name) + ":" + value(table._records[i]._values[f]);
         }
      }
      const auto& shape = file->_shapes[i];
      out << std::format(
         R"({{"type":"Feature","geometry":{},"properties":{{{}}}}}{})",
         shape.isNull() ? "null" : geometry(shape),
         properties,
         i + 1 < file->_shapes.size() ? ",\n" : "\n"
      );
   }
   out << "]}\n";
   if (!out)
   {
      std::println(stderr, "could not write {}", args[2]);
      return 1;
   }
   std::println("wrote {} features to {}", file->_shapes.size(), args[2]);
   return 0;
}
