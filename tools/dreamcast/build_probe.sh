#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
mkdir -p build/dreamcast-host
TOOL="${1:-asset-probe}"
case "$TOOL" in
    asset-probe) ENTRY="tools/dreamcast/asset_probe.c" ;;
    test-stream-media) ENTRY="tools/dreamcast/test_stream_media.c" ;;
    splash-frames) ENTRY="" ;;    # (the headless game with the draw-list tools: the loading screen's pictures)
    *) echo "Unknown tool: $TOOL" >&2; exit 2 ;;
esac
EXTRA=()
if [[ "$TOOL" == splash-frames ]]; then
    SOURCES=("ports/headless/main.c" "ports/dreamcast/pvr/drawlist.c" "ports/dreamcast/pvr/render_emit.c" "tools/dreamcast/drawlist_check.c")
    EXTRA=(-DDL_CHECK -I src/sound)
else
    SOURCES=("$ENTRY" "ports/dreamcast/page_cache.c" "ports/dreamcast/stream_media.c")
fi
for f in src/*.c src/*/*.c; do
    [[ -f "$f" ]] && SOURCES+=("$f")
done
for f in runtime/src/machine.c runtime/src/io.c runtime/src/script.c runtime/src/sound.c runtime/src/ym3438.c runtime/src/memory_census.c runtime/src/tasks.c runtime/src/video.c runtime/src/workers.c runtime/src/multipcm.c runtime/src/sound_chips.c runtime/src/sound_cpu.c runtime/src/game.c runtime/src/assets.c runtime/src/assets_media.c runtime/src/assets_midi.c runtime/src/assets_pack.c runtime/src/json.c runtime/src/png.c runtime/src/inflate.c; do
    [[ -f "$f" ]] && SOURCES+=("$f")
done
cc -O2 -std=gnu11 -fno-strict-aliasing -Wno-stringop-overflow "${EXTRA[@]}" -I runtime/include -I src -o "build/dreamcast-host/$TOOL" "${SOURCES[@]}" -lm -pthread
echo "build/dreamcast-host/$TOOL"
