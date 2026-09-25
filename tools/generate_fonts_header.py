#!/usr/bin/env python3
# Embeds the UI fonts in assets/fonts/ (see assets/fonts/README.txt) into
# src/fonts_data.h, same pattern as generate_icon_header.py. Only
# app_fonts.cpp includes the result.
import os
import sys

fonts_dir = "assets/fonts"
out_header = "src/fonts_data.h"
fonts = [
    ("noto_sans", "NotoSans-Subset.ttf"),
    ("noto_sans_math", "NotoSansMath-Subset.ttf"),
    ("noto_sans_symbols2", "NotoSansSymbols2-Subset.ttf"),
    ("twemoji", "Twemoji.Mozilla.ttf"),
]

parts = [f"// Auto-generated from {fonts_dir} – do not edit",
         "#pragma once", "#include <cstddef>", "", "namespace Embedded {"]
for name, filename in fonts:
    path = os.path.join(fonts_dir, filename)
    if not os.path.isfile(path):
        print(f"Error: {path} not found", file=sys.stderr)
        sys.exit(1)
    with open(path, "rb") as f:
        data = f.read()
    hex_bytes = ",".join(f"0x{b:02x}" for b in data)
    parts.append(f"    static const unsigned char {name}_ttf[] = {{{hex_bytes}}};")
    parts.append(f"    static constexpr std::size_t {name}_ttf_size = {len(data)};")
parts.append("}")

with open(out_header, "w") as f:
    f.write("\n".join(parts) + "\n")

print(f"Generated {out_header} ({len(fonts)} fonts)")
