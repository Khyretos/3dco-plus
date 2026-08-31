#!/usr/bin/env python3
import os
import sys
import json

shaders_dir = "assets/shaders"
out_header = "src/shaders_data.h"

if not os.path.isdir(shaders_dir):
    print(f"Warning: {shaders_dir} not found – creating empty.", file=sys.stderr)
    os.makedirs(shaders_dir, exist_ok=True)

shader_data = {}
for root, dirs, files in os.walk(shaders_dir):
    for d in dirs:
        folder = os.path.join(root, d)
        frag_path = os.path.join(folder, "fragment.glsl")
        if os.path.isfile(frag_path):
            with open(frag_path, "r") as f:
                frag_src = f.read()
            shader_data[d] = {"fragment": frag_src}

json_str = json.dumps(shader_data, indent=2)
escaped = json_str.replace("\\", "\\\\").replace('"', '\\"').replace("\n", '\\n"\n"')

header = f"""// Auto-generated from {shaders_dir} – do not edit
#pragma once
#include <cstddef>

namespace Embedded {{
    static const char shaders_data[] =
        "{escaped}";
}}
"""

with open(out_header, "w") as f:
    f.write(header)

print(f"Generated {out_header} with {len(shader_data)} shaders")
