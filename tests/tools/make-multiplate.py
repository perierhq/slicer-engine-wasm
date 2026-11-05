#!/usr/bin/env python3
"""Writes tests/inputs/multiplate.3mf from tests/inputs/twocolour.3mf: three plates in Bambu Studio's layout.

  plate 1  "Two colours": cube A (filament 1) and cube B (filament 2), as in twocolour.3mf
  plate 2  "One colour": a third cube, filament 1
  plate 3  "Textured": a fourth cube, filament 2, on the textured PEI plate (plate setting)

Plates sit where Bambu Studio puts them: columns = ceil-ish sqrt(count) (PartPlate.hpp compute_colum_count), stride
1.2 × the printable width/depth (LOGICAL_PART_PLATE_GAP), rows going towards -y. The P1S bed is 256 × 256.
"""
import re
import sys
import zipfile

src, dst = sys.argv[1], sys.argv[2]
stride = 256 * 1.2
with zipfile.ZipFile(src) as z:
    files = {i.filename: z.read(i.filename) for i in z.infolist()}

model = files["3D/3dmodel.model"].decode()
# Duplicate the mesh objects 1/2 (cube A) as 5/6 and 3/4 (cube B) as 7/8.
def object_block(oid):
    return re.search(r'  <object id="%d"[^>]*>.*?</object>\n' % oid, model, re.S).group(0)
extra = ""
for old, new in ((1, 5), (2, 6), (3, 7), (4, 8)):
    block = object_block(old).replace('<object id="%d"' % old, '<object id="%d"' % new)
    block = block.replace('objectid="%d"' % (old - 1), 'objectid="%d"' % (new - 1)) if old in (2, 4) else block
    block = block.replace('name="Cube A"', 'name="Cube C"').replace('name="Cube B"', 'name="Cube D"')
    extra += block
model = model.replace(" </resources>", extra + " </resources>")
model = model.replace(
    ' </build>',
    '  <item objectid="6" transform="1 0 0 0 1 0 0 0 1 %g 128 0" printable="1"/>\n'
    '  <item objectid="8" transform="1 0 0 0 1 0 0 0 1 128 %g 0" printable="1"/>\n </build>' % (stride + 128, 128 - stride))
files["3D/3dmodel.model"] = model.encode()

settings = files["Metadata/model_settings.config"].decode()
objects = ""
for oid, part, name, extruder in ((6, 5, "Cube C", 1), (8, 7, "Cube D", 2)):
    objects += (' <object id="%d">\n  <metadata key="name" value="%s"/>\n  <metadata key="extruder" value="%d"/>\n'
                '  <part id="%d" subtype="normal_part">\n   <metadata key="name" value="%s"/>\n'
                '   <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n  </part>\n </object>\n') % (oid, name, extruder, part, name)
plates = settings[settings.index(" <plate>"):settings.index("</config>")]
plates = plates.replace('<metadata key="plater_name" value=""/>', '<metadata key="plater_name" value="Two colours"/>')
def plate(index, name, oid, extra=""):
    return (' <plate>\n  <metadata key="plater_id" value="%d"/>\n  <metadata key="plater_name" value="%s"/>\n'
            '  <metadata key="locked" value="false"/>\n%s  <model_instance>\n   <metadata key="object_id" value="%d"/>\n'
            '   <metadata key="instance_id" value="0"/>\n  </model_instance>\n </plate>\n') % (index, name, extra, oid)
plates += plate(2, "One colour", 6) + plate(3, "Textured", 8, '  <metadata key="bed_type" value="Textured PEI Plate"/>\n')
settings = settings[:settings.index(" <plate>")] + objects + plates + "</config>\n"
files["Metadata/model_settings.config"] = settings.encode()

with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in files.items():
        z.writestr(name, data)
