# Sample data

Real shapefiles for the reference tests and the testbed scenes.

## naturalearth/

From [Natural Earth](https://www.naturalearthdata.com/), via
[nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector). Public domain.

| file | shape type | records |
|---|---|---|
| `ne_110m_admin_0_countries` | Polygon | 177 |
| `ne_50m_admin_0_countries` | Polygon | 242 |
| `ne_10m_time_zones` | Polygon | 120 |
| `ne_110m_lakes` | Polygon | 24 |
| `ne_110m_rivers_lake_centerlines` | PolyLine | 13 |
| `ne_110m_populated_places_simple` | Point | 243 |

## cambridge/

`cambridge_kendall_buildings`: 709 MultiPatch buildings around Kendall Square and MIT from the
[City of Cambridge 3D model](https://www.cambridgema.gov/GIS/3D/3ddata/esrishapefile), in Massachusetts State Plane
(NAD83, US survey feet) with z in feet. City of Cambridge GIS data is published under the
[ODC Public Domain Dedication and License 1.0](https://opendatacommons.org/licenses/pddl/1.0/).

The city publishes the model as a File Geodatabase; `tools/cambridge_multipatch.py` turns the buildings in this area
back into a MultiPatch shapefile (triangle fans, outer and inner rings) with a few of their attributes.
