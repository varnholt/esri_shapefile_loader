// converts the 3D shapes of a shapefile (MultiPatch, PolygonZ, PolyLineZ) to a Wavefront OBJ, one object per record.
//
//   shp2obj buildings.shp out.obj
//
// Triangle strips and fans become triangles, rings become polygon faces (OBJ has no holes, so inner rings are left out).
// Polylines become lines. OBJ is y-up: x stays x, z becomes y and y becomes -z. The coordinates are moved by the
// minimum of the file bounds, written as a comment, so floats keep their precision with projected coordinates.

#include <cstddef>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string>

#include <tinyshp/tinyshp.hpp>

int main(int argc, char* argv[])
{
   const std::span args(argv, static_cast<std::size_t>(argc));
   if (args.size() != 3)
   {
      std::println(stderr, "usage: shp2obj file.shp out.obj");
      return 2;
   }
   const auto file = tinyshp::loadFile(args[1]);
   if (!file)
   {
      std::println(stderr, "{}: {}", args[1], tinyshp::describe(file.error()));
      return 1;
   }

   const auto& bounds = file->_header._bounds;
   const double z_origin = file->_header._z_range._min;
   std::ofstream out(args[2], std::ios::binary);
   out << std::format("# {} from {}\n# offset x {} y {} z {}\n", tinyshp::describe(file->_header._type), args[1], bounds._x_min, bounds._y_min, z_origin);

   // the .dbf column used to name objects, if there is one
   std::optional<std::size_t> name_field;
   if (file->_attributes)
   {
      name_field = file->_attributes->fieldIndex("NAME");
   }

   std::size_t vertex_base = 1;  // obj indices start at 1
   std::size_t triangles = 0;
   std::size_t faces = 0;
   for (std::size_t s = 0; s < file->_shapes.size(); ++s)
   {
      const auto& shape = file->_shapes[s];
      if (shape.isNull())
      {
         continue;
      }
      std::string name = std::format("shape_{}", shape._record_number);
      if (name_field)
      {
         const auto label = tinyshp::toString(file->_attributes->_records[s]._values[*name_field]);
         if (!label.empty())
         {
            name += "_" + label;
         }
      }
      for (auto& c : name)
      {
         c = c == ' ' ? '_' : c;
      }
      out << "o " << name << '\n';

      for (std::size_t i = 0; i < shape._points.size(); ++i)
      {
         const double z = shape._z.empty() ? 0.0 : shape._z[i] - z_origin;
         out << std::format("v {} {} {}\n", shape._points[i]._x - bounds._x_min, z, -(shape._points[i]._y - bounds._y_min));
      }

      const auto base = tinyshp::baseType(shape._type);
      for (std::size_t p = 0; p < shape.partCount(); ++p)
      {
         const auto [first, last] = shape.partRange(p);
         const auto index = [&](std::size_t i) { return vertex_base + i; };
         const auto type = p < shape._part_types.size() ? shape._part_types[p] : tinyshp::PartType::OuterRing;

         if (base == tinyshp::ShapeType::PolyLine)
         {
            out << 'l';
            for (std::size_t i = first; i < last; ++i)
            {
               out << ' ' << index(i);
            }
            out << '\n';
         }
         else if (base == tinyshp::ShapeType::MultiPatch && type == tinyshp::PartType::TriangleStrip)
         {
            for (std::size_t i = first; i + 2 < last; ++i)
            {
               // every other triangle of a strip is flipped to keep the winding
               const bool odd = (i - first) % 2 == 1;
               out << std::format("f {} {} {}\n", index(i), index(odd ? i + 2 : i + 1), index(odd ? i + 1 : i + 2));
               ++triangles;
            }
         }
         else if (base == tinyshp::ShapeType::MultiPatch && type == tinyshp::PartType::TriangleFan)
         {
            for (std::size_t i = first + 1; i + 1 < last; ++i)
            {
               out << std::format("f {} {} {}\n", index(first), index(i), index(i + 1));
               ++triangles;
            }
         }
         else if (base == tinyshp::ShapeType::Polygon || base == tinyshp::ShapeType::MultiPatch)
         {
            const auto ring = shape.part(p);
            const bool hole = base == tinyshp::ShapeType::MultiPatch ? type == tinyshp::PartType::InnerRing : !tinyshp::isClockwise(ring);
            // rings repeat their first point at the end; a face lists it once
            const std::size_t end = last - first > 1 && ring.front() == ring.back() ? last - 1 : last;
            if (hole || end - first < 3)
            {
               continue;
            }
            out << 'f';
            for (std::size_t i = first; i < end; ++i)
            {
               out << ' ' << index(i);
            }
            out << '\n';
            ++faces;
         }
      }
      vertex_base += shape._points.size();
   }

   if (!out)
   {
      std::println(stderr, "could not write {}", args[2]);
      return 1;
   }
   std::println("wrote {} shapes, {} triangles and {} polygon faces to {}", file->_shapes.size(), triangles, faces, args[2]);
   return 0;
}
