#!/bin/sh
# Rebuild romfs/puzzles/ (bundled default puzzles) from the original Met Open Access
# images in ignore/defaultimages/: one downscale to long side <= 1024 (the 3DS never uses
# more), one JPEG encode at q95 (measured: worst case ~43 dB PSNR as shown on the 3DS,
# 4.9MB total vs 21.5MB lossless PNG). The lite build (make lite) gets the same pictures at
# q70 from romfs_lite/puzzles (1.3MB total); credits are copied over.
cd "$(dirname "$0")/.." || exit 1
make -f Makefile.host build_host/prescale >/dev/null || exit 1
SRC=ignore/defaultimages
OUT=romfs/puzzles
LITE=romfs_lite/puzzles
mkdir -p "$OUT" "$LITE"
rm -f "$OUT"/*.png "$OUT"/*.jpg "$LITE"/*.jpg
while IFS='|' read -r src name; do
  [ -z "$src" ] && continue
  ./build_host/prescale 1024 "$SRC/$src" "$OUT/$name.jpg" || exit 1
  ./build_host/prescale 1024 "$SRC/$src" "$LITE/$name.jpg" 70 || exit 1
done <<'LIST'
DP-14936-039.jpg|By the Seashore
DP-20821-001.jpg|The Gulf Stream
DP-25460-001.jpg|Woman beside a Vase of Flowers
DP-31997-001-NEW.jpg|Two Men Contemplating the Moon
DP-35674-001.jpg|Madame Charpentier and Children
DP-42549-001.jpg|Wheat Field with Cypresses
DP124477.jpg|Nichiren Calming the Waves
DP145911.jpg|Wheat Fields
DP146455.jpg|Merry Company on a Terrace
DP231550.jpg|The Card Players
DP349564.jpg|View of Toledo
DT1859.jpg|Jalais Hill, Pontoise
LIST
cp "$OUT/CREDITS.txt" "$LITE/CREDITS.txt"
