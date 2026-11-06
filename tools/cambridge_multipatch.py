"""Builds data/cambridge/cambridge_kendall_buildings.{shp,shx,dbf,prj,cpg} from the City of Cambridge 3D buildings.

The city publishes its 3D building models as an Esri File Geodatabase:
  https://cambmagisdata.blob.core.windows.net/camb3d/Camb3D_Active_20220810.gdb.zip
  (https://www.cambridgema.gov/GIS/3D/3ddata/esrishapefile, public domain, ODC PDDL 1.0)

GDAL reads the multipatch geometry as polygons. This script writes them back as a MultiPatch shapefile: triangles
become TriangleFan parts, larger faces OuterRing parts with InnerRing holes. Coordinates stay as published, in
Massachusetts State Plane (NAD83, US survey feet), z in feet above NAVD88.

  pip install fiona
  python tools/cambridge_multipatch.py Camb3D_Active_20220810.gdb data/cambridge
"""

import datetime
import os
import struct
import sys

import fiona

# Kendall Square and MIT, in longitude and latitude, on the buildings' Center_Long and Center_Lat
WEST, SOUTH, EAST, NORTH = -71.0980, 42.3570, -71.0790, 42.3680
NAME = "cambridge_kendall_buildings"

FIELDS = [
    # name, type, length, decimals, source
    ("MODEL_ID", "C", 16, 0, "Model_ID"),
    ("BLDG_ID", "C", 16, 0, "Building_ID"),
    ("NAME", "C", 80, 0, "Name"),
    ("POI_TYPE", "C", 40, 0, "POI_Type"),
    ("HEIGHT_FT", "N", 12, 2, "Height_Ft"),
    ("GROUND_FT", "N", 12, 2, "Ground_Elev_Ft"),
    ("TOP_FT", "N", 12, 2, "Max_Elev_Ft"),
    ("MODEL_DATE", "D", 8, 0, "Model_Date"),
    ("SOURCE", "C", 60, 0, "Model_Source"),
]

TRIANGLE_FAN, OUTER_RING, INNER_RING = 1, 2, 3


def parts_of(geometry):
    """(part type, [(x, y, z)]) for every face of a building."""
    polygons = []
    if geometry.type == "GeometryCollection":
        for child in geometry.geometries:
            polygons += child.coordinates if child.type == "MultiPolygon" else [child.coordinates]
    elif geometry.type == "MultiPolygon":
        polygons = geometry.coordinates
    else:
        polygons = [geometry.coordinates]

    parts = []
    for rings in polygons:
        outer = rings[0]
        if len(rings) == 1 and len(outer) == 4 and outer[0] == outer[-1]:
            parts.append((TRIANGLE_FAN, outer[:3]))
            continue
        parts.append((OUTER_RING, outer))
        parts += [(INNER_RING, hole) for hole in rings[1:]]
    return parts


def record(parts):
    points = [p for _, ring in parts for p in ring]
    xs, ys, zs = [p[0] for p in points], [p[1] for p in points], [p[2] for p in points]
    out = struct.pack("<i4d", 31, min(xs), min(ys), max(xs), max(ys))
    out += struct.pack("<ii", len(parts), len(points))
    first = 0
    for _, ring in parts:
        out += struct.pack("<i", first)
        first += len(ring)
    out += b"".join(struct.pack("<i", kind) for kind, _ in parts)
    out += b"".join(struct.pack("<2d", p[0], p[1]) for p in points)
    out += struct.pack("<2d", min(zs), max(zs)) + b"".join(struct.pack("<d", z) for z in zs)
    return out, (min(xs), min(ys), max(xs), max(ys), min(zs), max(zs))


def header(file_code_length, bounds):
    return struct.pack(">7i", 9994, 0, 0, 0, 0, 0, file_code_length) + struct.pack("<2i", 1000, 31) + struct.pack(
        "<8d", bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5], 0.0, 0.0
    )


def cell(value, kind, length, decimals):
    if value is None:
        text = ""
    elif kind == "N":
        text = f"{value:.{decimals}f}".rjust(length)
    elif kind == "D":
        text = datetime.datetime.fromisoformat(str(value)).strftime("%Y%m%d")
    else:
        text = str(value)
    data = text.encode("utf-8")[:length]
    return data + b" " * (length - len(data))


def main(gdb, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rows, records = [], []
    with fiona.open(gdb, layer="Camb3D_Bldg_Active_MP") as layer:
        crs_wkt = layer.crs_wkt
        for feature in layer:
            props = feature.properties
            if feature.geometry is None or not (WEST <= props["Center_Long"] <= EAST and SOUTH <= props["Center_Lat"] <= NORTH):
                continue
            records.append(record(parts_of(feature.geometry)))
            rows.append([props[source] for *_, source in FIELDS])

    bounds = [min(r[1][i] for r in records) if i in (0, 1, 4) else max(r[1][i] for r in records) for i in range(6)]

    shp, shx = bytearray(), bytearray()
    offset = 50
    for number, (content, _) in enumerate(records, 1):
        shx += struct.pack(">2i", offset, len(content) // 2)
        shp += struct.pack(">2i", number, len(content) // 2) + content
        offset += 4 + len(content) // 2

    base = os.path.join(out_dir, NAME)
    with open(base + ".shp", "wb") as f:
        f.write(header(50 + len(shp) // 2, bounds) + shp)
    with open(base + ".shx", "wb") as f:
        f.write(header(50 + len(shx) // 2, bounds) + shx)

    record_length = 1 + sum(length for _, _, length, _, _ in FIELDS)
    today = datetime.date.today()
    dbf = struct.pack("<4BIHH20x", 3, today.year - 1900, today.month, today.day, len(rows), 32 + 32 * len(FIELDS) + 1, record_length)
    for name, kind, length, decimals, _ in FIELDS:
        dbf += name.encode("ascii").ljust(11, b"\0") + kind.encode("ascii") + b"\0" * 4 + bytes([length, decimals]) + b"\0" * 14
    dbf += b"\r"
    for row in rows:
        dbf += b" " + b"".join(cell(value, kind, length, decimals) for value, (_, kind, length, decimals, _) in zip(row, FIELDS))
    dbf += b"\x1a"
    with open(base + ".dbf", "wb") as f:
        f.write(dbf)

    from fiona.crs import CRS

    with open(base + ".prj", "w") as f:
        f.write(CRS.from_wkt(crs_wkt).to_wkt(version="WKT1_ESRI") if crs_wkt else "")
    with open(base + ".cpg", "w") as f:
        f.write("UTF-8")
    print(f"{len(records)} buildings, {os.path.getsize(base + '.shp')} bytes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
