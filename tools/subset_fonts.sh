#!/usr/bin/env bash
# Regenerates the subset Noto fonts in assets/fonts/ from the upstream
# google/fonts files. Only needed when changing which characters are
# covered - the results are committed. Requires: pip install fonttools
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
B=https://raw.githubusercontent.com/google/fonts/main/ofl
curl -sSLf -o "$tmp/sans.ttf" "$B/notosans/NotoSans%5Bwdth,wght%5D.ttf"
curl -sSLf -o "$tmp/math.ttf" "$B/notosansmath/NotoSansMath-Regular.ttf"
curl -sSLf -o "$tmp/sym2.ttf" "$B/notosanssymbols2/NotoSansSymbols2-Regular.ttf"

# Variable font -> static Regular instance (what the UI renders).
python3 -m fontTools.varLib.instancer "$tmp/sans.ttf" wght=400 wdth=100 -o "$tmp/sans-400.ttf" -q

common=(--layout-features= --no-hinting)
pyftsubset "$tmp/sans-400.ttf" "${common[@]}" --output-file=assets/fonts/NotoSans-Subset.ttf \
  --unicodes="U+0020-024F,U+0300-036F,U+0370-03FF,U+0400-04FF,U+1E00-1EFF,U+2000-206F,U+20A0-20CF,U+2100-218F,U+2190-21FF,U+2200-22FF,U+2460-24FF,U+2500-259F,U+25A0-25FF,U+2600-26FF,U+2700-27BF,U+FFFD"
pyftsubset "$tmp/math.ttf" "${common[@]}" --output-file=assets/fonts/NotoSansMath-Subset.ttf \
  --unicodes="U+2190-21FF,U+2200-22FF,U+2300-23FF,U+27F0-27FF,U+2900-297F"
pyftsubset "$tmp/sym2.ttf" "${common[@]}" --output-file=assets/fonts/NotoSansSymbols2-Subset.ttf \
  --unicodes="U+2190-21FF,U+2300-23FF,U+2460-24FF,U+2500-27BF,U+2B00-2BFF"
rm -rf "$tmp"
ls -la assets/fonts/*.ttf
