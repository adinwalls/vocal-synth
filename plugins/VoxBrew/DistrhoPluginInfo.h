#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND    "VoxBrew"
#define DISTRHO_PLUGIN_NAME     "VoxBrew Harmonizer"
#define DISTRHO_PLUGIN_URI      "urn:voxbrew:harmonizer"
#define DISTRHO_PLUGIN_CLAP_ID  "com.voxbrew.harmonizer"

#define DISTRHO_PLUGIN_BRAND_ID  VxBr
#define DISTRHO_PLUGIN_UNIQUE_ID VbHm

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_UI_USE_CAIRO           1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       912
#define DISTRHO_UI_DEFAULT_HEIGHT      540
// the editor reads live voice pitches straight from the plugin (same process)
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        0
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:PitchPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Pitch Shift|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "pitch-shifter", "stereo"

#endif
