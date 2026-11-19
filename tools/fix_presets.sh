#!/bin/sh
# Presets must not change Key or Low Latency, and must not wipe the notes you
# wrote or the melody scan: take those out of the generated LV2 preset file.
set -e
f="$1"
[ -f "$f" ] || exit 0
# DPF ends the list early at every read-only port (detected pitch, active
# voices), which leaves the controls after it outside the preset. Join the
# pieces back into one list first.
perl -0pi -e 's/    \] \.\n\n(    \[\n)/    ] ,\n$1/g; s/    \[\n\s+lv2:symbol "(?:key|low_latency)" ;\n[^\n]+\n    \] ,\n//g; s/    state:state \[\n(?:        <[^\n]*\n)*    \] ;\n\n//g' "$f"
