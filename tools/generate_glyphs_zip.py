#!/usr/bin/env python3
# Mirrors generate_models_zip.py exactly, just for assets/glyphs instead of
# assets/models - see that script for the reasoning behind the approach
# (embed the whole folder as a zip baked into the binary, extracted to the
# user's data directory on first use rather than shipped as loose files).
import os
import zipfile
from io import BytesIO

glyphs_dir = "assets/glyphs"
out_header = "src/glyphs_zip_data.h"

if not os.path.isdir(glyphs_dir):
    print(
        f"Warning: {glyphs_dir} not found — building without embedded input "
        f"history glyphs. The app will still build and run, and Input "
        f"History will still work in text/notation modes, but the "
        f"glyph-based styles won't have any icons to show until you add "
        f"{glyphs_dir}."
    )
    os.makedirs(glyphs_dir, exist_ok=True)

zip_buffer = BytesIO()
with zipfile.ZipFile(zip_buffer, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(glyphs_dir):
        for file in files:
            full_path = os.path.join(root, file)
            arcname = os.path.relpath(full_path, start=os.path.dirname(glyphs_dir))
            zf.write(full_path, arcname)

data = zip_buffer.getvalue()
hex_bytes = ", ".join(f"0x{b:02x}" for b in data)

header = f"""// Auto-generated from {glyphs_dir} – do not edit
#pragma once
namespace Embedded {{
    extern const unsigned char glyphs_zip_data[] = {{
        {hex_bytes}
    }};
    extern const unsigned int glyphs_zip_size = {len(data)};
}}
"""

with open(out_header, "w") as f:
    f.write(header)

print(f"Generated {out_header} ({len(data)} bytes from {glyphs_dir})")
