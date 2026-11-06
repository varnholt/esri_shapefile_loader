// writes .shp and .dbf files in memory, following the specification, so the tests can build every shape type and field
// type and then break the files on purpose.

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <tinyshp/tinyshp.hpp>

namespace writer
{

class Bytes
{
public:
   std::vector<std::byte> _data;

   void u8(uint8_t value)
   {
      _data.push_back(static_cast<std::byte>(value));
   }

   void le16(uint16_t value)
   {
      u8(static_cast<uint8_t>(value));
      u8(static_cast<uint8_t>(value >> 8));
   }

   void le32(int32_t value)
   {
      const auto bits = std::bit_cast<uint32_t>(value);
      for (auto shift = 0; shift < 32; shift += 8)
      {
         u8(static_cast<uint8_t>(bits >> shift));
      }
   }

   void be32(int32_t value)
   {
      const auto bits = std::bit_cast<uint32_t>(value);
      for (auto shift = 24; shift >= 0; shift -= 8)
      {
         u8(static_cast<uint8_t>(bits >> shift));
      }
   }

   void f64(double value)
   {
      const auto bits = std::bit_cast<uint64_t>(value);
      for (auto shift = 0; shift < 64; shift += 8)
      {
         u8(static_cast<uint8_t>(bits >> shift));
      }
   }

   void text(std::string_view value, std::size_t length, char pad = ' ')
   {
      for (std::size_t i = 0; i < length; ++i)
      {
         u8(static_cast<uint8_t>(i < value.size() ? value[i] : pad));
      }
   }

   void patchBe32(std::size_t at, int32_t value)
   {
      const auto bits = std::bit_cast<uint32_t>(value);
      for (std::size_t i = 0; i < 4; ++i)
      {
         _data[at + i] = static_cast<std::byte>(bits >> (24 - 8 * i));
      }
   }
};

inline tinyshp::Box boundsOf(const std::vector<tinyshp::Point>& points)
{
   tinyshp::Box box{
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::lowest()
   };
   for (const auto& point : points)
   {
      box._x_min = std::min(box._x_min, point._x);
      box._y_min = std::min(box._y_min, point._y);
      box._x_max = std::max(box._x_max, point._x);
      box._y_max = std::max(box._y_max, point._y);
   }
   return points.empty() ? tinyshp::Box{} : box;
}

inline tinyshp::Range rangeOf(const std::vector<double>& values)
{
   if (values.empty())
   {
      return {};
   }
   const auto [low, high] = std::ranges::minmax(values);
   return {low, high};
}

inline void box(Bytes& out, const tinyshp::Box& value)
{
   out.f64(value._x_min);
   out.f64(value._y_min);
   out.f64(value._x_max);
   out.f64(value._y_max);
}

inline void values(Bytes& out, const std::vector<double>& list)
{
   const auto range = rangeOf(list);
   out.f64(range._min);
   out.f64(range._max);
   for (const auto value : list)
   {
      out.f64(value);
   }
}

/// \brief the record content of a shape, starting with its type. _z and _m are written if present, so a Z shape
/// without _m is written without the optional measures.
inline Bytes content(const tinyshp::Shape& shape)
{
   Bytes out;
   out.le32(static_cast<int32_t>(shape._type));
   if (shape._type == tinyshp::ShapeType::Null)
   {
      return out;
   }
   const auto base = tinyshp::baseType(shape._type);
   if (base == tinyshp::ShapeType::Point)
   {
      out.f64(shape._points[0]._x);
      out.f64(shape._points[0]._y);
      if (!shape._z.empty())
      {
         out.f64(shape._z[0]);
      }
      if (!shape._m.empty())
      {
         out.f64(shape._m[0]);
      }
      return out;
   }

   box(out, boundsOf(shape._points));
   if (base != tinyshp::ShapeType::MultiPoint)
   {
      out.le32(static_cast<int32_t>(shape._parts.size()));
   }
   out.le32(static_cast<int32_t>(shape._points.size()));
   for (const auto part : shape._parts)
   {
      out.le32(static_cast<int32_t>(part));
   }
   for (const auto type : shape._part_types)
   {
      out.le32(static_cast<int32_t>(type));
   }
   for (const auto& point : shape._points)
   {
      out.f64(point._x);
      out.f64(point._y);
   }
   if (!shape._z.empty())
   {
      values(out, shape._z);
   }
   if (!shape._m.empty())
   {
      values(out, shape._m);
   }
   return out;
}

/// \brief a complete .shp file. `contents` replaces the generated record content where it is not empty, for broken
/// records.
inline std::vector<std::byte> shp(tinyshp::ShapeType type, const std::vector<tinyshp::Shape>& shapes)
{
   Bytes records;
   std::vector<tinyshp::Point> all;
   int32_t number = 1;
   for (const auto& shape : shapes)
   {
      const auto record = content(shape);
      records.be32(number++);
      records.be32(static_cast<int32_t>(record._data.size() / 2));
      records._data.insert(records._data.end(), record._data.begin(), record._data.end());
      all.insert(all.end(), shape._points.begin(), shape._points.end());
   }

   Bytes out;
   out.be32(9994);
   for (auto i = 0; i < 5; ++i)
   {
      out.be32(0);
   }
   out.be32(static_cast<int32_t>((100 + records._data.size()) / 2));
   out.le32(1000);
   out.le32(static_cast<int32_t>(type));
   box(out, boundsOf(all));
   for (auto i = 0; i < 4; ++i)
   {
      out.f64(0.0);
   }
   out._data.insert(out._data.end(), records._data.begin(), records._data.end());
   return out._data;
}

struct FieldSpec
{
   std::string _name;
   char _type = 'C';
   uint8_t _length = 10;
   uint8_t _decimals = 0;
};

/// \brief a dBase III table. each cell is written as given, padded with spaces to the field length; binary fields
/// (I, O) take their raw bytes in the string.
inline std::vector<std::byte> dbf(
   const std::vector<FieldSpec>& fields,
   const std::vector<std::vector<std::string>>& rows,
   const std::vector<bool>& deleted = {},
   uint8_t language_driver = 0
)
{
   Bytes out;
   uint16_t record_length = 1;
   for (const auto& field : fields)
   {
      record_length = static_cast<uint16_t>(record_length + field._length);
   }
   const auto header_length = static_cast<uint16_t>(32 + 32 * fields.size() + 1);

   out.u8(0x03);
   out.u8(126);  // 2026-11-06
   out.u8(11);
   out.u8(6);
   out.le32(static_cast<int32_t>(rows.size()));
   out.le16(header_length);
   out.le16(record_length);
   for (auto i = 0; i < 17; ++i)
   {
      out.u8(0);
   }
   out.u8(language_driver);
   out.u8(0);
   out.u8(0);

   for (const auto& field : fields)
   {
      out.text(field._name, 11, '\0');
      out.u8(static_cast<uint8_t>(field._type));
      out.le32(0);
      out.u8(field._length);
      out.u8(field._decimals);
      for (auto i = 0; i < 14; ++i)
      {
         out.u8(0);
      }
   }
   out.u8(0x0d);

   for (std::size_t r = 0; r < rows.size(); ++r)
   {
      out.u8(r < deleted.size() && deleted[r] ? '*' : ' ');
      for (std::size_t f = 0; f < fields.size(); ++f)
      {
         const bool numeric = fields[f]._type == 'N' || fields[f]._type == 'F';
         auto cell = rows[r][f];
         if (numeric && cell.size() < fields[f]._length)
         {
            cell.insert(0, fields[f]._length - cell.size(), ' ');  // numbers are right aligned
         }
         out.text(cell, fields[f]._length, fields[f]._type == 'I' || fields[f]._type == 'O' ? '\0' : ' ');
      }
   }
   out.u8(0x1a);
   return out._data;
}

}  // namespace writer
