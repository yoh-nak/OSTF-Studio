"""Import the user-authored Reverb v1.0 GenExpr and connected Max preset tables."""
import json
import re
import sys
from pathlib import Path

patch = json.loads(Path(sys.argv[1]).read_bytes()[32:])["patcher"]
boxes = {b["box"]["id"]: b["box"] for b in patch["boxes"]}
links = [line["patchline"] for line in patch["lines"]]
folder = Path(__file__).resolve().parents[1] / "src/plugins/effects/knobber"
folder.mkdir(exist_ok=True)
header = "// Derived from SUPER KNOBBER REVERB v1.0. See tools/import_knobber_reverb.py.\n"
def emit(name, s):
    (folder / name).write_text(header + s, encoding="utf-8")

def items(menu):
    return [v for v in boxes[menu]["items"] if v != ","]

code = next(b["box"]["code"] for b in boxes["reverb-gen"]["patcher"]["boxes"] if "code" in b["box"])
decl = re.compile(r"\b(Delay|Param|History)\s+(\w+)\(([-.\d]+)\);")
declarations = decl.findall(code)
body = decl.sub("", code).strip()
histories = {n for t,n,v in declarations if t == "History"}
params = {n for t,n,v in declarations if t == "Param"}
delays = {n for t,n,v in declarations if t == "Delay"}
controls = []
for i in range(1,109):
    dial = boxes[f"dial-{i}"]["saved_attribute_attributes"]["valueof"]
    route = boxes.get(f"pre-{i}", {}).get("text","").split()
    pid = route[-1] if route else {51:"shimmermix",52:"pitchone",53:"pitchtwo",63:"mixamount",64:"outputdb"}[i]
    controls.append((pid,dial["parameter_longname"],dial["parameter_mmin"],dial["parameter_mmax"],dial["parameter_initial"][0]))
assert params - {c[0] for c in controls} == {"rtype"}
def initializer(row):
    return "{" + ", ".join(json.dumps(v) for v in row) + "}"
def table(name, rows):
    return f"inline constexpr double {name}[][{len(rows[0])}] = {{\n" + ",\n".join(initializer(row) for row in rows) + "};\n"
data = "#pragma once\nnamespace knobverb {\nstruct Control { const char* id; const char* name; double low, high, initial; };\n"
data += "inline constexpr Control controls[] = {\n" + ",\n".join(initializer(c) for c in controls) + "};\n"
for var, menu in [("types","type-menu"),("sources","genre-menu"),("styles","style-menu")]:
    data += f"inline constexpr const char* {var}[] = "+initializer(items(menu))+";\n"
for var,selector in [("typesBank","type-sel"),("sourcesBank","genre-sel"),("stylesBank","style-sel"),("depthBank","preset-depth-sel-v087")]:
    connected=sorted((q["source"][1],q["destination"][0]) for q in links if q["source"][0]==selector)
    rows=[[float(v) for v in boxes[key]["text"].split()] for _,key in connected]
    assert len(set(map(len,rows)))==1
    data+=table(var,rows)
for var,unpack,count in [("sourceMap","genre-unpack",12),("styleMap","style-unpack",25)]:
    mapping=[-1]*count
    for q in links:
        match=re.fullmatch(r"dial-(\d+)",q["destination"][0])
        if q["source"][0]==unpack and match: mapping[q["source"][1]]=int(match[1])-1
    data+=f"inline constexpr int {var}[] = "+initializer(mapping)+";\n"
default_index = int(boxes["load-type"]["text"].split()[-1])
data+=f"inline constexpr int initialType = {items('type-menu').index(items('type-menu-az-v067')[default_index])};\n"
emit("Controls.h",data+"}\n")
engine = '#pragma once\n#include "../../instruments/knobber/Primitives.h"\n#include "Controls.h"\nnamespace knobverb {\nstruct Engine {\n'
for t,n,v in declarations:
    if t=="Delay": engine+=f"knobber::Delay {n}{{{v}}};\n"
    if t=="History": engine+=f"double {n} = {v};\n"
engine+="double beforeLeft{}, beforeRight{};\nvoid reset();\nknobber::Stereo process(double in1,double in2,double samplerate,int rtype,const std::array<double,108>& controls);\n};\n}\n"
emit("Engine.h",engine)
plain=re.sub(r"//[^\n]*","",body)
locals_=list(dict.fromkeys(n for n in re.findall(r'\b(\w+)\s*(?:=(?!=)|\+=|-=|\*=|/=)',plain) if n not in histories|params))
source='#include "Engine.h"\nnamespace knobverb {\nusing namespace knobber;\nvoid Engine::reset(){\n'
for t,n,v in declarations:
    if t=="Delay": source+=f"{n}.clear();\n"
    if t=="History": source+=f"{n}={v};\n"
source+='beforeLeft=beforeRight=0;\n}\nStereo Engine::process(double in1,double in2,double samplerate,int rtype,const std::array<double,108>& values){\n'
for i,(n,*_) in enumerate(controls):
    if n in params: source+=f"const double {n}=values[{i}];\n"
source+="using std::abs;\n"
# Keep values spanning conditional algorithm branches in the GenExpr function scope.
# Declare straight-line temporaries at their first assignment.
depth=0
hoisted=set()
for m in re.finditer(r'[{}]|\b(\w+)\s*=(?!=)',plain):
    token=m.group(0)
    if token=='{': depth+=1
    elif token=='}': depth-=1
    elif m[1] in locals_ and depth: hoisted.add(m[1])
for n in locals_:
    if n in hoisted: source+=f"double {n} = 0;\n"
    else: body=re.sub(r'\b'+n+r'\s*=(?!=)',f'double {n} = ',body,count=1)
source+=body+'\n'
for t,n,v in declarations:
    if t=="Delay": source+=f"{n}.advance();\n"
emit("Engine.cpp",source+"beforeLeft=out3; beforeRight=out4;\nreturn {out1,out2};\n}\n}\n")
