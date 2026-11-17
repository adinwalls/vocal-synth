#!/bin/sh
# Build and install VoxBrew into your home folder. No sudo needed.
set -e
cd "$(dirname "$0")"
make
mkdir -p ~/.lv2 ~/.vst3 ~/.clap
rm -rf ~/.lv2/VoxBrew.lv2 ~/.vst3/VoxBrew.vst3
cp -r bin/VoxBrew.lv2 ~/.lv2/
cp -r bin/VoxBrew.vst3 ~/.vst3/
cp bin/VoxBrew.clap ~/.clap/
echo "Installed VoxBrew (LV2, VST3, CLAP). Rescan plugins in your DAW."
