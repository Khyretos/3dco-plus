#!/usr/bin/env python3
import os
import sys

input_file = "assets/icon.png"
output_header = "src/icon_data.h"

if not os.path.isfile(input_file):
    print(f"Error: {input_file} not found", file=sys.stderr)
    sys.exit(1)

with open(input_file, "rb") as f:
    data = f.read()

hex_bytes = ", ".join(f"0x{b:02x}" for b in data)

header = f"""// Auto-generated from {input_file} – do not edit
#pragma once
#include <cstddef>

namespace Embedded {{
    static const unsigned char icon_data[] = {{
        {hex_bytes}
    }};
    static constexpr std::size_t icon_size = {len(data)};
}}
"""

with open(output_header, "w") as f:
    f.write(header)

print(f"Generated {output_header} ({len(data)} bytes)")
