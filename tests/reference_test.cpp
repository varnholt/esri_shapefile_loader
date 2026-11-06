// loads real shapefiles: Natural Earth (points, lines, polygons with holes, utf-8 attributes) and the City of Cambridge
// 3D buildings (MultiPatch in a projected coordinate system). The expected values were read with GDAL.

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "test_framework.h"
#include "tinyshp/tinyshp.hpp"

namespace
{

const std::filesystem::path data_dir{TINYSHP_DATA_DIR};

tinyshp::Shapefile load(const std::string& name)
{
   auto file = tinyshp::loadFile(data_dir / name);
   CHECK_MESSAGE(file.has_value(), "{}: {}", name, file ? "" : tinyshp::describe(file.error()));
   return file ? std::move(*file) : tinyshp::Shapefile{};
}

std::optional<std::size_t> findRecord(const tinyshp::Shapefile& file, std::string_view field, std::string_view value)
{
   if (!file._attributes)
   {
      return std::nullopt;
   }
   for (std::size_t i = 0; i < file._attributes->_records.size(); ++i)
   {
      if (file._attributes->text(i, field) == value)
      {
         return i;
      }
   }
   return std::nullopt;
}

/// \brief every record lies in the header bounds and every point in its record's bounds.
void checkBounds(const tinyshp::Shapefile& file, std::string_view name)
{
   const auto inside = [](const tinyshp::Box& outer, const tinyshp::Box& inner)
   {
      constexpr double slack = 1e-9;
      return inner._x_min >= outer._x_min - slack && inner._y_min >= outer._y_min - slack &&
             inner._x_max <= outer._x_max + slack && inner._y_max <= outer._y_max + slack;
   };
   std::size_t bad = 0;
   for (const auto& shape : file._shapes)
   {
      if (shape.isNull())
      {
         continue;
      }
      bad += inside(file._header._bounds, shape._bounds) ? 0 : 1;
      for (const auto& point : shape._points)
      {
         bad += shape._bounds.contains(point) ? 0 : 1;
      }
   }
   CHECK_MESSAGE(bad == 0, "{}: {} points or records outside their bounds", name, bad);
}

}  // namespace

TEST_CASE("natural earth countries: polygons, holes and utf-8 attributes")
{
   const auto file = load("naturalearth/ne_110m_admin_0_countries.shp");
   CHECK(file._header._type == tinyshp::ShapeType::Polygon);
   CHECK(file._shapes.size() == 177);
   CHECK(file.isGeographic());
   CHECK(file._attributes && file._attributes->_fields.size() == 168);
   checkBounds(file, "countries");

   const auto germany = findRecord(file, "NAME", "Germany");
   const auto france = findRecord(file, "NAME", "France");
   const auto south_africa = findRecord(file, "NAME", "South Africa");
   const auto lesotho = findRecord(file, "NAME", "Lesotho");
   const auto japan = findRecord(file, "NAME", "Japan");
   CHECK(germany == 121u && france == 43u && south_africa == 25u && lesotho == 26u && japan == 155u);
   if (!germany || !france || !south_africa || !lesotho || !japan)
   {
      return;
   }

   const auto& countries = *file._attributes;
   CHECK(countries.number(*germany, "POP_EST") == 83132799.0);
   CHECK(countries.text(*germany, "CONTINENT") == "Europe");
   CHECK(countries.text(*japan, "NAME_JA") == "\xe6\x97\xa5\xe6\x9c\xac");  // 日本

   const tinyshp::Point berlin{13.3996, 52.5238};
   const tinyshp::Point paris{2.3522, 48.8566};
   const tinyshp::Point maseru{27.4833, -29.3167};
   CHECK(tinyshp::contains(file._shapes[*germany], berlin));
   CHECK(!tinyshp::contains(file._shapes[*germany], paris));
   CHECK(tinyshp::contains(file._shapes[*france], paris));

   // Lesotho is a hole in South Africa
   CHECK(file._shapes[*south_africa].partCount() == 2);
   CHECK(!tinyshp::contains(file._shapes[*south_africa], maseru));
   CHECK(tinyshp::contains(file._shapes[*lesotho], maseru));
   const auto groups = tinyshp::polygons(file._shapes[*south_africa]);
   CHECK(groups.size() == 1 && groups[0]._holes.size() == 1);

   // France: mainland, Corsica and French Guiana
   CHECK(file._shapes[*france].partCount() == 3);
   CHECK(tinyshp::polygons(file._shapes[*france]).size() == 3);

   // outer rings are clockwise everywhere, as the specification wants
   std::size_t outer = 0;
   std::size_t holes = 0;
   for (const auto& shape : file._shapes)
   {
      for (const auto& polygon : tinyshp::polygons(shape))
      {
         outer += tinyshp::isClockwise(polygon._outer) ? 1 : 0;
         holes += polygon._holes.size();
      }
   }
   CHECK(holes == 1);
   CHECK(outer == 288);
}

TEST_CASE("natural earth places, rivers and lakes")
{
   const auto places = load("naturalearth/ne_110m_populated_places_simple.shp");
   CHECK(places._header._type == tinyshp::ShapeType::Point);
   CHECK(places._shapes.size() == 243);
   checkBounds(places, "places");
   const auto tokyo = findRecord(places, "name", "Tokyo");
   CHECK(tokyo == 233u);
   if (tokyo)
   {
      const auto point = places._shapes[*tokyo]._points.at(0);
      CHECK(std::abs(point._x - 139.7494616) < 1e-6 && std::abs(point._y - 35.6869628) < 1e-6);
      CHECK(places._attributes->number(*tokyo, "pop_max") == 35676000.0);
   }

   const auto rivers = load("naturalearth/ne_110m_rivers_lake_centerlines.shp");
   CHECK(rivers._header._type == tinyshp::ShapeType::PolyLine);
   CHECK(rivers._shapes.size() == 13);
   checkBounds(rivers, "rivers");

   const auto lakes = load("naturalearth/ne_110m_lakes.shp");
   CHECK(lakes._shapes.size() == 24);
   checkBounds(lakes, "lakes");
}

TEST_CASE("natural earth 50m countries and 10m time zones")
{
   const auto countries = load("naturalearth/ne_50m_admin_0_countries.shp");
   CHECK(countries._shapes.size() == 242);
   checkBounds(countries, "50m countries");

   const auto zones = load("naturalearth/ne_10m_time_zones.shp");
   CHECK(zones._shapes.size() == 120);
   CHECK(zones._attributes && zones._attributes->fieldIndex("zone").has_value());
   checkBounds(zones, "time zones");
}

TEST_CASE("cambridge buildings: MultiPatch with z, in state plane feet")
{
   const auto file = load("cambridge/cambridge_kendall_buildings.shp");
   CHECK(file._header._type == tinyshp::ShapeType::MultiPatch);
   CHECK(file._shapes.size() == 709);
   CHECK(!file.isGeographic());
   CHECK(file._projection.find("StatePlane_Massachusetts") != std::string::npos);
   checkBounds(file, "cambridge");
   CHECK(std::abs(file._header._bounds._x_min - 764635.25215514) < 1e-6);
   CHECK(std::abs(file._header._bounds._y_max - 2959513.765613064) < 1e-6);

   std::size_t fans = 0;
   std::size_t rings = 0;
   for (const auto& shape : file._shapes)
   {
      CHECK(shape._z.size() == shape._points.size());
      CHECK(shape._part_types.size() == shape._parts.size());
      CHECK(shape._z_range._min <= shape._z_range._max);
      for (const auto type : shape._part_types)
      {
         fans += type == tinyshp::PartType::TriangleFan ? 1 : 0;
         rings += type == tinyshp::PartType::OuterRing ? 1 : 0;
      }
   }
   CHECK(fans > 0 && rings > 0);

   const auto museum = findRecord(file, "NAME", "MIT Museum, Building N51");
   CHECK(museum == 8u);
   if (museum)
   {
      CHECK(file._attributes->number(*museum, "HEIGHT_FT") == 53.68);
      CHECK(file._attributes->text(*museum, "MODEL_DATE") == "2010-04-14");
      const auto& shape = file._shapes[*museum];
      CHECK(shape._z_range._max - shape._z_range._min > 40.0);
   }
}

int main()
{
   return test::runAll();
}
