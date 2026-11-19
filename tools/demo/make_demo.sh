#!/bin/sh
# Builds the VoxBrew demo video: a vocal through the real engine, with the real
# editor on screen.
#
#   tools/demo/make_demo.sh [dry-vocal.wav] [out.mp4]
#
# Without a vocal it makes a synthetic stand-in singer (tools/demo/make_voice.py,
# needs: pip install numpy pyworld soundfile). A real vocal should be one short
# phrase in A major (about 6 seconds, 48 kHz); it is played once per scene.
# Needs ffmpeg and libcairo2-dev.
set -e
cd "$(dirname "$0")/../.."
VOICE="$1"
OUT="${2:-build/voxbrew-demo.mp4}"
mkdir -p build
if [ -z "$VOICE" ]; then
    VOICE=build/demo_voice.wav
    python3 tools/demo/make_voice.py "$VOICE"
fi
${CXX:-g++} -O2 -std=c++17 $(pkg-config --cflags cairo) -o build/demo_video \
    tools/demo/demo_video.cpp src/dsp/*.cpp $(pkg-config --libs cairo)
./build/demo_video "$VOICE" build/demo_audio.wav |
    ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s 1920x1080 -r 30 -i - \
        -c:v libx264 -pix_fmt yuv420p -crf 18 build/demo_video.mp4
ffmpeg -hide_banner -loglevel error -y -i build/demo_video.mp4 -i build/demo_audio.wav \
    -c:v copy -c:a aac -b:a 256k -movflags +faststart "$OUT"
echo "wrote $OUT"
