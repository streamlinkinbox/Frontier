// Minimal PNG writer for the headless proof harnesses. Exists because ExternalPackages/stb is an uninitialised
// submodule in some checkouts; where stb IS available the harness can use stbi_write_png instead — the signature
// matches deliberately. Emits 8-bit RGB, deflate "stored" blocks (no compression, exact bytes, no dependencies).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace PngWriteShim {

inline uint32_t Crc32(const unsigned char* Bytes, size_t Count, uint32_t Seed = 0u)
{
    static uint32_t Table[256];
    static bool     Ready = false;
    if (!Ready)
    {
        for (uint32_t I = 0; I < 256; ++I)
        {
            uint32_t C = I;
            for (int K = 0; K < 8; ++K) C = (C & 1u) ? 0xEDB88320u ^ (C >> 1) : (C >> 1);
            Table[I] = C;
        }
        Ready = true;
    }
    uint32_t C = Seed ^ 0xFFFFFFFFu;
    for (size_t I = 0; I < Count; ++I) C = Table[(C ^ Bytes[I]) & 0xFFu] ^ (C >> 8);
    return C ^ 0xFFFFFFFFu;
}

inline void PushBigEndian(std::vector<unsigned char>& Out, uint32_t Value)
{
    Out.push_back(static_cast<unsigned char>(Value >> 24));
    Out.push_back(static_cast<unsigned char>(Value >> 16));
    Out.push_back(static_cast<unsigned char>(Value >> 8));
    Out.push_back(static_cast<unsigned char>(Value));
}

inline void PushChunk(std::vector<unsigned char>& Out, const char Tag[4], const std::vector<unsigned char>& Payload)
{
    PushBigEndian(Out, static_cast<uint32_t>(Payload.size()));
    const size_t Start = Out.size();
    Out.insert(Out.end(), Tag, Tag + 4);
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    PushBigEndian(Out, Crc32(Out.data() + Start, Out.size() - Start));
}

// Channels must be 3 (RGB). Returns 1 on success to match the stb signature.
inline int WritePng(const char* Path, int Width, int Height, int Channels, const void* Pixels, int Stride)
{
    if (Channels != 3 || Width <= 0 || Height <= 0) return 0;
    const unsigned char* Source = static_cast<const unsigned char*>(Pixels);

    // Raw scanlines, each prefixed with filter byte 0.
    std::vector<unsigned char> Raw;
    Raw.reserve(static_cast<size_t>(Height) * (static_cast<size_t>(Width) * 3u + 1u));
    for (int Y = 0; Y < Height; ++Y)
    {
        Raw.push_back(0u);
        Raw.insert(Raw.end(), Source + static_cast<size_t>(Y) * Stride,
                              Source + static_cast<size_t>(Y) * Stride + static_cast<size_t>(Width) * 3u);
    }

    // zlib stream: 0x78 0x01, then deflate stored blocks, then Adler-32.
    std::vector<unsigned char> Stream;
    Stream.push_back(0x78u);
    Stream.push_back(0x01u);
    size_t Offset = 0;
    while (Offset < Raw.size())
    {
        const size_t Block = Raw.size() - Offset < 65535u ? Raw.size() - Offset : 65535u;
        const bool   Last  = Offset + Block >= Raw.size();
        Stream.push_back(Last ? 1u : 0u);
        Stream.push_back(static_cast<unsigned char>(Block & 0xFFu));
        Stream.push_back(static_cast<unsigned char>(Block >> 8));
        Stream.push_back(static_cast<unsigned char>(~Block & 0xFFu));
        Stream.push_back(static_cast<unsigned char>((~Block >> 8) & 0xFFu));
        Stream.insert(Stream.end(), Raw.begin() + Offset, Raw.begin() + Offset + Block);
        Offset += Block;
    }
    uint32_t A = 1u, B = 0u;
    for (unsigned char Byte : Raw) { A = (A + Byte) % 65521u; B = (B + A) % 65521u; }
    PushBigEndian(Stream, (B << 16) | A);

    std::vector<unsigned char> Out{ 0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n' };

    std::vector<unsigned char> Header;
    PushBigEndian(Header, static_cast<uint32_t>(Width));
    PushBigEndian(Header, static_cast<uint32_t>(Height));
    Header.push_back(8u);   // bit depth
    Header.push_back(2u);   // colour type: truecolour
    Header.push_back(0u); Header.push_back(0u); Header.push_back(0u);
    PushChunk(Out, "IHDR", Header);
    PushChunk(Out, "IDAT", Stream);
    PushChunk(Out, "IEND", {});

    std::FILE* File = std::fopen(Path, "wb");
    if (File == nullptr) return 0;
    std::fwrite(Out.data(), 1, Out.size(), File);
    std::fclose(File);
    return 1;
}

} // namespace PngWriteShim

inline int stbi_write_png(const char* Path, int Width, int Height, int Channels, const void* Pixels, int Stride)
{
    return PngWriteShim::WritePng(Path, Width, Height, Channels, Pixels, Stride);
}
