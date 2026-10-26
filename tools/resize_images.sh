#!/bin/sh
# Resize every image in a directory to long side <= 1024 (never upscaled) and write it
# as PNG, ready to drop into the 3DS puzzles folder. Images already small enough in a
# format the app decodes (jpg/png/bmp) are copied untouched, unless EXIF says they're
# rotated (the app ignores EXIF, so those are re-encoded with the rotation applied). Uses the first available of:
# ImageMagick 7 (magick), ImageMagick 6 (convert), libvips (vipsthumbnail), ffmpeg.
# Usage: resize_images.sh INDIR [OUTDIR]   (OUTDIR defaults to INDIR/resized)
MAX=1024
[ -d "$1" ] || { echo "usage: $0 INDIR [OUTDIR]" >&2; exit 1; }
IN=$1
OUT=${2:-$1/resized}
mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd) || exit 1  # absolute: vipsthumbnail -o is relative to the input

if command -v magick >/dev/null; then TOOL=magick
elif command -v convert >/dev/null && convert -version 2>/dev/null | grep -q ImageMagick; then TOOL=convert
elif command -v vipsthumbnail >/dev/null; then TOOL=vips
elif command -v ffmpeg >/dev/null; then TOOL=ffmpeg
else echo "need one of: magick, convert, vipsthumbnail, ffmpeg" >&2; exit 1
fi
echo "using $TOOL"

# Prints "W H"
dims() {
  case $TOOL in
    magick) magick identify -format '%w %h' "$1[0]" ;;
    convert) identify -format '%w %h' "$1[0]" ;;
    vips) echo "$(vipsheader -f width "$1") $(vipsheader -f height "$1")" ;;
    ffmpeg) ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$1" | tr , ' ' ;;
  esac
}

# True if EXIF orientation is anything but normal (missing counts as normal)
rotated() {
  case $TOOL in
    magick) o=$(magick identify -format '%[EXIF:Orientation]' "$1[0]" 2>/dev/null) ;;
    convert) o=$(identify -format '%[EXIF:Orientation]' "$1[0]" 2>/dev/null) ;;
    vips) o=$(vipsheader -f orientation "$1" 2>/dev/null) ;;
    # ffmpeg reports EXIF orientation as a display matrix rotation
    ffmpeg) o=$(ffprobe -v error -select_streams v:0 -read_intervals %+#1 \
      -show_entries frame_side_data=rotation -of csv=p=0 "$1" | tr -d ', \n')
      [ "$o" = 0 ] && o= ;;
  esac
  [ -n "$o" ] && [ "$o" != 1 ]
}

fail=0
for f in "$IN"/*; do
  [ -f "$f" ] || continue
  ext=$(printf '%s' "${f##*.}" | tr 'A-Z' 'a-z')
  case $ext in
    jpg|jpeg|png|webp|bmp|gif|tif|tiff) ;;
    *) continue ;;
  esac
  name=$(basename "$f")
  case $ext in
    jpg|jpeg|png|bmp)
      set -- $(dims "$f")
      if [ -n "$2" ] && [ "$1" -le $MAX ] && [ "$2" -le $MAX ] && ! rotated "$f"; then
        cp "$f" "$OUT/" && echo "$name (copied)" && continue
      fi ;;
  esac
  dst="$OUT/${name%.*}.png"
  case $TOOL in
    # [0]: first frame only (gifs); -auto-orient applies EXIF rotation; > = shrink only
    magick|convert) $TOOL "$f[0]" -auto-orient -resize "${MAX}x${MAX}>" "$dst" ;;
    vips) vipsthumbnail "$f" --size "${MAX}x${MAX}>" -o "$dst" ;;
    ffmpeg) ffmpeg -nostdin -loglevel error -y -i "$f" -frames:v 1 \
      -vf "scale='min($MAX,iw)':'min($MAX,ih)':force_original_aspect_ratio=decrease:flags=area" \
      "$dst" ;;
  esac
  if [ $? -eq 0 ]; then echo "$name -> $dst"; else echo "FAILED: $name" >&2; fail=1; fi
done
exit $fail
