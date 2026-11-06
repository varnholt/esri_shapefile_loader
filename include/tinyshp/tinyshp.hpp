// tinyshp: ESRI shapefile loader (.shp geometry, .dbf attributes, .prj projection), C++23, single header.
// https://github.com/varnholt/tinyshp
//
// Follows the ESRI Shapefile Technical Description (July 1998) and the dBase III table format.
// Every read is bounds checked and every count is checked against the bytes that are left before anything is
// allocated, so untrusted files fail with an error instead of crashing or allocating gigabytes.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace tinyshp
{

struct Version
{
   int32_t major = 0;
   int32_t minor = 0;
   int32_t patch = 0;
};

inline constexpr Version version{1, 0, 0};

// ---------------------------------------------------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------------------------------------------------

enum class ShapeType : int32_t
{
   Null = 0,
   Point = 1,
   PolyLine = 3,
   Polygon = 5,
   MultiPoint = 8,
   PointZ = 11,
   PolyLineZ = 13,
   PolygonZ = 15,
   MultiPointZ = 18,
   PointM = 21,
   PolyLineM = 23,
   PolygonM = 25,
   MultiPointM = 28,
   MultiPatch = 31,
};

/// \brief the shape type without z or m: Point, PolyLine, Polygon, MultiPoint, MultiPatch or Null.
[[nodiscard]] constexpr ShapeType baseType(ShapeType type)
{
   switch (type)
   {
      case ShapeType::PointZ:
      case ShapeType::PointM:
         return ShapeType::Point;
      case ShapeType::PolyLineZ:
      case ShapeType::PolyLineM:
         return ShapeType::PolyLine;
      case ShapeType::PolygonZ:
      case ShapeType::PolygonM:
         return ShapeType::Polygon;
      case ShapeType::MultiPointZ:
      case ShapeType::MultiPointM:
         return ShapeType::MultiPoint;
      default:
         return type;
   }
}

/// \brief true for the types that carry a z coordinate per point (the Z types and MultiPatch).
[[nodiscard]] constexpr bool hasZ(ShapeType type)
{
   return type == ShapeType::PointZ || type == ShapeType::PolyLineZ || type == ShapeType::PolygonZ ||
          type == ShapeType::MultiPointZ || type == ShapeType::MultiPatch;
}

/// \brief true for the types that may carry a measure per point. for the Z types and MultiPatch it is optional.
[[nodiscard]] constexpr bool mayHaveM(ShapeType type)
{
   return hasZ(type) || type == ShapeType::PointM || type == ShapeType::PolyLineM || type == ShapeType::PolygonM ||
          type == ShapeType::MultiPointM;
}

[[nodiscard]] constexpr bool isKnown(int32_t type)
{
   switch (type)
   {
      case 0:
      case 1:
      case 3:
      case 5:
      case 8:
      case 11:
      case 13:
      case 15:
      case 18:
      case 21:
      case 23:
      case 25:
      case 28:
      case 31:
         return true;
      default:
         return false;
   }
}

[[nodiscard]] constexpr std::string_view describe(ShapeType type)
{
   switch (type)
   {
      case ShapeType::Null:
         return "Null";
      case ShapeType::Point:
         return "Point";
      case ShapeType::PolyLine:
         return "PolyLine";
      case ShapeType::Polygon:
         return "Polygon";
      case ShapeType::MultiPoint:
         return "MultiPoint";
      case ShapeType::PointZ:
         return "PointZ";
      case ShapeType::PolyLineZ:
         return "PolyLineZ";
      case ShapeType::PolygonZ:
         return "PolygonZ";
      case ShapeType::MultiPointZ:
         return "MultiPointZ";
      case ShapeType::PointM:
         return "PointM";
      case ShapeType::PolyLineM:
         return "PolyLineM";
      case ShapeType::PolygonM:
         return "PolygonM";
      case ShapeType::MultiPointM:
         return "MultiPointM";
      case ShapeType::MultiPatch:
         return "MultiPatch";
   }
   return "unknown";
}

/// \brief how a MultiPatch part is to be read.
enum class PartType : int32_t
{
   TriangleStrip = 0,
   TriangleFan = 1,
   OuterRing = 2,
   InnerRing = 3,
   FirstRing = 4,
   Ring = 5,
};

struct Point
{
   double _x = 0.0;
   double _y = 0.0;

   friend constexpr bool operator==(const Point&, const Point&) = default;
};

struct Box
{
   double _x_min = 0.0;
   double _y_min = 0.0;
   double _x_max = 0.0;
   double _y_max = 0.0;

   [[nodiscard]] constexpr double width() const
   {
      return _x_max - _x_min;
   }

   [[nodiscard]] constexpr double height() const
   {
      return _y_max - _y_min;
   }

   [[nodiscard]] constexpr bool contains(Point point) const
   {
      return point._x >= _x_min && point._x <= _x_max && point._y >= _y_min && point._y <= _y_max;
   }

   friend constexpr bool operator==(const Box&, const Box&) = default;
};

struct Range
{
   double _min = 0.0;
   double _max = 0.0;

   friend constexpr bool operator==(const Range&, const Range&) = default;
};

/// \brief measures below -10^38 mean "no data" (shapefile specification, page 2).
[[nodiscard]] constexpr bool isNoData(double measure)
{
   return measure < -1e38;
}

/// \brief one record of the .shp file.
///
/// Point types have one point and no parts. MultiPoint types have their points and no parts. PolyLine, Polygon and
/// MultiPatch have parts: _parts[i] is the index of the first point of part i, and part(i) returns its points.
struct Shape
{
   int32_t _record_number = 0;  // 1-based, as stored
   ShapeType _type = ShapeType::Null;
   Box _bounds;                       // as stored; for points it is the point itself
   std::vector<uint32_t> _parts;      // first point of each part
   std::vector<PartType> _part_types; // MultiPatch only, one per part
   std::vector<Point> _points;
   std::vector<double> _z;  // one per point for the Z types and MultiPatch, otherwise empty
   std::vector<double> _m;  // one per point when the record has measures, otherwise empty; see isNoData()
   Range _z_range;
   Range _m_range;

   [[nodiscard]] bool isNull() const
   {
      return _type == ShapeType::Null;
   }

   /// \brief the number of parts; points and multipoints count as a single part.
   [[nodiscard]] std::size_t partCount() const
   {
      if (!_parts.empty())
      {
         return _parts.size();
      }
      return _points.empty() ? 0 : 1;
   }

   /// \brief the index range [first, last) of part `index` in _points.
   [[nodiscard]] std::pair<std::size_t, std::size_t> partRange(std::size_t index) const
   {
      if (_parts.empty())
      {
         return {0, _points.size()};
      }
      const std::size_t first = _parts[index];
      const std::size_t last = index + 1 < _parts.size() ? _parts[index + 1] : _points.size();
      return {first, last};
   }

   [[nodiscard]] std::span<const Point> part(std::size_t index) const
   {
      const auto [first, last] = partRange(index);
      return std::span<const Point>{_points}.subspan(first, last - first);
   }
};

/// \brief the signed area of a ring: positive for counterclockwise rings (with y up), negative for clockwise ones.
[[nodiscard]] inline double signedArea(std::span<const Point> ring)
{
   if (ring.size() < 3)
   {
      return 0.0;
   }
   // relative to the first point, which keeps precision for projected coordinates in the millions
   const Point origin = ring.front();
   double sum = 0.0;
   for (std::size_t i = 1; i + 1 < ring.size(); ++i)
   {
      const double ax = ring[i]._x - origin._x;
      const double ay = ring[i]._y - origin._y;
      const double bx = ring[i + 1]._x - origin._x;
      const double by = ring[i + 1]._y - origin._y;
      sum += ax * by - bx * ay;
   }
   return sum * 0.5;
}

/// \brief the specification wants outer rings clockwise and holes counterclockwise.
[[nodiscard]] inline bool isClockwise(std::span<const Point> ring)
{
   return signedArea(ring) < 0.0;
}

/// \brief even-odd point in ring test; the ring may or may not repeat its first point at the end.
[[nodiscard]] inline bool contains(std::span<const Point> ring, Point point)
{
   bool inside = false;
   const std::size_t count = ring.size();
   for (std::size_t i = 0, j = count - 1; i < count; j = i++)
   {
      const Point& a = ring[i];
      const Point& b = ring[j];
      if ((a._y > point._y) != (b._y > point._y) &&
          point._x < (b._x - a._x) * (point._y - a._y) / (b._y - a._y) + a._x)
      {
         inside = !inside;
      }
   }
   return inside;
}

/// \brief true if a polygon shape covers the point: even-odd over all its rings, so holes are respected.
[[nodiscard]] inline bool contains(const Shape& shape, Point point)
{
   const auto base = baseType(shape._type);
   if ((base != ShapeType::Polygon && base != ShapeType::MultiPatch) || !shape._bounds.contains(point))
   {
      return false;
   }
   bool inside = false;
   for (std::size_t i = 0; i < shape.partCount(); ++i)
   {
      if (shape._type == ShapeType::MultiPatch && i < shape._part_types.size() &&
          (shape._part_types[i] == PartType::TriangleStrip || shape._part_types[i] == PartType::TriangleFan))
      {
         continue;
      }
      if (contains(shape.part(i), point))
      {
         inside = !inside;
      }
   }
   return inside;
}

/// \brief an outer ring and its holes.
struct Polygon
{
   std::span<const Point> _outer;
   std::vector<std::span<const Point>> _holes;
};

/// \brief splits a polygon shape into outer rings with their holes.
///
/// Clockwise rings are outer rings and counterclockwise rings are holes, as the specification says. Each hole goes to
/// the smallest outer ring that contains it; a hole outside every outer ring is returned as an outer ring of its own,
/// since such files exist and drawing the ring beats dropping it. MultiPatch rings use their part types instead: inner
/// rings are holes of the ring before them, every other ring is an outer ring, triangle strips and fans are skipped. The spans point into `shape`, which has to outlive the result.
[[nodiscard]] inline std::vector<Polygon> polygons(const Shape& shape)
{
   std::vector<Polygon> result;
   const auto base = baseType(shape._type);

   if (base == ShapeType::MultiPatch)
   {
      for (std::size_t i = 0; i < shape.partCount(); ++i)
      {
         const PartType type = i < shape._part_types.size() ? shape._part_types[i] : PartType::Ring;
         if (type == PartType::TriangleStrip || type == PartType::TriangleFan)
         {
            continue;
         }
         if (type == PartType::InnerRing && !result.empty())
         {
            result.back()._holes.push_back(shape.part(i));
         }
         else
         {
            result.push_back({shape.part(i), {}});
         }
      }
      return result;
   }

   if (base != ShapeType::Polygon)
   {
      return result;
   }

   std::vector<double> outer_areas;
   std::vector<std::span<const Point>> holes;
   for (std::size_t i = 0; i < shape.partCount(); ++i)
   {
      const auto ring = shape.part(i);
      if (ring.size() < 3)
      {
         continue;
      }
      const double area = signedArea(ring);
      if (area < 0.0)
      {
         result.push_back({ring, {}});
         outer_areas.push_back(-area);
      }
      else
      {
         holes.push_back(ring);
      }
   }

   for (const auto& hole : holes)
   {
      std::optional<std::size_t> best;
      for (std::size_t i = 0; i < outer_areas.size(); ++i)
      {
         if (contains(result[i]._outer, hole.front()) && (!best || outer_areas[i] < outer_areas[*best]))
         {
            best = i;
         }
      }
      if (best)
      {
         result[*best]._holes.push_back(hole);
      }
      else
      {
         result.push_back({hole, {}});
         outer_areas.push_back(std::abs(signedArea(hole)));
      }
   }
   return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// attributes
// ---------------------------------------------------------------------------------------------------------------------

enum class FieldType : char
{
   Character = 'C',
   Number = 'N',
   Float = 'F',
   Logical = 'L',
   Date = 'D',
   Memo = 'M',
   Integer = 'I',  // 4 byte binary integer (Visual FoxPro)
   Double = 'O',   // 8 byte binary double
   // other type letters are kept as they are in the file, their values read as nulls
};

struct Date
{
   int32_t _year = 0;
   int32_t _month = 0;
   int32_t _day = 0;

   friend constexpr bool operator==(const Date&, const Date&) = default;
};

/// \brief a field value. std::monostate is a null: a blank number or date, a '?' logical, or a field type tinyshp does
/// not decode (memos and binary types other than I and O).
using Value = std::variant<std::monostate, std::string, double, bool, Date>;

struct Field
{
   std::string _name;
   FieldType _type = FieldType::Character;
   uint8_t _length = 0;
   uint8_t _decimals = 0;
};

struct Record
{
   bool _deleted = false;
   std::vector<Value> _values;  // one per field
};

/// \brief formats a value for display: text as is, numbers without needless decimals, dates as yyyy-mm-dd.
[[nodiscard]] inline std::string toString(const Value& value)
{
   struct Visitor
   {
      std::string operator()(std::monostate) const
      {
         return {};
      }
      std::string operator()(const std::string& text) const
      {
         return text;
      }
      std::string operator()(double number) const
      {
         if (std::isfinite(number) && number == std::floor(number) && std::abs(number) < 1e15)
         {
            return std::to_string(static_cast<int64_t>(number));
         }
         std::array<char, 64> buffer{};
         const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
         return error == std::errc{} ? std::string(buffer.data(), end) : std::string{};
      }
      std::string operator()(bool flag) const
      {
         return flag ? "true" : "false";
      }
      std::string operator()(const Date& date) const
      {
         std::string text(10, '0');
         const auto put = [&text](std::size_t at, int32_t number, std::size_t digits)
         {
            for (std::size_t i = 0; i < digits; ++i)
            {
               text[at + digits - 1 - i] = static_cast<char>('0' + number % 10);
               number /= 10;
            }
         };
         put(0, date._year, 4);
         text[4] = '-';
         put(5, date._month, 2);
         text[7] = '-';
         put(8, date._day, 2);
         return text;
      }
   };
   return std::visit(Visitor{}, value);
}

/// \brief the .dbf table: one record per shape, in the same order.
struct Table
{
   std::vector<Field> _fields;
   std::vector<Record> _records;

   /// \brief index of the field with that name, ignoring case.
   [[nodiscard]] std::optional<std::size_t> fieldIndex(std::string_view name) const
   {
      for (std::size_t i = 0; i < _fields.size(); ++i)
      {
         const auto& field_name = _fields[i]._name;
         if (field_name.size() == name.size() &&
             std::ranges::equal(
                field_name,
                name,
                [](char a, char b)
                { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
             ))
         {
            return i;
         }
      }
      return std::nullopt;
   }

   /// \brief the value of a field in a record, or nullptr if there is no such record or field.
   [[nodiscard]] const Value* find(std::size_t record, std::string_view field) const
   {
      const auto index = fieldIndex(field);
      if (!index || record >= _records.size())
      {
         return nullptr;
      }
      return &_records[record]._values[*index];
   }

   /// \brief any value as text, empty for nulls and missing fields.
   [[nodiscard]] std::string text(std::size_t record, std::string_view field) const
   {
      const Value* value = find(record, field);
      return value ? toString(*value) : std::string{};
   }

   /// \brief a numeric value, or nothing for nulls, missing fields and other types.
   [[nodiscard]] std::optional<double> number(std::size_t record, std::string_view field) const
   {
      const Value* value = find(record, field);
      if (value == nullptr)
      {
         return std::nullopt;
      }
      if (const auto* number = std::get_if<double>(value))
      {
         return *number;
      }
      return std::nullopt;
   }
};

// ---------------------------------------------------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------------------------------------------------

struct Header
{
   int32_t _file_length = 0;  // in bytes, as declared
   int32_t _version = 0;
   ShapeType _type = ShapeType::Null;
   Box _bounds;
   Range _z_range;
   Range _m_range;
};

struct Shapefile
{
   Header _header;
   std::vector<Shape> _shapes;
   std::optional<Table> _attributes;  // from the .dbf, if there was one
   std::string _projection;           // the .prj text (well-known text), empty if there was none

   /// \brief true if the .prj names a geographic coordinate system, i.e. the coordinates are longitude and latitude.
   [[nodiscard]] bool isGeographic() const
   {
      const auto start = _projection.find_first_not_of(" \t\r\n");
      return start != std::string::npos && _projection.compare(start, 6, "GEOGCS") == 0;
   }
};

/// \brief how text in the .dbf is encoded. tinyshp always returns utf-8.
enum class Encoding
{
   Auto,         // the .cpg if there is one, otherwise each value is kept if it is valid utf-8 and read as Windows-1252
                 // if it is not
   Utf8,         // invalid sequences become U+FFFD
   Windows1252,  // also right for ISO-8859-1 text
};

struct LoadOptions
{
   Encoding _encoding = Encoding::Auto;
   bool _load_attributes = true;  // loadFile(): read the .dbf next to the .shp
   bool _skip_deleted = false;    // keep deleted .dbf records (flagged) so record i stays shape i
};

/// \brief the parts of a shapefile in memory, for load(). Empty members are absent.
struct Sources
{
   std::span<const std::byte> _shp;
   std::span<const std::byte> _dbf;
   std::string_view _prj;
   std::string_view _cpg;
};

enum class ErrorCode
{
   FileNotFound,
   ReadFailed,
   NotAShapefile,
   NotADbaseTable,
   Truncated,
   Corrupt,
   UnsupportedShapeType,
   UnsupportedDbaseVersion,
   RecordCountMismatch,
};

struct Error
{
   ErrorCode _code = ErrorCode::Corrupt;
   std::size_t _offset = 0;  // byte offset in the file the error is about
   std::string _detail;      // starts with "dbf: " for errors in the attribute table
};

[[nodiscard]] constexpr std::string_view describe(ErrorCode code)
{
   switch (code)
   {
      case ErrorCode::FileNotFound:
         return "file not found";
      case ErrorCode::ReadFailed:
         return "file could not be read";
      case ErrorCode::NotAShapefile:
         return "not a shapefile";
      case ErrorCode::NotADbaseTable:
         return "not a dBase table";
      case ErrorCode::Truncated:
         return "file is truncated";
      case ErrorCode::Corrupt:
         return "file is corrupt";
      case ErrorCode::UnsupportedShapeType:
         return "unsupported shape type";
      case ErrorCode::UnsupportedDbaseVersion:
         return "unsupported dBase version";
      case ErrorCode::RecordCountMismatch:
         return "the .dbf and the .shp have a different number of records";
   }
   return "unknown error";
}

/// \brief e.g. "file is truncated at byte 1234: polygon points"
[[nodiscard]] inline std::string describe(const Error& error)
{
   std::string text{describe(error._code)};
   if (error._code != ErrorCode::FileNotFound && error._code != ErrorCode::ReadFailed &&
       error._code != ErrorCode::RecordCountMismatch)
   {
      text += " at byte " + std::to_string(error._offset);
   }
   if (!error._detail.empty())
   {
      text += ": " + error._detail;
   }
   return text;
}

/// \brief parses a .shp file in memory.
[[nodiscard]] std::expected<Shapefile, Error> load(std::span<const std::byte> shp, const LoadOptions& options = {});

/// \brief parses a .shp with its .dbf, .prj and .cpg, all optional except the .shp.
[[nodiscard]] std::expected<Shapefile, Error> load(const Sources& sources, const LoadOptions& options = {});

/// \brief parses a .dbf file in memory. `cpg` is the content of the .cpg file, if any.
[[nodiscard]] std::expected<Table, Error>
loadTable(std::span<const std::byte> dbf, const LoadOptions& options = {}, std::string_view cpg = {});

/// \brief reads a .shp file and, next to it, the .dbf, .prj and .cpg with the same name if they exist.
[[nodiscard]] std::expected<Shapefile, Error> loadFile(const std::filesystem::path& path, const LoadOptions& options = {});

// ---------------------------------------------------------------------------------------------------------------------
// implementation
// ---------------------------------------------------------------------------------------------------------------------

namespace detail
{

inline constexpr int32_t file_code = 9994;
inline constexpr std::size_t header_size = 100;

/// \brief bounds checked reads of big and little endian values.
class Reader
{
public:
   Reader(std::span<const std::byte> data, std::size_t base = 0) : _data(data), _base(base)
   {
   }

   [[nodiscard]] std::size_t position() const
   {
      return _position;
   }

   /// \brief position in the file, for error messages.
   [[nodiscard]] std::size_t offset() const
   {
      return _base + _position;
   }

   [[nodiscard]] std::size_t remaining() const
   {
      return _data.size() - _position;
   }

   [[nodiscard]] bool has(std::size_t bytes) const
   {
      return bytes <= remaining();
   }

   void skip(std::size_t bytes)
   {
      _position += std::min(bytes, remaining());
   }

   [[nodiscard]] std::span<const std::byte> bytes(std::size_t count)
   {
      const auto result = _data.subspan(_position, count);
      _position += count;
      return result;
   }

   // the caller checks has() first; these never read past the end
   [[nodiscard]] uint32_t u32le()
   {
      return littleEndian<uint32_t>(4);
   }

   [[nodiscard]] uint32_t u32be()
   {
      return std::byteswap(u32le());
   }

   [[nodiscard]] int32_t i32le()
   {
      return std::bit_cast<int32_t>(u32le());
   }

   [[nodiscard]] int32_t i32be()
   {
      return std::bit_cast<int32_t>(u32be());
   }

   [[nodiscard]] uint16_t u16le()
   {
      return littleEndian<uint16_t>(2);
   }

   [[nodiscard]] uint8_t u8()
   {
      return littleEndian<uint8_t>(1);
   }

   [[nodiscard]] double f64le()
   {
      return std::bit_cast<double>(littleEndian<uint64_t>(8));
   }

private:
   template <typename T>
   [[nodiscard]] T littleEndian(std::size_t size)
   {
      T value{};
      if (!has(size))
      {
         _position = _data.size();
         return value;
      }
      for (std::size_t i = 0; i < size; ++i)
      {
         value = static_cast<T>(value | static_cast<T>(static_cast<T>(_data[_position + i]) << (8 * i)));
      }
      _position += size;
      return value;
   }

   std::span<const std::byte> _data;
   std::size_t _base = 0;
   std::size_t _position = 0;
};

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::size_t offset, std::string detail)
{
   return std::unexpected(Error{code, offset, std::move(detail)});
}

// --- shp ---------------------------------------------------------------------------------------------------------------

inline Box readBox(Reader& reader)
{
   Box box;
   box._x_min = reader.f64le();
   box._y_min = reader.f64le();
   box._x_max = reader.f64le();
   box._y_max = reader.f64le();
   return box;
}

inline Range readRange(Reader& reader)
{
   Range range;
   range._min = reader.f64le();
   range._max = reader.f64le();
   return range;
}

/// \brief z or m values: a range and one double per point. returns false if they do not fit.
inline bool readValues(Reader& reader, std::size_t count, Range& range, std::vector<double>& values)
{
   if (!reader.has(16) || (reader.remaining() - 16) / 8 < count)
   {
      return false;
   }
   range = readRange(reader);
   values.resize(count);
   for (auto& value : values)
   {
      value = reader.f64le();
   }
   return true;
}

/// \brief parses the content of one record, after the record header, into `shape`.
inline std::expected<void, Error> parseShape(Reader& reader, ShapeType file_type, Shape& shape)
{
   const auto start = reader.offset();
   if (!reader.has(4))
   {
      return fail(ErrorCode::Corrupt, start, "record has no shape type");
   }
   const int32_t raw_type = reader.i32le();
   if (raw_type == 0)
   {
      shape._type = ShapeType::Null;
      return {};
   }
   if (!isKnown(raw_type))
   {
      return fail(ErrorCode::UnsupportedShapeType, start, "record shape type " + std::to_string(raw_type));
   }
   const auto type = static_cast<ShapeType>(raw_type);
   if (type != file_type)
   {
      return fail(
         ErrorCode::Corrupt,
         start,
         std::string{"record is a "} + std::string{describe(type)} + " in a " + std::string{describe(file_type)} +
            " file"
      );
   }
   shape._type = type;
   const auto base = baseType(type);

   if (base == ShapeType::Point)
   {
      if (!reader.has(16))
      {
         return fail(ErrorCode::Corrupt, reader.offset(), "point record is too short");
      }
      const Point point{reader.f64le(), reader.f64le()};
      shape._points = {point};
      shape._bounds = {point._x, point._y, point._x, point._y};
      if (hasZ(type))
      {
         if (!reader.has(8))
         {
            return fail(ErrorCode::Corrupt, reader.offset(), "point record has no z");
         }
         shape._z = {reader.f64le()};
         shape._z_range = {shape._z[0], shape._z[0]};
      }
      // the measure is mandatory for PointM and optional for PointZ
      if (mayHaveM(type) && reader.has(8))
      {
         shape._m = {reader.f64le()};
         shape._m_range = {shape._m[0], shape._m[0]};
      }
      else if (type == ShapeType::PointM)
      {
         return fail(ErrorCode::Corrupt, reader.offset(), "point record has no measure");
      }
      return {};
   }

   if (!reader.has(32 + 4))
   {
      return fail(ErrorCode::Corrupt, reader.offset(), "record is too short for its bounds");
   }
   shape._bounds = readBox(reader);

   std::size_t part_count = 0;
   if (base != ShapeType::MultiPoint)
   {
      const int32_t parts = reader.i32le();
      if (parts < 0)
      {
         return fail(ErrorCode::Corrupt, reader.offset() - 4, "negative part count");
      }
      part_count = static_cast<std::size_t>(parts);
   }
   if (!reader.has(4))
   {
      return fail(ErrorCode::Corrupt, reader.offset(), "record is too short for its point count");
   }
   const int32_t points = reader.i32le();
   if (points < 0)
   {
      return fail(ErrorCode::Corrupt, reader.offset() - 4, "negative point count");
   }
   const auto point_count = static_cast<std::size_t>(points);

   // every count is checked against what is left in the record before anything is allocated
   const std::size_t part_bytes_each = base == ShapeType::MultiPatch ? 8 : 4;
   if (reader.remaining() / part_bytes_each < part_count)
   {
      return fail(ErrorCode::Corrupt, reader.offset(), std::to_string(part_count) + " parts do not fit the record");
   }
   if ((reader.remaining() - part_count * part_bytes_each) / 16 < point_count)
   {
      return fail(ErrorCode::Corrupt, reader.offset(), std::to_string(point_count) + " points do not fit the record");
   }
   if (part_count > 0 && point_count == 0)
   {
      return fail(ErrorCode::Corrupt, reader.offset(), "parts without points");
   }
   if (part_count == 0 && point_count > 0 && base != ShapeType::MultiPoint)
   {
      return fail(ErrorCode::Corrupt, reader.offset(), "points without parts");
   }

   const auto parts_offset = reader.offset();
   shape._parts.resize(part_count);
   for (auto& first : shape._parts)
   {
      first = reader.u32le();
   }
   // part starts are ascending point indices; the first one has to be 0 or points would belong to no part
   for (std::size_t i = 0; i < part_count; ++i)
   {
      const bool first_ok = i > 0 || shape._parts[0] == 0;
      const bool ascending = i == 0 || shape._parts[i] >= shape._parts[i - 1];
      if (!first_ok || !ascending || shape._parts[i] >= point_count)
      {
         return fail(ErrorCode::Corrupt, parts_offset + i * 4, "part " + std::to_string(i) + " starts at an invalid point");
      }
   }

   if (base == ShapeType::MultiPatch)
   {
      const auto types_offset = reader.offset();
      shape._part_types.resize(part_count);
      for (std::size_t i = 0; i < part_count; ++i)
      {
         const int32_t part_type = reader.i32le();
         if (part_type < 0 || part_type > 5)
         {
            return fail(ErrorCode::Corrupt, types_offset + i * 4, "unknown multipatch part type " + std::to_string(part_type));
         }
         shape._part_types[i] = static_cast<PartType>(part_type);
      }
   }

   shape._points.resize(point_count);
   for (auto& point : shape._points)
   {
      point._x = reader.f64le();
      point._y = reader.f64le();
   }

   if (hasZ(type) && !readValues(reader, point_count, shape._z_range, shape._z))
   {
      return fail(ErrorCode::Corrupt, reader.offset(), "z values do not fit the record");
   }

   if (mayHaveM(type))
   {
      // optional for the Z types: present only if the record has room for them
      const bool mandatory = !hasZ(type);
      if (!readValues(reader, point_count, shape._m_range, shape._m))
      {
         shape._m.clear();
         shape._m_range = {};
         if (mandatory)
         {
            return fail(ErrorCode::Corrupt, reader.offset(), "measures do not fit the record");
         }
      }
   }
   return {};
}

inline std::expected<Shapefile, Error> parseShp(std::span<const std::byte> data)
{
   Shapefile file;
   Reader reader(data);
   if (!reader.has(header_size))
   {
      if (reader.has(4) && reader.i32be() == file_code)
      {
         return fail(ErrorCode::Truncated, data.size(), "file header");
      }
      return fail(ErrorCode::NotAShapefile, 0, "the file is shorter than a shapefile header");
   }
   if (reader.i32be() != file_code)
   {
      return fail(ErrorCode::NotAShapefile, 0, "file code is not 9994");
   }
   reader.skip(20);
   const int32_t length_words = reader.i32be();
   auto& header = file._header;
   header._version = reader.i32le();
   const int32_t raw_type = reader.i32le();
   header._bounds = readBox(reader);
   header._z_range = readRange(reader);
   header._m_range = readRange(reader);

   if (!isKnown(raw_type))
   {
      return fail(ErrorCode::UnsupportedShapeType, 32, "shape type " + std::to_string(raw_type));
   }
   header._type = static_cast<ShapeType>(raw_type);

   if (length_words < static_cast<int32_t>(header_size / 2))
   {
      return fail(ErrorCode::Corrupt, 24, "file length is shorter than the header");
   }
   const auto declared = static_cast<std::size_t>(length_words) * 2;
   header._file_length = static_cast<int32_t>(std::min<std::size_t>(declared, INT32_MAX));
   if (declared > data.size())
   {
      return fail(ErrorCode::Truncated, data.size(), "the header declares " + std::to_string(declared) + " bytes");
   }

   // trailing bytes after the declared length are ignored
   Reader records(data.first(declared));
   records.skip(header_size);
   while (records.remaining() > 0)
   {
      const auto record_offset = records.offset();
      if (!records.has(8))
      {
         return fail(ErrorCode::Truncated, record_offset, "record header");
      }
      Shape shape;
      shape._record_number = records.i32be();
      const int32_t content_words = records.i32be();
      if (content_words < 2)
      {
         return fail(ErrorCode::Corrupt, record_offset + 4, "record content length " + std::to_string(content_words));
      }
      const auto content_bytes = static_cast<std::size_t>(content_words) * 2;
      if (!records.has(content_bytes))
      {
         return fail(ErrorCode::Truncated, record_offset, "record " + std::to_string(shape._record_number));
      }
      Reader content(records.bytes(content_bytes), record_offset + 8);
      if (auto parsed = parseShape(content, header._type, shape); !parsed)
      {
         return std::unexpected(std::move(parsed.error()));
      }
      file._shapes.push_back(std::move(shape));
   }
   return file;
}

// --- dbf ---------------------------------------------------------------------------------------------------------------

inline void appendUtf8(std::string& text, uint32_t code_point)
{
   if (code_point < 0x80)
   {
      text += static_cast<char>(code_point);
   }
   else if (code_point < 0x800)
   {
      text += static_cast<char>(0xc0 | (code_point >> 6));
      text += static_cast<char>(0x80 | (code_point & 0x3f));
   }
   else
   {
      text += static_cast<char>(0xe0 | (code_point >> 12));
      text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
      text += static_cast<char>(0x80 | (code_point & 0x3f));
   }
}

/// \brief Windows-1252 to utf-8; the same as ISO-8859-1 except for 0x80 to 0x9f.
inline std::string windows1252ToUtf8(std::string_view bytes)
{
   static constexpr std::array<uint16_t, 32> high{0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                                  0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
                                                  0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                                  0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178};
   std::string text;
   text.reserve(bytes.size());
   for (const char c : bytes)
   {
      const auto byte = static_cast<uint8_t>(c);
      if (byte >= 0x80 && byte < 0xa0)
      {
         appendUtf8(text, high[byte - 0x80]);
      }
      else
      {
         appendUtf8(text, byte);
      }
   }
   return text;
}

/// \brief length of the valid utf-8 sequence at `at`, or 0 if it is not valid.
inline std::size_t utf8SequenceLength(std::string_view text, std::size_t at)
{
   const auto lead = static_cast<uint8_t>(text[at]);
   std::size_t length = 0;
   uint32_t code_point = 0;
   if (lead < 0x80)
   {
      return 1;
   }
   if ((lead & 0xe0) == 0xc0)
   {
      length = 2;
      code_point = lead & 0x1fu;
   }
   else if ((lead & 0xf0) == 0xe0)
   {
      length = 3;
      code_point = lead & 0x0fu;
   }
   else if ((lead & 0xf8) == 0xf0)
   {
      length = 4;
      code_point = lead & 0x07u;
   }
   else
   {
      return 0;
   }
   if (at + length > text.size())
   {
      return 0;
   }
   for (std::size_t i = 1; i < length; ++i)
   {
      const auto continuation = static_cast<uint8_t>(text[at + i]);
      if ((continuation & 0xc0) != 0x80)
      {
         return 0;
      }
      code_point = (code_point << 6) | (continuation & 0x3fu);
   }
   // no overlong forms, surrogates or code points past U+10FFFF
   static constexpr std::array<uint32_t, 5> minimum{0, 0, 0x80, 0x800, 0x10000};
   if (code_point < minimum[length] || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff))
   {
      return 0;
   }
   return length;
}

[[nodiscard]] inline bool isValidUtf8(std::string_view text)
{
   for (std::size_t at = 0; at < text.size();)
   {
      const auto length = utf8SequenceLength(text, at);
      if (length == 0)
      {
         return false;
      }
      at += length;
   }
   return true;
}

/// \brief replaces invalid sequences with U+FFFD.
inline std::string sanitizeUtf8(std::string_view text)
{
   std::string result;
   result.reserve(text.size());
   for (std::size_t at = 0; at < text.size();)
   {
      const auto length = utf8SequenceLength(text, at);
      if (length == 0)
      {
         appendUtf8(result, 0xfffd);
         ++at;
      }
      else
      {
         result.append(text.substr(at, length));
         at += length;
      }
   }
   return result;
}

inline std::string decodeText(std::string_view bytes, Encoding encoding)
{
   switch (encoding)
   {
      case Encoding::Utf8:
         return sanitizeUtf8(bytes);
      case Encoding::Windows1252:
         return windows1252ToUtf8(bytes);
      case Encoding::Auto:
         break;
   }
   return isValidUtf8(bytes) ? std::string{bytes} : windows1252ToUtf8(bytes);
}

/// \brief the encoding a .cpg file names, or Auto if it names nothing tinyshp knows.
inline Encoding encodingFromCpg(std::string_view cpg)
{
   std::string name;
   for (const char c : cpg)
   {
      if (std::isalnum(static_cast<unsigned char>(c)) != 0)
      {
         name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
   }
   if (name == "UTF8" || name == "65001")
   {
      return Encoding::Utf8;
   }
   if (name == "1252" || name == "ANSI1252" || name == "CP1252" || name == "WINDOWS1252" || name == "ISO88591" ||
       name == "88591" || name == "LATIN1")
   {
      return Encoding::Windows1252;
   }
   return Encoding::Auto;
}

inline std::string_view trim(std::string_view text)
{
   const auto is_space = [](char c) { return c == ' ' || c == '\0' || c == '\t' || c == '\r' || c == '\n'; };
   while (!text.empty() && is_space(text.front()))
   {
      text.remove_prefix(1);
   }
   while (!text.empty() && is_space(text.back()))
   {
      text.remove_suffix(1);
   }
   return text;
}

inline std::string_view trimRight(std::string_view text)
{
   while (!text.empty() && (text.back() == ' ' || text.back() == '\0'))
   {
      text.remove_suffix(1);
   }
   return text;
}

/// \brief a decimal number as dBase writes it, independent of the C locale. nothing for blanks and garbage.
inline std::optional<double> parseNumber(std::string_view text)
{
   text = trim(text);
   if (text.empty())
   {
      return std::nullopt;
   }
   if (text.front() == '+')
   {
      text.remove_prefix(1);
   }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
   double value = 0.0;
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
   if (error != std::errc{} || end != text.data() + text.size())
   {
      return std::nullopt;
   }
   return value;
#else
   // standard libraries without floating point from_chars: digits, an optional fraction and exponent
   std::size_t at = 0;
   bool negative = false;
   if (at < text.size() && text[at] == '-')
   {
      negative = true;
      ++at;
   }
   double mantissa = 0.0;
   int32_t exponent = 0;
   bool digits = false;
   for (; at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0; ++at)
   {
      mantissa = mantissa * 10.0 + (text[at] - '0');
      digits = true;
   }
   if (at < text.size() && text[at] == '.')
   {
      for (++at; at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0; ++at)
      {
         mantissa = mantissa * 10.0 + (text[at] - '0');
         --exponent;
         digits = true;
      }
   }
   if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
   {
      ++at;
      bool negative_exponent = false;
      if (at < text.size() && (text[at] == '-' || text[at] == '+'))
      {
         negative_exponent = text[at] == '-';
         ++at;
      }
      int32_t value = 0;
      bool exponent_digits = false;
      for (; at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0; ++at)
      {
         value = std::min(value * 10 + (text[at] - '0'), 10000);
         exponent_digits = true;
      }
      if (!exponent_digits)
      {
         return std::nullopt;
      }
      exponent += negative_exponent ? -value : value;
   }
   if (!digits || at != text.size())
   {
      return std::nullopt;
   }
   const double value = mantissa * std::pow(10.0, exponent);
   return negative ? -value : value;
#endif
}

inline Value parseValue(const Field& field, std::span<const std::byte> raw, Encoding encoding)
{
   const std::string_view text{reinterpret_cast<const char*>(raw.data()), raw.size()};
   switch (field._type)
   {
      case FieldType::Character:
         return decodeText(trimRight(text), encoding);
      case FieldType::Number:
      case FieldType::Float:
      {
         const auto number = parseNumber(text);
         if (!number)
         {
            return std::monostate{};
         }
         return *number;
      }
      case FieldType::Logical:
      {
         const char flag = text.empty() ? '?' : text.front();
         if (flag == 'T' || flag == 't' || flag == 'Y' || flag == 'y')
         {
            return true;
         }
         if (flag == 'F' || flag == 'f' || flag == 'N' || flag == 'n')
         {
            return false;
         }
         return std::monostate{};
      }
      case FieldType::Date:
      {
         const auto digits = trim(text);
         if (digits.size() != 8 || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; }))
         {
            return std::monostate{};
         }
         const auto number = [&](std::size_t at, std::size_t count)
         {
            int32_t value = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
               value = value * 10 + (digits[at + i] - '0');
            }
            return value;
         };
         const Date date{number(0, 4), number(4, 2), number(6, 2)};
         if (date._month < 1 || date._month > 12 || date._day < 1 || date._day > 31)
         {
            return std::monostate{};
         }
         return date;
      }
      case FieldType::Integer:
      {
         if (raw.size() != 4)
         {
            return std::monostate{};
         }
         Reader reader(raw);
         return static_cast<double>(reader.i32le());
      }
      case FieldType::Double:
      {
         if (raw.size() != 8)
         {
            return std::monostate{};
         }
         Reader reader(raw);
         return reader.f64le();
      }
      default:
         break;
   }
   return std::monostate{};
}

inline std::expected<Table, Error> parseDbf(std::span<const std::byte> data, const LoadOptions& options, std::string_view cpg)
{
   const auto fail_dbf = [](ErrorCode code, std::size_t offset, std::string detail)
   { return fail(code, offset, "dbf: " + std::move(detail)); };

   Reader reader(data);
   if (!reader.has(32))
   {
      return fail_dbf(ErrorCode::NotADbaseTable, 0, "shorter than a table header");
   }
   const uint8_t version_byte = reader.u8();
   if ((version_byte & 0x07) == 4)
   {
      return fail_dbf(ErrorCode::UnsupportedDbaseVersion, 0, "dBase 7 tables are not supported");
   }
   if ((version_byte & 0x07) != 3 && version_byte != 0x30 && version_byte != 0x31 && version_byte != 0x32)
   {
      return fail_dbf(ErrorCode::NotADbaseTable, 0, "version byte " + std::to_string(version_byte));
   }
   reader.skip(3);  // date of last update
   const uint32_t record_count = reader.u32le();
   const uint16_t header_length = reader.u16le();
   const uint16_t record_length = reader.u16le();
   reader.skip(17);
   const uint8_t language_driver = reader.u8();
   reader.skip(2);

   Encoding encoding = options._encoding;
   if (encoding == Encoding::Auto)
   {
      encoding = encodingFromCpg(cpg);
   }
   if (encoding == Encoding::Auto && (language_driver == 0x57 || language_driver == 0x03))
   {
      encoding = Encoding::Windows1252;
   }

   if (header_length < 33 || header_length > data.size())
   {
      return fail_dbf(header_length < 33 ? ErrorCode::Corrupt : ErrorCode::Truncated, 8, "header length " + std::to_string(header_length));
   }

   Table table;
   std::size_t sum = 1;  // the deletion flag
   while (reader.position() + 32 <= header_length)
   {
      const auto field_offset = reader.offset();
      const auto descriptor = reader.bytes(32);
      if (std::to_integer<uint8_t>(descriptor[0]) == 0x0d)
      {
         break;
      }
      Field field;
      const std::string_view name_bytes{reinterpret_cast<const char*>(descriptor.data()), 11};
      field._name = windows1252ToUtf8(trim(name_bytes.substr(0, name_bytes.find('\0'))));
      field._type = static_cast<FieldType>(static_cast<char>(descriptor[11]));
      field._length = std::to_integer<uint8_t>(descriptor[16]);
      field._decimals = std::to_integer<uint8_t>(descriptor[17]);
      if (field._length == 0)
      {
         return fail_dbf(ErrorCode::Corrupt, field_offset + 16, "field " + field._name + " has length 0");
      }
      sum += field._length;
      table._fields.push_back(std::move(field));
   }
   if (sum > record_length)
   {
      return fail_dbf(
         ErrorCode::Corrupt,
         10,
         "record length " + std::to_string(record_length) + " is shorter than its fields (" + std::to_string(sum) + ")"
      );
   }

   const std::size_t body = data.size() - header_length;
   if (record_length == 0 || body / record_length < record_count)
   {
      // a record count that the file cannot hold, checked before reserving anything
      const std::size_t complete = record_length == 0 ? 0 : body / record_length;
      return fail_dbf(
         ErrorCode::Truncated,
         header_length + complete * record_length,
         std::to_string(record_count) + " records declared, " + std::to_string(complete) + " present"
      );
   }

   table._records.reserve(record_count);
   Reader records(data.subspan(header_length), header_length);
   for (uint32_t r = 0; r < record_count; ++r)
   {
      Reader row(records.bytes(record_length), header_length + std::size_t{r} * record_length);
      Record record;
      record._deleted = std::to_integer<uint8_t>(row.bytes(1)[0]) == '*';
      if (record._deleted && options._skip_deleted)
      {
         continue;
      }
      record._values.reserve(table._fields.size());
      for (const auto& field : table._fields)
      {
         record._values.push_back(parseValue(field, row.bytes(field._length), encoding));
      }
      table._records.push_back(std::move(record));
   }
   return table;
}

inline std::expected<std::vector<std::byte>, Error> readFile(const std::filesystem::path& path)
{
   std::error_code error;
   if (!std::filesystem::is_regular_file(path, error))
   {
      return fail(ErrorCode::FileNotFound, 0, path.string());
   }
   std::ifstream stream(path, std::ios::binary);
   if (!stream)
   {
      return fail(ErrorCode::ReadFailed, 0, path.string());
   }
   stream.seekg(0, std::ios::end);
   const auto size = stream.tellg();
   if (size < 0)
   {
      return fail(ErrorCode::ReadFailed, 0, path.string());
   }
   stream.seekg(0, std::ios::beg);
   std::vector<std::byte> bytes(static_cast<std::size_t>(size));
   if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
   {
      return fail(ErrorCode::ReadFailed, 0, path.string());
   }
   return bytes;
}

/// \brief the file next to `shp` with the given extension in lower or upper case, if it exists.
inline std::optional<std::filesystem::path> sibling(const std::filesystem::path& shp, std::string_view extension)
{
   std::string upper{extension};
   std::ranges::transform(upper, upper.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
   for (const auto& candidate : {std::string{extension}, upper})
   {
      auto path = shp;
      path.replace_extension(candidate);
      std::error_code error;
      if (std::filesystem::is_regular_file(path, error))
      {
         return path;
      }
   }
   return std::nullopt;
}

}  // namespace detail

inline std::expected<Shapefile, Error> load(std::span<const std::byte> shp, const LoadOptions& options)
{
   return load(Sources{shp, {}, {}, {}}, options);
}

inline std::expected<Shapefile, Error> load(const Sources& sources, const LoadOptions& options)
{
   auto file = detail::parseShp(sources._shp);
   if (!file)
   {
      return file;
   }
   file->_projection = std::string{sources._prj};
   if (!sources._dbf.empty())
   {
      auto table = detail::parseDbf(sources._dbf, options, sources._cpg);
      if (!table)
      {
         return std::unexpected(std::move(table.error()));
      }
      if (!options._skip_deleted && table->_records.size() != file->_shapes.size())
      {
         return detail::fail(
            ErrorCode::RecordCountMismatch,
            0,
            std::to_string(table->_records.size()) + " in the .dbf, " + std::to_string(file->_shapes.size()) +
               " in the .shp"
         );
      }
      file->_attributes = std::move(*table);
   }
   return file;
}

inline std::expected<Table, Error> loadTable(std::span<const std::byte> dbf, const LoadOptions& options, std::string_view cpg)
{
   return detail::parseDbf(dbf, options, cpg);
}

inline std::expected<Shapefile, Error> loadFile(const std::filesystem::path& path, const LoadOptions& options)
{
   const auto shp = detail::readFile(path);
   if (!shp)
   {
      return std::unexpected(shp.error());
   }

   const auto text = [](const std::vector<std::byte>& bytes)
   { return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()}; };

   std::vector<std::byte> dbf;
   std::string prj;
   std::string cpg;
   if (options._load_attributes)
   {
      if (const auto dbf_path = detail::sibling(path, ".dbf"))
      {
         auto bytes = detail::readFile(*dbf_path);
         if (!bytes)
         {
            return std::unexpected(bytes.error());
         }
         dbf = std::move(*bytes);
      }
      if (const auto cpg_path = detail::sibling(path, ".cpg"))
      {
         if (const auto bytes = detail::readFile(*cpg_path))
         {
            cpg = text(*bytes);
         }
      }
   }
   if (const auto prj_path = detail::sibling(path, ".prj"))
   {
      if (const auto bytes = detail::readFile(*prj_path))
      {
         prj = text(*bytes);
      }
   }
   return load(Sources{*shp, dbf, prj, cpg}, options);
}

}  // namespace tinyshp
