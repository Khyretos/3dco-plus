Fonts embedded into 3dco+ at build time (see tools/generate_fonts_header.py).
All are free to redistribute, including commercially.

NotoSans-Subset.ttf          Main UI font.
                             Noto Sans, SIL Open Font License 1.1 (OFL-NotoSans.txt).
NotoSansMath-Subset.ttf      Fallback for arrows and math symbols.
                             Noto Sans Math, SIL OFL 1.1 (OFL-NotoSansMath.txt).
NotoSansSymbols2-Subset.ttf  Fallback for check marks, shapes and other symbols.
                             Noto Sans Symbols 2, SIL OFL 1.1 (OFL-NotoSansSymbols2.txt).
Twemoji.Mozilla.ttf          Color emoji (COLRv0), unmodified release v0.7.0 from
                             https://github.com/mozilla/twemoji-colr
                             Emoji artwork: Copyright Twitter, Inc and other contributors,
                             CC-BY 4.0. Font build: Mozilla, Apache 2.0 (LICENSE-Twemoji.txt).

The three Noto files are subsets of the upstream fonts (google/fonts repository,
default weight/width instance, trimmed to the Unicode ranges the app needs). To
regenerate them, see tools/subset_fonts.sh.
