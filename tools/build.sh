#!/usr/bin/env bash
# Build both Ghost firmwares and copy the merged images into web/firmware/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-$HOME/tools/arduino-cli}"
CTAGS="${CTAGS_DIR:-$HOME/tools/fakectags}"
FQBN="esp32:esp32:esp32:PartitionScheme=huge_app"
python3 "$ROOT/tools/embed_page.py" "$ROOT/firmware/ghost_listener/page.html" "$ROOT/firmware/ghost_listener/page.h"
for fw in sender listener; do
  out="$ROOT/build/$fw"
  mkdir -p "$out" "$ROOT/web/firmware/$fw"
  "$CLI" compile --fqbn "$FQBN" --libraries "$ROOT/firmware/libraries" \
    --build-property "runtime.tools.ctags.path=$CTAGS" \
    --output-dir "$out" "$ROOT/firmware/ghost_$fw" 2>&1 | grep -vi discovery | grep -E "error|warning: unused|Sketch uses|Global variables" || true
  test -f "$out/ghost_$fw.ino.merged.bin"
  cp "$out/ghost_$fw.ino.merged.bin" "$ROOT/web/firmware/$fw/ghost-$fw.bin"
  echo "built $fw: $(stat -c%s "$ROOT/web/firmware/$fw/ghost-$fw.bin") bytes"
done

# Trim the 0xFF padding at the end of each merged image so the web flasher downloads less.
for f in web/firmware/*/ghost-*.bin; do
  python3 - "$f" <<'PY'
import sys
p = sys.argv[1]
d = open(p, 'rb').read()
n = len(d.rstrip(b'\xff'))
n = (n + 4095) // 4096 * 4096
open(p, 'wb').write(d[:n])
print(p, n, 'bytes')
PY
done
