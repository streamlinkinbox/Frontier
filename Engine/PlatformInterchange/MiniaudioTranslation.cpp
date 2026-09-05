//============================================================================================================================================
//                                                    MINIAUDIOTRANSLATION.CPP
//============================================================================================================================================
// 🧩 The single translation unit that compiles miniaudio (ExternalPackages/miniaudio/miniaudio.h, single-header, C++-clean).
//    Twin of ContentInterchange/UfbxTranslation.cpp: kept as a .cpp so both build lists (CMake and ToolchainSequence.ps1)
//    stay C++-only. Nothing else in the engine may define MINIAUDIO_IMPLEMENTATION; AudioExchange.cpp includes the header
//    for declarations only.
//
//    Feature knobs — Phase A uses miniaudio for one thing: a low-level playback device with a realtime data callback.
//    Everything the engine does not need is compiled out so the TU stays small and no hidden threads or decoders exist:
//        MA_NO_ENGINE            high-level engine / sound graph  (the powertrain synth is its own integrator)
//        MA_NO_RESOURCE_MANAGER  async file loading + its worker thread
//        MA_NO_NODE_GRAPH        node graph mixing
//        MA_NO_DECODING          WAV/FLAC/MP3 decoders            (WaveCodec owns file I/O)
//        MA_NO_ENCODING          WAV encoder                      (WaveCodec owns file I/O)
//        MA_NO_GENERATION        waveform / noise generators      (AcousticIntegrator owns synthesis)
//    Drivers stay at miniaudio's defaults (WASAPI → DirectSound → WinMM on Windows, PulseAudio → ALSA → JACK on Linux,
//    Core Audio on macOS) with the null driver as the terminator — AudioExchange selects it explicitly when no device
//    is wanted (sandbox proofs, offline renders).

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include <miniaudio.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif
