#!/bin/sh
# Compile (no link) check the 3DS code with host gcc against stub headers (tools/stubs).
# Catches typos and type errors only; the stubs are not the real libctru/citro2d.
cd "$(dirname "$0")/.." || exit 1
for f in source/*.c source/external/*.c source_app/*.c; do
  gcc -c -o /dev/null -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -D__3DS__ -DAPP_VERSION_MAJOR=1 -DAPP_VERSION_MINOR=0 -DAPP_VERSION_MICRO=0 -DAPP_VERSION_STRING='"1.0.0"' \
    -Itools/stubs -Isource -Isource/external -Isource_app "$f" || echo "FAILED: $f"
done
