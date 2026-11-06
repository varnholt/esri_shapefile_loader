// tests against files built by tests/shp_writer.h, plus robustness against truncated and corrupted input

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <print>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "shp_writer.h"
#include "test_framework.h"
#include "tinyshp/tinyshp.hpp"

namespace
{

using tinyshp::PartType;
using tinyshp::Point;
using tinyshp::Shape;
using tinyshp::ShapeType;

/// \brief a square ring, clockwise (an outer ring) unless counterclockwise is asked for, closed like the
/// specification wants.
std::vector<Point> square(double x, double y, double size, bool counterclockwise = false)
{
   std::vector<Point> ring{{x, y}, {x, y + size}, {x + size, y + size}, {x + size, y}, {x, y}};
   if (counterclockwise)
   {
      std::ranges::reverse(ring);
   }
   return ring;
}

Shape makeShape(ShapeType type, const std::vector<std::vector<Point>>& parts, bool z, bool m)
{
   Shape shape;
   shape._type = type;
   for (const auto& part : parts)
   {
      if (tinyshp::baseType(type) != ShapeType::MultiPoint && tinyshp::baseType(type) != ShapeType::Point)
      {
         shape._parts.push_back(static_cast<uint32_t>(shape._points.size()));
      }
      shape._points.insert(shape._points.end(), part.begin(), part.end());
   }
   for (std::size_t i = 0; i < shape._points.size(); ++i)
   {
      if (z)
      {
         shape._z.push_back(100.0 + static_cast<double>(i));
      }
      if (m)
      {
         shape._m.push_back(static_cast<double>(i) * 0.5);
      }
   }
   return shape;
}

/// \brief one record of every shape type, with and without the optional measures.
std::vector<std::pair<ShapeType, Shape>> everyType()
{
   const auto ring = square(0, 0, 10);
   const std::vector<Point> line{{0, 0}, {5, 5}, {10, 0}};
   const std::vector<Point> single{{3.5, -7.25}};
   const std::vector<Point> cloud{{1, 2}, {3, 4}, {-5, 6}};

   std::vector<std::pair<ShapeType, Shape>> shapes;
   const auto add = [&](ShapeType type, const std::vector<std::vector<Point>>& parts, bool z, bool m)
   { shapes.emplace_back(type, makeShape(type, parts, z, m)); };

   add(ShapeType::Point, {single}, false, false);
   add(ShapeType::PointM, {single}, false, true);
   add(ShapeType::PointZ, {single}, true, true);
   add(ShapeType::PointZ, {single}, true, false);
   add(ShapeType::MultiPoint, {cloud}, false, false);
   add(ShapeType::MultiPointM, {cloud}, false, true);
   add(ShapeType::MultiPointZ, {cloud}, true, true);
   add(ShapeType::MultiPointZ, {cloud}, true, false);
   add(ShapeType::PolyLine, {line, line}, false, false);
   add(ShapeType::PolyLineM, {line}, false, true);
   add(ShapeType::PolyLineZ, {line, line}, true, true);
   add(ShapeType::PolyLineZ, {line}, true, false);
   add(ShapeType::Polygon, {ring, square(2, 2, 3, true)}, false, false);
   add(ShapeType::PolygonM, {ring}, false, true);
   add(ShapeType::PolygonZ, {ring}, true, true);
   add(ShapeType::PolygonZ, {ring}, true, false);

   // a fan, a strip and a ring with a hole
   auto patch = makeShape(
      ShapeType::MultiPatch,
      {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, ring, square(2, 2, 3, true)},
      true,
      true
   );
   patch._part_types = {PartType::TriangleFan, PartType::TriangleStrip, PartType::OuterRing, PartType::InnerRing};
   shapes.emplace_back(ShapeType::MultiPatch, patch);
   return shapes;
}

/// \brief a polygon file with null records, holes and several shapes; the input for the robustness tests.
std::vector<std::byte> sampleFile()
{
   std::vector<Shape> shapes;
   shapes.push_back(makeShape(ShapeType::PolygonZ, {square(0, 0, 10), square(2, 2, 3, true)}, true, true));
   shapes.push_back(Shape{});
   shapes.push_back(makeShape(ShapeType::PolygonZ, {square(20, 0, 5), square(30, 0, 5)}, true, false));
   return writer::shp(ShapeType::PolygonZ, shapes);
}

std::vector<std::byte> sampleTable()
{
   return writer::dbf(
      {{"NAME", 'C', 12}, {"POP", 'N', 10}, {"AREA", 'F', 12, 3}, {"OK", 'L', 1}, {"SINCE", 'D', 8}},
      {{"Alpha", "1200", "3.250", "T", "20240229"}, {"", "", "", "?", ""}, {"Gamma", "-7", "1e3", "f", "19991231"}}
   );
}

std::filesystem::path tempPath(const std::string& name)
{
   return std::filesystem::temp_directory_path() / std::format("tinyshp_test_{}_{}", std::random_device{}(), name);
}

void writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
   std::ofstream out(path, std::ios::binary);
   out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("every shape type reads back what was written")
{
   for (const auto& [type, written] : everyType())
   {
      const auto bytes = writer::shp(type, {written});
      const auto file = tinyshp::load(bytes);
      CHECK_MESSAGE(file.has_value(), "{}: {}", tinyshp::describe(type), file ? "" : tinyshp::describe(file.error()));
      if (!file)
      {
         continue;
      }
      CHECK(file->_header._type == type);
      CHECK(file->_header._version == 1000);
      CHECK(file->_header._file_length == static_cast<int32_t>(bytes.size()));
      CHECK(file->_shapes.size() == 1);
      const auto& shape = file->_shapes[0];
      CHECK(shape._record_number == 1);
      CHECK(shape._type == type);
      CHECK_MESSAGE(shape._points == written._points, "{}", tinyshp::describe(type));
      CHECK_MESSAGE(shape._parts == written._parts, "{}", tinyshp::describe(type));
      CHECK(shape._part_types == written._part_types);
      CHECK_MESSAGE(shape._z == written._z, "{} z", tinyshp::describe(type));
      CHECK_MESSAGE(shape._m == written._m, "{} m", tinyshp::describe(type));
      if (!written._z.empty())
      {
         CHECK(shape._z_range == writer::rangeOf(written._z));
      }
      if (!written._m.empty())
      {
         CHECK(shape._m_range == writer::rangeOf(written._m));
      }
      CHECK(shape._bounds == writer::boundsOf(written._points));
      CHECK(file->_header._bounds == writer::boundsOf(written._points));
   }
}

TEST_CASE("parts, null records and record numbers")
{
   const auto file = tinyshp::load(sampleFile());
   CHECK(file.has_value());
   if (!file)
   {
      return;
   }
   CHECK(file->_shapes.size() == 3);
   CHECK(file->_shapes[1].isNull());
   CHECK(file->_shapes[1]._points.empty());
   CHECK(file->_shapes[2]._record_number == 3);

   const auto& shape = file->_shapes[0];
   CHECK(shape.partCount() == 2);
   CHECK(shape.part(0).size() == 5);
   CHECK(shape.part(1).front() == (Point{2, 2}));
   CHECK((shape.partRange(1) == std::pair<std::size_t, std::size_t>(5, 10)));
   CHECK(file->_shapes[2]._m.empty());  // PolygonZ without the optional measures

   // points and multipoints are one part
   const auto multipoint = makeShape(ShapeType::MultiPoint, {{{1, 1}, {2, 2}}}, false, false);
   CHECK(multipoint.partCount() == 1);
   CHECK(multipoint.part(0).size() == 2);
   CHECK(Shape{}.partCount() == 0);
}

TEST_CASE("measures below -1e38 are no data")
{
   CHECK(tinyshp::isNoData(-1e39));
   CHECK(tinyshp::isNoData(-std::numeric_limits<double>::max()));
   CHECK(!tinyshp::isNoData(-1e37));
   CHECK(!tinyshp::isNoData(0.0));
}

TEST_CASE("ring orientation, area and containment")
{
   const auto outer = square(0, 0, 10);
   const auto hole = square(2, 2, 3, true);
   CHECK(tinyshp::isClockwise(outer));
   CHECK(!tinyshp::isClockwise(hole));
   CHECK(std::abs(tinyshp::signedArea(outer) + 100.0) < 1e-9);
   CHECK(std::abs(tinyshp::signedArea(hole) - 9.0) < 1e-9);
   CHECK(tinyshp::signedArea(std::vector<Point>{{0, 0}, {1, 1}}) == 0.0);

   // far from the origin, like projected coordinates, the area stays exact
   const auto far = square(4'500'000.0, 7'200'000.0, 0.5);
   CHECK(std::abs(tinyshp::signedArea(far) + 0.25) < 1e-9);

   CHECK(tinyshp::contains(outer, {5, 5}));
   CHECK(!tinyshp::contains(outer, {15, 5}));
   CHECK(!tinyshp::contains(std::span<const Point>{}, {0, 0}));

   const auto shape = makeShape(ShapeType::Polygon, {outer, hole}, false, false);
   auto bounded = shape;
   bounded._bounds = writer::boundsOf(shape._points);
   CHECK(tinyshp::contains(bounded, {1, 1}));
   CHECK(!tinyshp::contains(bounded, {3, 3}));  // in the hole
   CHECK(!tinyshp::contains(bounded, {11, 3}));

   auto line = makeShape(ShapeType::PolyLine, {outer}, false, false);
   line._bounds = bounded._bounds;
   CHECK(!tinyshp::contains(line, {1, 1}));  // lines cover nothing
}

TEST_CASE("polygons() groups holes with the smallest outer ring around them")
{
   // a big square with an island inside a lake inside it, a second separate square, and a stray hole
   auto shape = makeShape(
      ShapeType::Polygon,
      {square(0, 0, 100), square(10, 10, 50, true), square(20, 20, 10), square(200, 0, 10), square(500, 500, 5, true)},
      false,
      false
   );
   const auto groups = tinyshp::polygons(shape);
   CHECK(groups.size() == 4);
   if (groups.size() != 4)
   {
      return;
   }
   CHECK(groups[0]._outer.size() == 5 && groups[0]._outer.front() == (Point{0, 0}));
   CHECK(groups[0]._holes.size() == 1);
   CHECK(groups[1]._holes.empty());  // the island
   CHECK(groups[2]._holes.empty());
   CHECK(groups[3]._outer.front() == (Point{500, 500}));  // the stray hole, kept as an outer ring

   // multipatch: part types decide, triangles are skipped
   auto patch = makeShape(
      ShapeType::MultiPatch,
      {{{0, 0}, {1, 0}, {0, 1}}, square(0, 0, 10), square(2, 2, 3, true), square(20, 0, 5)},
      true,
      false
   );
   patch._part_types = {PartType::TriangleFan, PartType::OuterRing, PartType::InnerRing, PartType::FirstRing};
   const auto patches = tinyshp::polygons(patch);
   CHECK(patches.size() == 2);
   CHECK(!patches.empty() && patches[0]._holes.size() == 1);

   CHECK(tinyshp::polygons(makeShape(ShapeType::PolyLine, {square(0, 0, 1)}, false, false)).empty());
}

TEST_CASE("type helpers")
{
   CHECK(tinyshp::baseType(ShapeType::PolygonZ) == ShapeType::Polygon);
   CHECK(tinyshp::baseType(ShapeType::MultiPointM) == ShapeType::MultiPoint);
   CHECK(tinyshp::baseType(ShapeType::MultiPatch) == ShapeType::MultiPatch);
   CHECK(tinyshp::hasZ(ShapeType::MultiPatch));
   CHECK(!tinyshp::hasZ(ShapeType::PolyLineM));
   CHECK(tinyshp::mayHaveM(ShapeType::PointZ));
   CHECK(!tinyshp::mayHaveM(ShapeType::Polygon));
   CHECK(tinyshp::describe(ShapeType::PolyLineZ) == "PolyLineZ");
   CHECK(!tinyshp::isKnown(2));
}

TEST_CASE("invalid headers are reported")
{
   const auto not_shp = std::vector<std::byte>(200, std::byte{'G'});
   CHECK(tinyshp::load(not_shp).error()._code == tinyshp::ErrorCode::NotAShapefile);
   CHECK(tinyshp::load(std::span<const std::byte>{}).error()._code == tinyshp::ErrorCode::NotAShapefile);

   auto file = sampleFile();
   CHECK(tinyshp::load(std::span{file}.first(60)).error()._code == tinyshp::ErrorCode::Truncated);

   auto unknown = file;
   unknown[32] = std::byte{2};
   CHECK(tinyshp::load(unknown).error()._code == tinyshp::ErrorCode::UnsupportedShapeType);

   // the declared length is longer than the data
   auto longer = file;
   longer.pop_back();
   const auto error = tinyshp::load(longer).error();
   CHECK(error._code == tinyshp::ErrorCode::Truncated);
   CHECK(tinyshp::describe(error).starts_with("file is truncated at byte "));

   // bytes after the declared length are ignored
   auto padded = file;
   padded.resize(file.size() + 7);
   CHECK(tinyshp::load(padded).has_value());
}

TEST_CASE("broken records are reported, not misread")
{
   const auto expectError = [](const std::vector<std::byte>& bytes, tinyshp::ErrorCode code, std::string_view what)
   {
      const auto file = tinyshp::load(bytes);
      CHECK_MESSAGE(!file && file.error()._code == code, "{}: {}", what, file ? "loaded" : tinyshp::describe(file.error()));
   };

   const auto polygon = makeShape(ShapeType::Polygon, {square(0, 0, 1)}, false, false);

   // a record of another type
   expectError(writer::shp(ShapeType::PolyLine, {polygon}), tinyshp::ErrorCode::Corrupt, "mixed types");

   // part start past the points, descending parts, first part not at 0
   for (const auto& parts : std::vector<std::vector<uint32_t>>{{0, 9}, {0, 3, 2}, {1}})
   {
      auto broken = polygon;
      broken._parts = parts;
      expectError(writer::shp(ShapeType::Polygon, {broken}), tinyshp::ErrorCode::Corrupt, "parts");
   }

   // a point count far beyond the record must fail before anything is allocated
   auto bytes = writer::shp(ShapeType::Polygon, {polygon});
   const std::size_t point_count_at = 100 + 8 + 4 + 32 + 4;
   for (const int32_t count : {0x7fffffff, -1, 6})
   {
      auto huge = bytes;
      const auto bits = std::bit_cast<uint32_t>(count);
      for (std::size_t i = 0; i < 4; ++i)
      {
         huge[point_count_at + i] = static_cast<std::byte>(bits >> (8 * i));
      }
      expectError(huge, tinyshp::ErrorCode::Corrupt, std::format("point count {}", count));
   }

   // a record content length past the end of the file
   auto too_long = bytes;
   writer::Bytes patch{too_long};
   patch.patchBe32(104, 100000);
   expectError(patch._data, tinyshp::ErrorCode::Truncated, "content length");

   // a multipatch part type that does not exist
   auto patch_shape = makeShape(ShapeType::MultiPatch, {square(0, 0, 1)}, true, false);
   patch_shape._part_types = {static_cast<PartType>(9)};
   expectError(writer::shp(ShapeType::MultiPatch, {patch_shape}), tinyshp::ErrorCode::Corrupt, "part type");

   // PointM without its measure
   auto point = makeShape(ShapeType::PointM, {{{1, 2}}}, false, false);
   expectError(writer::shp(ShapeType::PointM, {point}), tinyshp::ErrorCode::Corrupt, "point measure");
}

TEST_CASE("dbf: every field type, nulls and formatting")
{
   const auto table = tinyshp::loadTable(sampleTable());
   CHECK(table.has_value());
   if (!table)
   {
      return;
   }
   CHECK(table->_fields.size() == 5);
   CHECK(table->_fields[2]._type == tinyshp::FieldType::Float);
   CHECK(table->_fields[2]._decimals == 3);
   CHECK(table->_records.size() == 3);

   CHECK(table->text(0, "NAME") == "Alpha");
   CHECK(table->number(0, "pop") == 1200.0);  // field names ignore case
   CHECK(table->number(0, "AREA") == 3.25);
   CHECK(table->text(0, "OK") == "true");
   CHECK(std::get<tinyshp::Date>(*table->find(0, "SINCE")) == (tinyshp::Date{2024, 2, 29}));
   CHECK(table->text(0, "SINCE") == "2024-02-29");

   // blanks: empty text, null everything else
   CHECK(std::holds_alternative<std::string>(*table->find(1, "NAME")));
   CHECK(std::holds_alternative<std::monostate>(*table->find(1, "POP")));
   CHECK(std::holds_alternative<std::monostate>(*table->find(1, "OK")));
   CHECK(std::holds_alternative<std::monostate>(*table->find(1, "SINCE")));
   CHECK(!table->number(1, "POP"));

   CHECK(table->number(2, "POP") == -7.0);
   CHECK(table->number(2, "AREA") == 1000.0);
   CHECK(table->text(2, "OK") == "false");

   CHECK(table->find(0, "MISSING") == nullptr);
   CHECK(table->find(9, "NAME") == nullptr);
   CHECK(!table->number(0, "NAME"));
   CHECK(tinyshp::toString(tinyshp::Value{2.5}) == "2.5");
   CHECK(tinyshp::toString(tinyshp::Value{1e20}) == "1e+20");
}

TEST_CASE("dbf: binary integer and double fields")
{
   std::string integer(4, '\0');
   const auto bits = std::bit_cast<uint32_t>(int32_t{-123456});
   for (std::size_t i = 0; i < 4; ++i)
   {
      integer[i] = static_cast<char>(bits >> (8 * i));
   }
   std::string floating(8, '\0');
   const auto double_bits = std::bit_cast<uint64_t>(6.02e23);
   for (std::size_t i = 0; i < 8; ++i)
   {
      floating[i] = static_cast<char>(double_bits >> (8 * i));
   }
   const auto table = tinyshp::loadTable(writer::dbf({{"I", 'I', 4}, {"O", 'O', 8}, {"MEMO", 'M', 10}}, {{integer, floating, "12"}}));
   CHECK(table.has_value());
   if (table)
   {
      CHECK(table->number(0, "I") == -123456.0);
      CHECK(table->number(0, "O") == 6.02e23);
      CHECK(std::holds_alternative<std::monostate>(*table->find(0, "MEMO")));
   }
}

TEST_CASE("dbf: text encodings")
{
   const std::string utf8 = "M\xc3\xbcnchen";     // München in utf-8
   const std::string latin1 = "M\xfcnchen";       // München in Windows-1252
   const std::string euro = "\x80 5";             // € 5 in Windows-1252
   const auto bytes = writer::dbf({{"NAME", 'C', 12}}, {{utf8}, {latin1}, {euro}});

   const auto automatic = tinyshp::loadTable(bytes);
   CHECK(automatic && automatic->text(0, "NAME") == utf8);
   CHECK(automatic && automatic->text(1, "NAME") == utf8);  // not valid utf-8, so read as Windows-1252
   CHECK(automatic && automatic->text(2, "NAME") == "\xe2\x82\xac 5");

   // a .cpg or the option decides for the whole table
   const auto ansi = tinyshp::loadTable(bytes, {}, "ANSI 1252\r\n");
   CHECK(ansi && ansi->text(0, "NAME") == "M\xc3\x83\xc2\xbcnchen");  // utf-8 bytes read as Windows-1252

   const auto forced = tinyshp::loadTable(bytes, {._encoding = tinyshp::Encoding::Utf8});
   CHECK(forced && forced->text(1, "NAME") == "M\xef\xbf\xbdnchen");  // replacement character

   // the language driver id 0x57 means ANSI
   const auto driver = tinyshp::loadTable(writer::dbf({{"NAME", 'C', 12}}, {{latin1}}, {}, 0x57));
   CHECK(driver && driver->text(0, "NAME") == utf8);
}

TEST_CASE("dbf: deleted records")
{
   const auto bytes = writer::dbf({{"ID", 'N', 4}}, {{"1"}, {"2"}, {"3"}}, {false, true, false});
   const auto kept = tinyshp::loadTable(bytes);
   CHECK(kept && kept->_records.size() == 3 && kept->_records[1]._deleted);
   const auto skipped = tinyshp::loadTable(bytes, {._skip_deleted = true});
   CHECK(skipped && skipped->_records.size() == 2 && skipped->number(1, "ID") == 3.0);
}

TEST_CASE("dbf: broken tables are reported")
{
   const auto bytes = sampleTable();
   CHECK(tinyshp::loadTable(std::span{bytes}.first(10)).error()._code == tinyshp::ErrorCode::NotADbaseTable);

   auto dbase7 = bytes;
   dbase7[0] = std::byte{0x04};
   CHECK(tinyshp::loadTable(dbase7).error()._code == tinyshp::ErrorCode::UnsupportedDbaseVersion);

   auto garbage = bytes;
   garbage[0] = std::byte{0x47};
   CHECK(tinyshp::loadTable(garbage).error()._code == tinyshp::ErrorCode::NotADbaseTable);

   // records cut off
   const auto cut = tinyshp::loadTable(std::span{bytes}.first(bytes.size() - 20));
   CHECK(!cut && cut.error()._code == tinyshp::ErrorCode::Truncated);
   CHECK(!cut && cut.error()._detail.starts_with("dbf: "));

   // a record count far beyond the file
   auto huge = bytes;
   huge[7] = std::byte{0x7f};
   CHECK(tinyshp::loadTable(huge).error()._code == tinyshp::ErrorCode::Truncated);

   // a record length shorter than the fields
   auto short_record = bytes;
   short_record[10] = std::byte{5};
   short_record[11] = std::byte{0};
   CHECK(tinyshp::loadTable(short_record).error()._code == tinyshp::ErrorCode::Corrupt);
}

TEST_CASE("load() with sources joins geometry, attributes and projection")
{
   const auto shp = sampleFile();
   const auto dbf = sampleTable();
   const std::string prj = R"(GEOGCS["GCS_WGS_1984",DATUM["D_WGS_1984",SPHEROID["WGS_1984",6378137.0,298.257223563]]])";
   const auto file = tinyshp::load(tinyshp::Sources{shp, dbf, prj, {}});
   CHECK(file.has_value());
   if (file)
   {
      CHECK(file->_attributes.has_value());
      CHECK(file->_attributes->text(2, "NAME") == "Gamma");
      CHECK(file->isGeographic());
   }

   const auto projected = tinyshp::load(tinyshp::Sources{shp, {}, R"(PROJCS["NAD_1983_StatePlane"])", {}});
   CHECK(projected && !projected->isGeographic() && !projected->_attributes);

   // two records in the table, three shapes
   const auto fewer = writer::dbf({{"ID", 'N', 4}}, {{"1"}, {"2"}});
   const auto mismatch = tinyshp::load(tinyshp::Sources{shp, fewer, {}, {}});
   CHECK(!mismatch && mismatch.error()._code == tinyshp::ErrorCode::RecordCountMismatch);
   CHECK(!mismatch && tinyshp::describe(mismatch.error()).find("2 in the .dbf, 3 in the .shp") != std::string::npos);
}

TEST_CASE("loadFile finds the .dbf, .prj and .cpg next to the .shp in either case")
{
   const auto base = tempPath("places");
   auto shp_path = base;
   shp_path.replace_extension(".shp");
   auto dbf_path = base;
   dbf_path.replace_extension(".DBF");
   auto prj_path = base;
   prj_path.replace_extension(".prj");

   writeFile(shp_path, sampleFile());
   writeFile(dbf_path, sampleTable());
   const std::string prj = R"(GEOGCS["WGS 84"])";
   writeFile(prj_path, std::as_bytes(std::span{prj}));

   const auto file = tinyshp::loadFile(shp_path);
   CHECK_MESSAGE(file.has_value(), "{}", file ? "" : tinyshp::describe(file.error()));
   if (file)
   {
      CHECK(file->_shapes.size() == 3);
      CHECK(file->_attributes && file->_attributes->text(0, "NAME") == "Alpha");
      CHECK(file->_projection == prj);
   }

   const auto geometry_only = tinyshp::loadFile(shp_path, {._load_attributes = false});
   CHECK(geometry_only && !geometry_only->_attributes);

   std::filesystem::remove(shp_path);
   std::filesystem::remove(dbf_path);
   std::filesystem::remove(prj_path);

   const auto missing = tinyshp::loadFile(shp_path);
   CHECK(!missing && missing.error()._code == tinyshp::ErrorCode::FileNotFound);
   CHECK(!missing && tinyshp::describe(missing.error()).starts_with("file not found: "));
}

TEST_CASE("every truncation is an error, never a crash")
{
   std::vector<std::byte> file;
   for (const auto& [type, shape] : everyType())
   {
      if (type == ShapeType::PolygonZ)
      {
         file = writer::shp(type, {shape, Shape{}, shape});
      }
   }
   for (std::size_t size = 0; size < file.size(); ++size)
   {
      const auto prefix = std::span{file}.first(size);
      CHECK_MESSAGE(!tinyshp::load(prefix).has_value(), "prefix of {} bytes", size);

      // with the declared length patched to match, the cut lands inside a record
      if (size >= 100 && size % 2 == 0)
      {
         writer::Bytes patched{std::vector<std::byte>(prefix.begin(), prefix.end())};
         patched.patchBe32(24, static_cast<int32_t>(size / 2));
         const auto result = tinyshp::load(patched._data);
         CHECK_MESSAGE(!result || result->_shapes.size() < 3, "patched prefix of {} bytes", size);
      }
   }

   const auto table = sampleTable();
   for (std::size_t size = 0; size < table.size() - 1; ++size)  // the last byte is the optional end marker
   {
      CHECK_MESSAGE(!tinyshp::loadTable(std::span{table}.first(size)).has_value(), "dbf prefix of {} bytes", size);
   }
}

TEST_CASE("random corruption never crashes")
{
   std::mt19937 random{20261106u};
   const auto shp = sampleFile();
   const auto dbf = sampleTable();
   std::uniform_int_distribution<int> byte_value(0, 255);
   std::size_t loaded = 0;
   for (auto round = 0; round < 3000; ++round)
   {
      auto bad_shp = shp;
      auto bad_dbf = dbf;
      std::uniform_int_distribution<std::size_t> shp_position(0, bad_shp.size() - 1);
      std::uniform_int_distribution<std::size_t> dbf_position(0, bad_dbf.size() - 1);
      for (auto flips = 0; flips < 1 + round % 8; ++flips)
      {
         bad_shp[shp_position(random)] = static_cast<std::byte>(byte_value(random));
         bad_dbf[dbf_position(random)] = static_cast<std::byte>(byte_value(random));
      }
      const auto file = tinyshp::load(tinyshp::Sources{bad_shp, bad_dbf, {}, {}});
      if (file)
      {
         ++loaded;
         for (const auto& shape : file->_shapes)
         {
            for (std::size_t i = 0; i < shape.partCount(); ++i)
            {
               CHECK(shape.part(i).size() <= shape._points.size());
            }
            CHECK(shape._z.empty() || shape._z.size() == shape._points.size());
            CHECK(shape._m.empty() || shape._m.size() == shape._points.size());
            (void)tinyshp::polygons(shape);
         }
         for (const auto& record : file->_attributes->_records)
         {
            for (const auto& value : record._values)
            {
               (void)tinyshp::toString(value);
            }
         }
      }
   }
   std::println("   {} of 3000 corrupted files still loaded", loaded);
}

int main()
{
   return test::runAll();
}
