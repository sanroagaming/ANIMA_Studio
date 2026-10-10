#!/usr/bin/env bash
set -euo pipefail
ANIMADIR="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -n "${ANIMA_BIN:-}" && -x "$ANIMA_BIN" ]]; then EXE="$ANIMA_BIN"
elif [[ -x "$ANIMADIR/anima" ]]; then EXE="$ANIMADIR/anima"
elif [[ -x "$ANIMADIR/build/anima" ]]; then EXE="$ANIMADIR/build/anima"
else echo "Build Anima03 first, or set ANIMA_BIN to the full path of the anima executable." >&2; exit 2; fi
"$EXE" self-test
"$EXE" check "$ANIMADIR/Examples/fibonacci.ani"
"$EXE" run "$ANIMADIR/Examples/fibonacci.ani"
"$EXE" check "$ANIMADIR/Examples/machine-inspector.ani"
"$EXE" run "$ANIMADIR/Examples/machine-inspector.ani"
"$EXE" check "$ANIMADIR/Examples/fixed-step-simulation.ani"
"$EXE" run "$ANIMADIR/Examples/fixed-step-simulation.ani"
"$EXE" check "$ANIMADIR/Examples/map-iteration.ani"
"$EXE" run "$ANIMADIR/Examples/map-iteration.ani"
"$EXE" check "$ANIMADIR/Examples/enums-and-records.ani"
"$EXE" run "$ANIMADIR/Examples/enums-and-records.ani"
"$EXE" run "$ANIMADIR/Examples/Modules/module-use.ani"
"$EXE" run "$ANIMADIR/Examples/Modules/enum-default-test.ani"
"$EXE" ui-catalog Button
"$EXE" dis "$ANIMADIR/Examples/fibonacci.ani" >/dev/null
"$EXE" test "$ANIMADIR/Tests/conformance-smoke.ani"
"$EXE" scene "$ANIMADIR/Examples/SpecificationTargets/ui-two-scenes.ani"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
for sample in definitions math-and-conditions native-power; do
  "$EXE" migrate "$ANIMADIR/Reference/0.2-examples/$sample.ani" -o "$TMPDIR/$sample.ani"
  "$EXE" run "$TMPDIR/$sample.ani"
  "$EXE" run "$ANIMADIR/Reference/0.2-examples/$sample.ani" --compat 0.2
done
echo "ANIMA 0.3 integrated checker/VM smoke suite passed."
