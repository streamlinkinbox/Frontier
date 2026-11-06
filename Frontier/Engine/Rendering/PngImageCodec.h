#pragma once
/*==========================================================================================================================================
                                                             PNGIMAGECODEC.H
==========================================================================================================================================*/
// 🧩 Dependency-free lossless RGBA8 PNG writer (RFC 2083 / RFC 1950 stored deflate blocks + Adler-32 + IEEE CRC-32).
//    Used by FluidEditor flipbook baking and headless GPU visual proof captures on Linux, Windows, and macOS.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Frontier
{

namespace PngDetail
{

inline uint32_t ComputeCrc32(const uint8_t* Data, size_t Length) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (size_t Index = 0; Index < Length; ++Index)
    {
        Crc ^= Data[Index];
        for (int Bit = 0; Bit < 8; ++Bit)
            Crc = (Crc >> 1u) ^ (0xEDB88320u & (0u - (Crc & 1u)));
    }
    return ~Crc;
}

inline void AppendBigEndian32(std::vector<uint8_t>& Out, uint32_t Val)
{
    Out.push_back(static_cast<uint8_t>((Val >> 24u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((Val >> 16u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((Val >> 8u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>(Val & 0xFFu));
}

inline void AppendChunk(std::vector<uint8_t>& Out, const char Type[4], const uint8_t* Payload, size_t Length)
{
    AppendBigEndian32(Out, static_cast<uint32_t>(Length));
    const size_t TypeOffset = Out.size();
    Out.push_back(static_cast<uint8_t>(Type[0]));
    Out.push_back(static_cast<uint8_t>(Type[1]));
    Out.push_back(static_cast<uint8_t>(Type[2]));
    Out.push_back(static_cast<uint8_t>(Type[3]));
    if (Payload && Length > 0)
        Out.insert(Out.end(), Payload, Payload + Length);
    const uint32_t Crc = ComputeCrc32(Out.data() + TypeOffset, 4u + Length);
    AppendBigEndian32(Out, Crc);
}

} // namespace PngDetail

inline std::vector<uint8_t> EncodeRgbaPngMemory(const uint8_t* TopDownRgba, int Width, int Height)
{
    std::vector<uint8_t> Png;
    if (!TopDownRgba || Width <= 0 || Height <= 0)
        return Png;

    const size_t RowBytes = 1u + static_cast<size_t>(Width) * 4u;
    std::vector<uint8_t> RawScanlines;
    RawScanlines.reserve(RowBytes * static_cast<size_t>(Height));
    for (int Y = 0; Y < Height; ++Y)
    {
        RawScanlines.push_back(0u); // Filter type 0 (None)
        const uint8_t* Row = TopDownRgba + static_cast<size_t>(Y) * static_cast<size_t>(Width) * 4u;
        RawScanlines.insert(RawScanlines.end(), Row, Row + static_cast<size_t>(Width) * 4u);
    }

    // ZLIB stream with uncompressed stored deflate blocks (max 65535 bytes each)
    std::vector<uint8_t> Zlib;
    Zlib.reserve(RawScanlines.size() + (RawScanlines.size() / 65535u + 2u) * 5u + 6u);
    Zlib.push_back(0x78u);
    Zlib.push_back(0x01u);

    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    size_t Offset   = 0u;
    while (Offset < RawScanlines.size())
    {
        const size_t Remaining = RawScanlines.size() - Offset;
        const uint16_t BlockLen = static_cast<uint16_t>(Remaining > 65535u ? 65535u : Remaining);
        const bool FinalBlock   = (Offset + BlockLen == RawScanlines.size());
        Zlib.push_back(FinalBlock ? 0x01u : 0x00u);
        Zlib.push_back(static_cast<uint8_t>(BlockLen & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((BlockLen >> 8u) & 0xFFu));
        const uint16_t NLen = static_cast<uint16_t>(~BlockLen);
        Zlib.push_back(static_cast<uint8_t>(NLen & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((NLen >> 8u) & 0xFFu));
        Zlib.insert(Zlib.end(), RawScanlines.data() + Offset, RawScanlines.data() + Offset + BlockLen);

        for (size_t Idx = 0; Idx < BlockLen; ++Idx)
        {
            AdlerA = (AdlerA + RawScanlines[Offset + Idx]) % 65521u;
            AdlerB = (AdlerB + AdlerA) % 65521u;
        }
        Offset += BlockLen;
    }
    PngDetail::AppendBigEndian32(Zlib, (AdlerB << 16u) | AdlerA);

    static constexpr uint8_t Signature[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    Png.insert(Png.end(), Signature, Signature + 8);

    std::vector<uint8_t> Ihdr;
    PngDetail::AppendBigEndian32(Ihdr, static_cast<uint32_t>(Width));
    PngDetail::AppendBigEndian32(Ihdr, static_cast<uint32_t>(Height));
    Ihdr.push_back(8u); // 8 bits per channel
    Ihdr.push_back(6u); // Colour type 6 = RGBA
    Ihdr.push_back(0u); // Compression
    Ihdr.push_back(0u); // Filter
    Ihdr.push_back(0u); // Interlace

    PngDetail::AppendChunk(Png, "IHDR", Ihdr.data(), Ihdr.size());
    PngDetail::AppendChunk(Png, "IDAT", Zlib.data(), Zlib.size());
    PngDetail::AppendChunk(Png, "IEND", nullptr, 0u);
    return Png;
}

inline bool WriteRgbaPngFile(const std::string& FilePath, const uint8_t* TopDownRgba, int Width, int Height)
{
    const std::vector<uint8_t> Encoded = EncodeRgbaPngMemory(TopDownRgba, Width, Height);
    if (Encoded.empty())
        return false;
    std::ofstream File(FilePath, std::ios::binary);
    if (!File)
        return false;
    File.write(reinterpret_cast<const char*>(Encoded.data()), static_cast<std::streamsize>(Encoded.size()));
    return static_cast<bool>(File);
}

} // namespace Frontier
