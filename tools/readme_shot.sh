#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
#
# A picture of the board's own frame (--shot-every, F12 with Square pixels) made into a README picture in the shape
# the arcade's monitor showed it: the board's pixels are narrower than they are tall (384 of them across a 4:3
# screen), so a frame saved as it is drawn looks too wide. Each 224 lines become 864, each 384 columns 1152 (4:3);
# a widescreen frame (512 columns) 1536 (16:9). Rewrites each file in place, as a 256-colour PNG.
#
#   tools/readme_shot.sh docs/screenshots/<game>/title.png ...
#
# Needs ImageMagick (magick).
set -eu
for f in "$@"; do
    size=$(magick identify -format '%w %h' "$f")
    w=${size% *} h=${size#* }
    [ $((h % 224)) -eq 0 ] || { echo "$f: ${w}x$h is not the board's 224 lines (already shaped?)" >&2; exit 1; }
    # (a frame saved at twice the board's size, 448 lines, counts as the board's)
    out_h=864 out_w=$((w * 3 / (h / 224)))
    magick "$f" -filter point -resize 400% -filter Lanczos -resize "${out_w}x${out_h}!" +dither -colors 256 -strip "PNG8:$f.new"
    mv "$f.new" "$f"
    echo "$f: ${w}x$h -> ${out_w}x$out_h"
done
