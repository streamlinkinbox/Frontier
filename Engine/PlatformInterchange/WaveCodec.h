//============================================================================================================================================
//                                                          WAVECODEC.H
//============================================================================================================================================
// 🧩 RIFF/WAVE encoder + decoder for interleaved float audio. Two jobs in Phase A: AudioExchange::RenderToWave writes the
//    offline dyno renders the AudioEditor visualises, and the Scratchpad harnesses read them back (and later the reference
//    recordings) for spectrum / order-diagram proofs.
//
//    Encode: PCM16 (default, smallest, every tool opens it), PCM24, or IEEE float32 (WAVE_FORMAT_IEEE_FLOAT, exact for
//    bit-identity proofs). Decode: PCM8/16/24/32 and float32/64, any channel count, RIFF chunks skipped by size (LIST,
//    fact, cue, bext …), WAVE_FORMAT_EXTENSIBLE unwrapped via the sub-format GUID. Everything little-endian per the RIFF
//    specification; written byte-wise so host endianness never matters. No allocations beyond the caller's vector.
//
//    Failure is returned, never thrown: false + a one-line reason in *Error (nullptr allowed).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SAMPLE ENCODING
//------------------------------------------------------------------------------------------------------------------------

enum class WaveEncodingCategory : uint32_t
{
    Pcm16   = 0,      // 16-bit signed integer, dithered from float with TPDF noise (1 LSB peak) — the default
    Pcm24   = 1,      // 24-bit signed integer, packed 3 bytes per sample
    Float32 = 2       // IEEE-754 single, exact round trip
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WAVE CLIP
//------------------------------------------------------------------------------------------------------------------------

// A decoded (or about-to-be-encoded) clip: interleaved frames, float in [-1, 1] nominal (decode never clamps).
struct WaveClip
{
    uint32_t           SampleRate   = 48000u;   // [Hz]
    uint32_t           ChannelCount = 2u;       // [-]
    std::vector<float> Samples;                 // [-]  interleaved, FrameCount × ChannelCount

    [[nodiscard]] uint64_t FrameCount()      const noexcept { return ChannelCount ? Samples.size() / ChannelCount : 0u; }
    [[nodiscard]] double   DurationSeconds() const noexcept { return SampleRate ? double(FrameCount()) / double(SampleRate) : 0.0; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WAVE CODEC
//------------------------------------------------------------------------------------------------------------------------

class WaveCodec
{
public:
    // Writes Clip to Path. Pcm16 dither uses a fixed-seed TPDF sequence so two encodes of the same clip are byte-identical.
    static bool Encode(std::string_view Path, const WaveClip& Clip, WaveEncodingCategory Encoding = WaveEncodingCategory::Pcm16,
                       std::string* Error = nullptr) noexcept;

    // Reads Path into Clip (replacing its contents). Integer formats are scaled to [-1, 1) by their full-scale value.
    static bool Decode(std::string_view Path, WaveClip& Clip, std::string* Error = nullptr) noexcept;

    // In-memory variants (the file variants wrap these) — used by the harnesses to avoid touching disk.
    static void EncodeBytes(const WaveClip& Clip, WaveEncodingCategory Encoding, std::vector<uint8_t>& Bytes) noexcept;
    static bool DecodeBytes(const uint8_t* Bytes, size_t ByteCount, WaveClip& Clip, std::string* Error = nullptr) noexcept;
};

} // namespace Frontier
