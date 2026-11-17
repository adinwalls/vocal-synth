#!/usr/bin/make -f
NAME = VoxBrew
FILES_DSP = VoxBrewPlugin.cpp \
	../../src/dsp/PitchTracker.cpp \
	../../src/dsp/VoiceAnalyzer.cpp \
	../../src/dsp/PsolaVoice.cpp \
	../../src/dsp/Harmonizer.cpp \
	glibc_compat.c
FILES_UI  = VoxBrewUI.cpp glibc_compat.c
UI_TYPE = cairo
DPF_PATH = ../../dpf
DPF_TARGET_DIR = ../../bin
DPF_BUILD_DIR = ../../build/VoxBrew
include ../../dpf/Makefile.plugins.mk
# portable binaries: don't depend on the host's libstdc++ version
LINK_FLAGS += -static-libstdc++ -static-libgcc
BUILD_CXX_FLAGS += -std=gnu++17
TARGETS += lv2 vst3 clap
all: $(TARGETS)
