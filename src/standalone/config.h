// iPlug2 build configuration for the RaDiff Voice Processor standalone application.
// (Plug-in formats get their own config in src/plugins/* in Phase 5.)

#define PLUG_NAME "RaDiff Voice Processor"
#define PLUG_MFR "RaDiff"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'RdVp'
#define PLUG_MFR_ID 'RaDf'
#define PLUG_URL_STR "https://github.com/sasquelch/radiff-voice-processor"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 RaDiff. MIT License."
#define PLUG_CLASS_NAME RaDiffVoiceApp

#define BUNDLE_NAME "RaDiffVoice"
#define BUNDLE_MFR "radiff" // iPlug2 derives the bundle id: com.radiff.app.RaDiffVoice (must match Info.plist)
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "RaDiffVoice"

#define PLUG_CHANNEL_IO "1-1 2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1440
#define PLUG_HEIGHT 934
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

// Only used when a plug-in format is built from this config (Phase 5 will have its own).
#define AUV2_ENTRY RaDiffVoice_Entry
#define AUV2_ENTRY_STR "RaDiffVoice_Entry"
#define AUV2_FACTORY RaDiffVoice_Factory
#define AUV2_VIEW_CLASS RaDiffVoice_View
#define AUV2_VIEW_CLASS_STR "RaDiffVoice_View"
#define VST3_SUBCATEGORY "Fx|Dynamics"

// Standalone host (IPlugAPP)
#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
