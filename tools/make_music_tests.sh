#!/bin/sh
# Test files for ./build_host/tests musicdec DIR (needs ffmpeg with libmp3lame, flac, libvorbis).
# 10 s of a 440 Hz tone in each format, stereo 44.1 kHz and mono 48 kHz, plus 6-channel
# files and junk with a music extension.
set -e
out=${1:?usage: make_music_tests.sh OUTDIR}
mkdir -p "$out"
tone="sine=frequency=440:duration=10"
ff() { ffmpeg -loglevel error -y "$@"; }
for ext in mp3 flac ogg; do
  case $ext in
    mp3) codec="-c:a libmp3lame -b:a 192k" ;;
    flac) codec="-c:a flac" ;;
    ogg) codec="-c:a libvorbis -q:a 5" ;;
  esac
  ff -f lavfi -i "$tone:sample_rate=44100" -ac 2 $codec "$out/stereo44.$ext"
  ff -f lavfi -i "$tone:sample_rate=48000" -ac 1 $codec "$out/mono48.$ext"
done
ff -f lavfi -i "$tone:sample_rate=48000" -ac 6 -c:a flac "$out/six.flac"
ff -f lavfi -i "$tone:sample_rate=48000" -ac 6 -c:a libvorbis "$out/six.ogg"
head -c 2000000 /dev/urandom > "$out/junk.mp3"
head -c 2000000 /dev/urandom > "$out/junk.flac"
head -c 2000000 /dev/urandom > "$out/junk.ogg"
