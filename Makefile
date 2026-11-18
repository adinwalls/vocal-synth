#!/usr/bin/make -f
PLUGINS = VoxBrew
CXXFLAGS_TOOLS = -O2 -std=c++17 -Wall -Wextra
DSP = src/dsp/PitchTracker.cpp src/dsp/VoiceAnalyzer.cpp src/dsp/PsolaVoice.cpp src/dsp/Harmonizer.cpp

all: plugins gen

plugins:
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p || exit 1; done

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

gen: plugins dpf/utils/lv2_ttl_generator
	@./dpf/utils/generate-ttl.sh bin
	@./tools/fix_presets.sh bin/VoxBrew.lv2/presets.ttl

# Offline engine tests and the render tool (no DPF needed)
build/test_engine: tests/test_engine.cpp $(DSP) src/*.h src/dsp/*.h tools/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS_TOOLS) -o $@ tests/test_engine.cpp $(DSP)

build/voxbrew_render: tools/voxbrew_render.cpp $(DSP) src/dsp/*.h tools/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS_TOOLS) -o $@ tools/voxbrew_render.cpp $(DSP)

test: build/test_engine
	./build/test_engine

render: build/voxbrew_render

# Draw the editor to docs/voxbrew.png without a DAW (needs libcairo2-dev)
build/ui_snapshot: tools/ui_snapshot.cpp $(DSP) src/*.h src/dsp/*.h tools/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS_TOOLS) $$(pkg-config --cflags cairo) -o $@ tools/ui_snapshot.cpp $(DSP) $$(pkg-config --libs cairo)

snapshot: build/ui_snapshot
	./build/ui_snapshot docs/voxbrew.png 1
	./build/ui_snapshot docs/voxbrew-notes.png 1 notes

install: all
	./install.sh

clean:
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p clean; done
	rm -rf bin build dpf/utils/lv2_ttl_generator

.PHONY: all plugins gen test render snapshot install clean
