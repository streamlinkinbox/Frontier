//============================================================================================================================================
//                                                         WAVECODEC.CPP
//============================================================================================================================================
// 🧩 RIFF/WAVE encode + decode (see WaveCodec.h). Byte-wise little-endian serialisation; chunk walk tolerant of padding
//    bytes and unknown chunks; EXTENSIBLE unwrapped by sub-format GUID.

#include "WaveCodec.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                   BYTE HELPERS
//------------------------------------------------------------------------------------------------------------------------

void Put16(std::vector<uint8_t>& B, uint32_t V) noexcept { B.push_back(uint8_t(V)); B.push_back(uint8_t(V >> 8)); }
void Put32(std::vector<uint8_t>& B, uint32_t V) noexcept { Put16(B, V & 0xFFFFu); Put16(B, V >> 16); }
void PutTag(std::vector<uint8_t>& B, const char* Tag) noexcept { for (int I = 0; I < 4; ++I) B.push_back(uint8_t(Tag[I])); }

uint32_t Get16(const uint8_t* P) noexcept { return uint32_t(P[0]) | (uint32_t(P[1]) << 8); }
uint32_t Get32(const uint8_t* P) noexcept { return Get16(P) | (Get16(P + 2) << 16); }
bool     TagIs(const uint8_t* P, const char* Tag) noexcept { return std::memcmp(P, Tag, 4) == 0; }

constexpr uint32_t FormatPcm        = 0x0001u;
constexpr uint32_t FormatIeeeFloat  = 0x0003u;
constexpr uint32_t FormatExtensible = 0xFFFEu;

// Triangular-PDF dither, fixed seed: two encodes of the same clip are byte-identical (proof friendliness).
struct DitherGenerator
{
    uint32_t Word = 0x9E3779B9u;
    float Next() noexcept
    {
        Word = Word * 747796405u + 2891336453u;
        const uint32_t A = ((Word >> ((Word >> 28u) + 4u)) ^ Word) * 277803737u;
        Word = Word * 747796405u + 2891336453u;
        const uint32_t B = ((Word >> ((Word >> 28u) + 4u)) ^ Word) * 277803737u;
        const float Ua = float((A >> 22u) ^ A) * (1.0f / 4294967296.0f);
        const float Ub = float((B >> 22u) ^ B) * (1.0f / 4294967296.0f);
        return Ua - Ub;   // [-1, 1) triangular, 1 LSB peak once scaled by the caller
    }
};

bool Fail(std::string* Error, const char* Reason) noexcept
{
    if (Error) *Error = Reason;
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENCODE
//------------------------------------------------------------------------------------------------------------------------

void WaveCodec::EncodeBytes(const WaveClip& Clip, WaveEncodingCategory Encoding, std::vector<uint8_t>& Bytes) noexcept
{
    const uint32_t Channels   = Clip.ChannelCount ? Clip.ChannelCount : 1u;
    const uint32_t BytesPer   = Encoding == WaveEncodingCategory::Pcm16 ? 2u : Encoding == WaveEncodingCategory::Pcm24 ? 3u : 4u;
    const uint32_t FormatTag  = Encoding == WaveEncodingCategory::Float32 ? FormatIeeeFloat : FormatPcm;
    const uint32_t FrameBytes = Channels * BytesPer;
    const uint64_t Frames     = Clip.Samples.size() / Channels;
    const uint32_t PayloadBytes  = uint32_t(Frames * FrameBytes);
    const uint32_t FmtBytes   = FormatTag == FormatIeeeFloat ? 18u : 16u;   // IEEE float carries cbSize = 0
    const bool     NeedsFact  = FormatTag == FormatIeeeFloat;
    const uint32_t FactBytes  = NeedsFact ? 12u : 0u;

    Bytes.clear();
    Bytes.reserve(12u + 8u + FmtBytes + FactBytes + 8u + PayloadBytes + 1u);

    PutTag(Bytes, "RIFF");
    Put32 (Bytes, 4u + 8u + FmtBytes + FactBytes + 8u + PayloadBytes + (PayloadBytes & 1u));
    PutTag(Bytes, "WAVE");

    PutTag(Bytes, "fmt ");
    Put32 (Bytes, FmtBytes);
    Put16 (Bytes, FormatTag);
    Put16 (Bytes, Channels);
    Put32 (Bytes, Clip.SampleRate);
    Put32 (Bytes, Clip.SampleRate * FrameBytes);
    Put16 (Bytes, FrameBytes);
    Put16 (Bytes, BytesPer * 8u);
    if (FormatTag == FormatIeeeFloat) Put16(Bytes, 0u);

    if (NeedsFact)
    {
        PutTag(Bytes, "fact");
        Put32 (Bytes, 4u);
        Put32 (Bytes, uint32_t(Frames));
    }

    PutTag(Bytes, "data");
    Put32 (Bytes, PayloadBytes);

    DitherGenerator Dither;
    const size_t Count = size_t(Frames) * Channels;
    for (size_t I = 0; I < Count; ++I)
    {
        const float S = Clip.Samples[I];
        switch (Encoding)
        {
            case WaveEncodingCategory::Pcm16:
            {
                float V = S * 32767.0f + Dither.Next();
                V = V > 32767.0f ? 32767.0f : (V < -32768.0f ? -32768.0f : V);
                const int32_t Q = int32_t(std::lrint(V));
                Put16(Bytes, uint32_t(uint16_t(Q)));
                break;
            }
            case WaveEncodingCategory::Pcm24:
            {
                float V = S * 8388607.0f;
                V = V > 8388607.0f ? 8388607.0f : (V < -8388608.0f ? -8388608.0f : V);
                const int32_t Q = int32_t(std::lrint(V));
                const uint32_t U = uint32_t(Q);
                Bytes.push_back(uint8_t(U)); Bytes.push_back(uint8_t(U >> 8)); Bytes.push_back(uint8_t(U >> 16));
                break;
            }
            case WaveEncodingCategory::Float32:
            {
                uint32_t U; std::memcpy(&U, &S, 4);
                Put32(Bytes, U);
                break;
            }
        }
    }
    if (PayloadBytes & 1u) Bytes.push_back(0u);   // RIFF pad byte
}

bool WaveCodec::Encode(std::string_view Path, const WaveClip& Clip, WaveEncodingCategory Encoding, std::string* Error) noexcept
{
    if (Clip.ChannelCount == 0u)                return Fail(Error, "WaveCodec::Encode: clip has zero channels");
    if (Clip.SampleRate == 0u)                  return Fail(Error, "WaveCodec::Encode: clip has zero sample rate");
    if (Clip.Samples.size() % Clip.ChannelCount) return Fail(Error, "WaveCodec::Encode: sample count is not a multiple of the channel count");

    std::vector<uint8_t> Bytes;
    EncodeBytes(Clip, Encoding, Bytes);

    const std::string PathText(Path);
    std::FILE* File = std::fopen(PathText.c_str(), "wb");
    if (!File) return Fail(Error, "WaveCodec::Encode: cannot open the output path for writing");
    const bool Written = std::fwrite(Bytes.data(), 1, Bytes.size(), File) == Bytes.size();
    std::fclose(File);
    return Written ? true : Fail(Error, "WaveCodec::Encode: short write");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DECODE
//------------------------------------------------------------------------------------------------------------------------

bool WaveCodec::DecodeBytes(const uint8_t* Bytes, size_t ByteCount, WaveClip& Clip, std::string* Error) noexcept
{
    if (ByteCount < 12u || !TagIs(Bytes, "RIFF") || !TagIs(Bytes + 8, "WAVE")) return Fail(Error, "WaveCodec::Decode: not a RIFF/WAVE stream");

    uint32_t FormatTag = 0u, Channels = 0u, SampleRate = 0u, BitsPer = 0u, FrameBytes = 0u;
    const uint8_t* Data = nullptr;
    uint32_t       PayloadBytes = 0u;

    size_t Cursor = 12u;
    while (Cursor + 8u <= ByteCount)
    {
        const uint8_t* Chunk = Bytes + Cursor;
        const uint32_t Size  = Get32(Chunk + 4);
        const uint8_t* Body  = Chunk + 8;
        const size_t   Avail = ByteCount - (Cursor + 8u);
        const uint32_t Used  = uint32_t(Size < Avail ? Size : Avail);   // tolerate a truncated final chunk

        if (TagIs(Chunk, "fmt "))
        {
            if (Used < 16u) return Fail(Error, "WaveCodec::Decode: fmt chunk too short");
            FormatTag  = Get16(Body);
            Channels   = Get16(Body + 2);
            SampleRate = Get32(Body + 4);
            FrameBytes = Get16(Body + 12);
            BitsPer    = Get16(Body + 14);
            if (FormatTag == FormatExtensible)
            {
                if (Used < 40u) return Fail(Error, "WaveCodec::Decode: EXTENSIBLE fmt chunk too short");
                FormatTag = Get16(Body + 24);   // first two bytes of the sub-format GUID carry the real tag
            }
        }
        else if (TagIs(Chunk, "data"))
        {
            Data      = Body;
            PayloadBytes = Used;
        }
        Cursor += 8u + size_t(Size) + (Size & 1u);
    }

    if (!Channels || !SampleRate)            return Fail(Error, "WaveCodec::Decode: fmt chunk missing");
    if (!Data)                                return Fail(Error, "WaveCodec::Decode: data chunk missing");
    const uint32_t BytesPer = BitsPer / 8u;
    if (!BytesPer || FrameBytes != BytesPer * Channels) return Fail(Error, "WaveCodec::Decode: inconsistent block alignment");

    const bool IsFloat = FormatTag == FormatIeeeFloat;
    const bool IsPcm   = FormatTag == FormatPcm;
    if (!IsFloat && !IsPcm) return Fail(Error, "WaveCodec::Decode: unsupported format tag (only PCM and IEEE float)");
    if (IsFloat && BytesPer != 4u && BytesPer != 8u) return Fail(Error, "WaveCodec::Decode: float must be 32 or 64 bit");
    if (IsPcm && (BytesPer < 1u || BytesPer > 4u))   return Fail(Error, "WaveCodec::Decode: PCM must be 8, 16, 24 or 32 bit");

    const uint64_t Frames = PayloadBytes / FrameBytes;
    Clip.SampleRate   = SampleRate;
    Clip.ChannelCount = Channels;
    Clip.Samples.resize(size_t(Frames) * Channels);

    const uint8_t* P = Data;
    for (size_t I = 0, N = Clip.Samples.size(); I < N; ++I, P += BytesPer)
    {
        float S;
        if (IsFloat)
        {
            if (BytesPer == 4u) { uint32_t U = Get32(P); std::memcpy(&S, &U, 4); }
            else                { uint64_t U = uint64_t(Get32(P)) | (uint64_t(Get32(P + 4)) << 32); double D; std::memcpy(&D, &U, 8); S = float(D); }
        }
        else switch (BytesPer)
        {
            case 1u:  S = (float(P[0]) - 128.0f) * (1.0f / 128.0f); break;
            case 2u:  S = float(int16_t(Get16(P))) * (1.0f / 32768.0f); break;
            case 3u:  { int32_t V = int32_t((uint32_t(P[0]) << 8) | (uint32_t(P[1]) << 16) | (uint32_t(P[2]) << 24)) >> 8; S = float(V) * (1.0f / 8388608.0f); break; }
            default:  S = float(int32_t(Get32(P))) * (1.0f / 2147483648.0f); break;
        }
        Clip.Samples[I] = S;
    }
    return true;
}

bool WaveCodec::Decode(std::string_view Path, WaveClip& Clip, std::string* Error) noexcept
{
    const std::string PathText(Path);
    std::FILE* File = std::fopen(PathText.c_str(), "rb");
    if (!File) return Fail(Error, "WaveCodec::Decode: cannot open the path for reading");

    std::vector<uint8_t> Bytes;
    uint8_t Chunk[1u << 16];
    size_t  Got;
    while ((Got = std::fread(Chunk, 1, sizeof(Chunk), File)) > 0) Bytes.insert(Bytes.end(), Chunk, Chunk + Got);
    std::fclose(File);

    return DecodeBytes(Bytes.data(), Bytes.size(), Clip, Error);
}

} // namespace Frontier
