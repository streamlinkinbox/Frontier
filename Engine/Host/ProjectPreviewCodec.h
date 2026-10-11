//============================================================================================================================================
//                                                           PROJECTPREVIEWCODEC.H
//============================================================================================================================================
// 📦 Discovers a project-owned image and translates bounded Windows image pixels into an aspect-preserving thumbnail.

#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{
inline bool FindProjectPreview(const std::filesystem::path &Project, std::filesystem::path &Image, std::string &Refusal)
{
    Image.clear();
    Refusal.clear();
    std::error_code                     Error;
    const auto                          Location = Project.parent_path() / "Preview";
    std::filesystem::directory_iterator Walk(Location, std::filesystem::directory_options::skip_permission_denied, Error), End;
    if (Error)
    {
        Refusal = "Place a PNG, JPEG, BMP, GIF or TIFF in this project's Preview folder, then Scan.";
        return false;
    }
    std::vector<std::filesystem::path> Images;
    unsigned                           Count = 0;
    for (; !Error && Walk != End; Walk.increment(Error))
    {
        if (++Count > 256)
        {
            Refusal = "Preview folder exceeds 256 entries. Keep only the images you want to use.";
            return false;
        }
        if (!Walk->is_regular_file(Error))
            continue;
        auto Extension = Walk->path().extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        if (Extension == ".png" || Extension == ".jpg" || Extension == ".jpeg" || Extension == ".bmp" || Extension == ".gif" ||
            Extension == ".tif" || Extension == ".tiff")
            Images.push_back(Walk->path());
    }
    // 📝 Any user image at the top level overrides the bundled picture without a required filename.
    const auto DefaultImage = Location / "Default/Project.png";
    if (!Error && Images.empty() && std::filesystem::is_regular_file(DefaultImage, Error))
        Images.push_back(DefaultImage);
    if (Error || Images.empty())
    {
        Refusal = "No readable supported image in Preview. Add PNG, JPEG, BMP, GIF or TIFF, then Scan.";
        return false;
    }
    std::sort(Images.begin(), Images.end());
    Image = Images.front();
    return true;
}
} // namespace Frontier

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace Frontier
{
struct ProjectPreviewPixels
{
    unsigned                  Width = 0, Height = 0;
    std::vector<std::uint8_t> Pixels;
    std::string               Refusal;
};

inline bool DecodeProjectPreview(const std::filesystem::path &Image, ProjectPreviewPixels &Delivered)
{
    Delivered = {};
    std::error_code Error;
    const auto      Bytes = std::filesystem::file_size(Image, Error);
    if (Error || Bytes == 0 || Bytes > 32u * 1024u * 1024u)
    {
        Delivered.Refusal = "Preview must be a readable image no larger than 32 MiB.";
        return false;
    }
    Microsoft::WRL::ComPtr<IWICImagingFactory>    Factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder>     Decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> DecodedImage;
    Microsoft::WRL::ComPtr<IWICBitmapScaler>      Scaler;
    Microsoft::WRL::ComPtr<IWICFormatConverter>   Converter;
    unsigned                                      Width = 0, Height = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Factory.GetAddressOf()))) ||
        FAILED(Factory->CreateDecoderFromFilename(Image.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  Decoder.GetAddressOf())) ||
        FAILED(Decoder->GetFrame(0, DecodedImage.GetAddressOf())) || FAILED(DecodedImage->GetSize(&Width, &Height)))
    {
        Delivered.Refusal = "Preview image could not be decoded. Replace it with a valid image, then Scan.";
        return false;
    }
    if (Width == 0 || Height == 0 || Width > 32768 || Height > 32768 || std::uint64_t(Width) * Height > 64000000)
    {
        Delivered.Refusal = "Preview exceeds 64 million pixels or 32768 pixels on one axis.";
        return false;
    }
    const double   Ratio         = std::min({1.0, 512.0 / Width, 288.0 / Height});
    const unsigned ReducedWidth  = std::max(1u, static_cast<unsigned>(Width * Ratio));
    const unsigned ReducedHeight = std::max(1u, static_cast<unsigned>(Height * Ratio));
    if (FAILED(Factory->CreateBitmapScaler(Scaler.GetAddressOf())) ||
        FAILED(Scaler->Initialize(DecodedImage.Get(), ReducedWidth, ReducedHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(Factory->CreateFormatConverter(Converter.GetAddressOf())) ||
        FAILED(Converter->Initialize(Scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
    {
        Delivered.Refusal = "Preview could not be resized or converted to RGBA.";
        return false;
    }
    Delivered.Pixels.resize(ReducedWidth * ReducedHeight * 4u);
    if (FAILED(Converter->CopyPixels(nullptr, ReducedWidth * 4u, static_cast<UINT>(Delivered.Pixels.size()), Delivered.Pixels.data())))
    {
        Delivered.Pixels.clear();
        Delivered.Refusal = "Preview pixel decoding failed. Replace the image, then Scan.";
        return false;
    }
    Delivered.Width  = ReducedWidth;
    Delivered.Height = ReducedHeight;
    return true;
}
} // namespace Frontier
#endif
