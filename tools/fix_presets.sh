#!/bin/sh
# Presets must not change Key or Low Latency, and must not wipe the notes you
# wrote or the melody scan: take those out of the generated LV2 preset file.
# (It also removes a stray block DPF writes after the output port, which would
# otherwise make the file invalid.)
set -e
f="$1"
[ -f "$f" ] || exit 0
perl -0pi -e 's/    \[\n\s+lv2:symbol "key" ;\n[^\n]+\n    \] ,\n//g; s/\n    \[\n\s+lv2:symbol "low_latency" ;\n[^\n]+\n    \] \.\n//g; s/    state:state \[\n(?:        <[^\n]*\n)*    \] ;\n\n//g' "$f"
