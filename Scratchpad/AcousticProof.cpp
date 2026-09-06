// Phase A row A2 (P0 / P4) — the measuring side of the C++ port: FFT, order sheet, band levels, spectral centroid, PNG order
//    diagram, WAV ⇄ reference-dump diff. Reads what Project-Dyno --render wrote and what the JavaScript dumped; produces
//    numbers in the log (the comparison) and a PNG (for eyes).
//
//    AcousticProof --self                                   FFT round trip; order reader on a synthetic comb = expected ±0.01 dB
//    AcousticProof --wav <file.wav> --f64 <ref.f64>         per-sample diff after float32 quantisation (bar: ≤ 1 LSB of 24-bit = 1.2e-7)
//    AcousticProof --wav <file.wav> --orders <rpm> --cylinders <N> [--at <seconds>] [--gate] [--png out.png] [--label text]
//                                                           order sheet (½ … 24 re the loudest) of the 0.68 s ending at --at (default: the end) at a held rpm + centroid + band levels;
//                                                           --gate applies the JavaScript proof's [1] bar (firing order N/2 within 20 dB of the loudest — for pure-tone renders)
//    AcousticProof --wav <file.wav> --png out.png --rpm-trace <t0,rpm0;t1,rpm1;…> [--label text]
//                                                           log-frequency spectrogram + order diagram (same layout as the JavaScript proof PNGs)
// Build (from repo root):
//    g++ -std=c++20 -O2 -Wall -Wextra -I. -IEngine Scratchpad/AcousticProof.cpp Engine/PlatformInterchange/WaveCodec.cpp -o /tmp/AcousticProof
//    (Scratchpad/CheckAcousticDyno.sh builds and drives it)

#include "../Engine/PlatformInterchange/WaveCodec.h"
#include "PngWriteShim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr double Pi = 3.141592653589793;

//------------------------------------------------------------------------------------------------------------------------
//                                                   FFT / SPECTRUM
//------------------------------------------------------------------------------------------------------------------------

void Fft(std::vector<double>& Re, std::vector<double>& Im)
{
    const size_t N = Re.size();
    for (size_t I = 1, J = 0; I < N; ++I)
    {
        size_t Bit = N >> 1;
        for (; J & Bit; Bit >>= 1) J ^= Bit;
        J ^= Bit;
        if (I < J) { std::swap(Re[I], Re[J]); std::swap(Im[I], Im[J]); }
    }
    for (size_t Len = 2; Len <= N; Len <<= 1)
    {
        const double Ang = -2.0 * Pi / double(Len), Wr = std::cos(Ang), Wi = std::sin(Ang);
        for (size_t I = 0; I < N; I += Len)
        {
            double Cr = 1.0, Ci = 0.0;
            for (size_t J = 0; J < Len / 2; ++J)
            {
                const double Ur = Re[I + J], Ui = Im[I + J];
                const double Vr = Re[I + J + Len / 2] * Cr - Im[I + J + Len / 2] * Ci, Vi = Re[I + J + Len / 2] * Ci + Im[I + J + Len / 2] * Cr;
                Re[I + J] = Ur + Vr; Im[I + J] = Ui + Vi; Re[I + J + Len / 2] = Ur - Vr; Im[I + J + Len / 2] = Ui - Vi;
                const double T = Cr * Wr - Ci * Wi; Ci = Cr * Wi + Ci * Wr; Cr = T;
            }
        }
    }
}

// Hann-windowed magnitude spectrum in dB (amplitude-normalised: a full-scale sine reads 0 dB), Size/2 bins.
std::vector<double> SpectrumDb(const std::vector<double>& X, size_t Start, size_t Size)
{
    std::vector<double> Re(Size, 0.0), Im(Size, 0.0);
    double WSum = 0.0;
    for (size_t I = 0; I < Size; ++I) { const double W = 0.5 - 0.5 * std::cos(2.0 * Pi * double(I) / double(Size)); Re[I] = (Start + I < X.size() ? X[Start + I] : 0.0) * W; WSum += W; }
    Fft(Re, Im);
    std::vector<double> Out(Size / 2);
    for (size_t K = 0; K < Size / 2; ++K) { const double Mag = 2.0 * std::sqrt(Re[K] * Re[K] + Im[K] * Im[K]) / WSum; Out[K] = 20.0 * std::log10(Mag + 1.0e-12); }
    return Out;
}

// Hann window main lobe at an offset of X bins (normalised: W(0) = 1, W(±1) = ½).
double HannLobe(double X)
{
    const double Ax = std::fabs(X);
    if (Ax < 1.0e-9) return 1.0;
    if (std::fabs(Ax - 1.0) < 1.0e-9) return 0.5;
    return std::sin(Pi * X) / (Pi * X * (1.0 - X * X));
}

// Level of order O at F0 [Hz]: the loudest bin within ±0.25 orders, with the tone's true offset and amplitude recovered
//    from the two neighbouring bins through the Hann lobe shape (the ratio of the neighbours is monotonic in the offset, so a
//    bisection inverts it exactly for one tone) — no scalloping loss, so a held-rpm order reads the same whichever bin it
//    lands between. The JavaScript proof reads the raw peak bin; comparisons between the two use this reader on both signals.
double OrderDb(const std::vector<double>& Db, double Order, double F0, double Rate, size_t Size)
{
    const double Hz = Order * F0, Half = 0.25 * F0;
    const long Lo = std::max(1L, long(std::floor((Hz - Half) * double(Size) / Rate))), Hi = std::min(long(Db.size()) - 2L, long(std::ceil((Hz + Half) * double(Size) / Rate)));
    double Best = -200.0; long At = Lo;
    for (long K = Lo; K <= Hi; ++K) if (Db[size_t(K)] > Best) { Best = Db[size_t(K)]; At = K; }
    const double MA = std::pow(10.0, Db[size_t(At - 1)] / 20.0), MB = std::pow(10.0, Best / 20.0), MC = std::pow(10.0, Db[size_t(At + 1)] / 20.0);
    if (MA <= 0.0 || MC <= 0.0) return Best;
    const double Ratio = MC / MA;                       // = W(δ − 1) / W(δ + 1) for a tone δ bins above the peak bin
    double LoD = -0.5, HiD = 0.5;
    for (int I = 0; I < 60; ++I) { const double Mid = 0.5 * (LoD + HiD); const double R = HannLobe(Mid - 1.0) / HannLobe(Mid + 1.0); if (R < Ratio) LoD = Mid; else HiD = Mid; }
    const double Delta = 0.5 * (LoD + HiD);
    return 20.0 * std::log10(MB / HannLobe(Delta));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PNG (heat ramp, same palette as the JavaScript proof)
//------------------------------------------------------------------------------------------------------------------------

void Heat(double T, unsigned char* Rgb)
{
    T = std::clamp(T, 0.0, 1.0);
    const double R = std::clamp(1.6 * T - 0.1, 0.0, 1.0), G = std::clamp(2.2 * T - 1.1, 0.0, 1.0), B = T < 0.5 ? 0.15 + 1.2 * T : std::max(0.0, 2.0 - 2.4 * T);
    Rgb[0] = (unsigned char)std::lround(255.0 * R); Rgb[1] = (unsigned char)std::lround(255.0 * G); Rgb[2] = (unsigned char)std::lround(255.0 * B);
}

struct RpmPoint { double T; double Rpm; };

void WriteDiagram(const std::string& Path, const std::vector<double>& Mono, double Rate, const std::vector<RpmPoint>& Trace, uint32_t Cylinders, const std::string& Label)
{
    const int W = 900, HS = 260, HO = 200, PAD = 8, H = HS + HO + 3 * PAD;
    const size_t Size = 4096; const double FLo = 30.0, FHi = 12000.0, MaxOrder = 24.0;
    std::vector<unsigned char> Ink(size_t(W) * size_t(H) * 3u, 18u);
    auto Put = [&](int X, int Y, const unsigned char* C) { if (X < 0 || Y < 0 || X >= W || Y >= H) return; std::memcpy(&Ink[(size_t(Y) * size_t(W) + size_t(X)) * 3u], C, 3u); };
    const unsigned char White[3] = { 255, 255, 255 }, Blue[3] = { 80, 160, 255 }, Grey[3] = { 140, 140, 140 };
    auto RpmAt = [&](double T)   // linear between trace points (keyframes), held outside them
    {
        if (Trace.empty()) return 1000.0;
        if (T <= Trace.front().T) return Trace.front().Rpm;
        for (size_t I = 1; I < Trace.size(); ++I)
            if (T < Trace[I].T) { const double Span = Trace[I].T - Trace[I - 1].T; return Span > 0.0 ? Trace[I - 1].Rpm + (Trace[I].Rpm - Trace[I - 1].Rpm) * (T - Trace[I - 1].T) / Span : Trace[I].Rpm; }
        return Trace.back().Rpm;
    };
    const size_t Hop = Mono.size() > Size ? (Mono.size() - Size) / size_t(W) : 1u;
    for (int X = 0; X < W; ++X)
    {
        const size_t Start = std::min(Mono.size() > Size ? Mono.size() - Size : 0u, size_t(X) * Hop);
        const std::vector<double> Db = SpectrumDb(Mono, Start, Size);
        for (int Y = 0; Y < HS; ++Y)
        {
            const double F = FLo * std::pow(FHi / FLo, 1.0 - double(Y) / HS);
            const size_t K = std::min(Size / 2 - 1, size_t(std::max(0L, std::lround(F * double(Size) / Rate))));
            unsigned char C[3]; Heat((Db[K] + 100.0) / 100.0, C); Put(X, PAD + Y, C);
        }
        const double Rpm = RpmAt(double(Start) / Rate), F0 = Rpm / 60.0;
        for (int Y = 0; Y < HO; ++Y)
        {
            const double Order = MaxOrder * (1.0 - double(Y) / HO), F = Order * F0;
            const long K = std::lround(F * double(Size) / Rate);
            if (K >= 2 && K < long(Size / 2) - 2) { double M = -200.0; for (long J = K - 2; J <= K + 2; ++J) M = std::max(M, Db[size_t(J)]); unsigned char C[3]; Heat((M + 90.0) / 90.0, C); Put(X, PAD * 2 + HS + Y, C); }
        }
        const double Fd = Rpm / 60.0 * double(Cylinders) / 2.0;
        Put(X, PAD + int(std::lround(HS * (1.0 - std::log(Fd / FLo) / std::log(FHi / FLo)))), White);
    }
    for (int O = 1; O <= int(MaxOrder); ++O) { const int Y = PAD * 2 + HS + int(std::lround(HO * (1.0 - O / MaxOrder))); const int Len = O == int(Cylinders / 2u) ? 40 : (O % 2 == 0 ? 14 : 7); for (int X = 0; X < Len; ++X) Put(X, Y, O == int(Cylinders / 2u) ? Blue : White); }
    for (double F : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) { const int Y = PAD + int(std::lround(HS * (1.0 - std::log(F / FLo) / std::log(FHi / FLo)))); for (int X = 0; X < 10; ++X) Put(X, Y, Grey); }
    PngWriteShim::WritePng(Path.c_str(), W, H, 3, Ink.data(), W * 3);
    std::printf("  wrote %s  (%s)\n", Path.c_str(), Label.c_str());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ARGUMENTS
//------------------------------------------------------------------------------------------------------------------------

const char* Arg(int argc, char** argv, const char* Name, const char* Fallback = nullptr)
{
    for (int I = 1; I + 1 < argc; ++I) if (std::strcmp(argv[I], Name) == 0) return argv[I + 1];
    return Fallback;
}
bool Has(int argc, char** argv, const char* Name) { for (int I = 1; I < argc; ++I) if (std::strcmp(argv[I], Name) == 0) return true; return false; }

int Passed = 0, Failed = 0;
void Check(bool Ok, const std::string& Text) { (Ok ? Passed : Failed)++; std::printf("  %s  %s\n", Ok ? "PASS" : "FAIL", Text.c_str()); }

} // namespace

int main(int argc, char** argv)
{
    using namespace Frontier;
    const double Rate = 48000.0;

    if (Has(argc, argv, "--self"))
    {
        std::printf("AcousticProof --self\n");
        // FFT round trip: two tones on bin centres (1 000 Hz is not one at 4 096 / 48 kHz — 984.375 Hz is), absolute dBFS
        const size_t Size = 4096;
        const double FA = 21.0 * Rate / Size, FB = 64.0 * Rate / Size;
        std::vector<double> X(Size);
        for (size_t I = 0; I < Size; ++I) X[I] = 0.5 * std::sin(2.0 * Pi * FA * double(I) / Rate) + 0.1 * std::sin(2.0 * Pi * FB * double(I) / Rate);
        std::vector<double> Db = SpectrumDb(X, 0, Size);
        const double B1 = Db[21], B3 = Db[64];
        char Line[256];
        std::snprintf(Line, sizeof(Line), "FFT: %.3f Hz at 0.5 → %.4f dBFS (want −6.0206 ± 0.001), %.0f Hz at 0.1 → %.4f dBFS (want −20.0 ± 0.001)", FA, B1, FB, B3);
        Check(std::fabs(B1 + 6.0206) < 0.001 && std::fabs(B3 + 20.0) < 0.001, Line);
        // order reader on a synthetic comb: orders 1 … 12 with known amplitudes — once on bin centres (f0 = 46.875 Hz, exact),
        //    once at 50 Hz (3 000 rpm, off-centre by up to half a bin: what the interpolation is for)
        const double Amp[12] = { 0.05, 0.1, 0.02, 0.2, 0.03, 0.4, 0.02, 0.15, 0.03, 0.1, 0.02, 0.2 };
        const size_t CombSize = 32768;   // 1.46 Hz bins: the orders sit ≥ 32 bins apart, where the Hann side lobes are below −80 dB
        for (const double F0 : { 32.0 * Rate / CombSize, 50.0 })
        {
            std::vector<double> Comb(size_t(Rate * 1.0));
            for (size_t I = 0; I < Comb.size(); ++I) { double V = 0.0; for (int O = 1; O <= 12; ++O) V += Amp[O - 1] * std::sin(2.0 * Pi * F0 * O * double(I) / Rate + 0.37 * O); Comb[I] = V; }
            Db = SpectrumDb(Comb, Comb.size() - CombSize, CombSize);
            double WorstErr = 0.0;
            for (int O = 1; O <= 12; ++O) { const double Want = 20.0 * std::log10(Amp[O - 1] / 0.4), Got = OrderDb(Db, O, F0, Rate, CombSize) - OrderDb(Db, 6.0, F0, Rate, CombSize); WorstErr = std::max(WorstErr, std::fabs(Got - Want)); }
            const bool Centred = F0 != 50.0;
            std::snprintf(Line, sizeof(Line), "order reader on a 12-order synthetic comb, f0 = %.3f Hz (%s): worst error %.4f dB re order 6 (bar ±0.01)", F0, Centred ? "bin-centred" : "3 000 rpm, off-centre", WorstErr);
            Check(WorstErr <= 0.01, Line);
        }
        std::printf("%s — %d pass, %d fail\n", Failed == 0 ? "ALL PASS" : "FAILURES", Passed, Failed);
        return Failed == 0 ? 0 : 1;
    }

    const char* WavPath = Arg(argc, argv, "--wav");
    if (!WavPath) { std::fprintf(stderr, "usage: see the header of Scratchpad/AcousticProof.cpp\n"); return 2; }
    WaveClip Clip; std::string Error;
    if (!WaveCodec::Decode(WavPath, Clip, &Error)) { std::fprintf(stderr, "cannot read %s: %s\n", WavPath, Error.c_str()); return 2; }
    const std::string Label = Arg(argc, argv, "--label", WavPath);
    const uint64_t Frames = Clip.FrameCount();

    if (const char* F64 = Arg(argc, argv, "--f64"))
    {
        std::ifstream F(F64, std::ios::binary); std::vector<double> Ref(size_t(Frames) * Clip.ChannelCount);
        F.read(reinterpret_cast<char*>(Ref.data()), std::streamsize(Ref.size() * sizeof(double)));
        const bool SizeOk = size_t(F.gcount()) == Ref.size() * sizeof(double);
        double Worst = 0.0; size_t At = 0u;
        for (size_t I = 0; I < Ref.size(); ++I) { const double D = std::fabs(double(Clip.Samples[I]) - Ref[I]); if (D > Worst) { Worst = D; At = I; } }
        char Line[512];
        std::snprintf(Line, sizeof(Line), "%s vs %s: %llu frames, max |Δ| %.3e at frame %zu ch %zu (bar 1.2e-7 = float32 rounding of |x| ≤ 1, > 1 LSB of 24-bit)", WavPath, F64, (unsigned long long)Frames, Worst, At / Clip.ChannelCount, At % Clip.ChannelCount);
        Check(SizeOk && Worst <= 1.2e-7, Line);
    }

    std::vector<double> Left(static_cast<size_t>(Frames), 0.0); for (size_t I = 0; I < Left.size(); ++I) Left[I] = Clip.Samples[I * Clip.ChannelCount];

    if (const char* RpmText = Arg(argc, argv, "--orders"))
    {
        const double Rpm = std::atof(RpmText), F0 = Rpm / 60.0;
        const uint32_t N = uint32_t(std::atoi(Arg(argc, argv, "--cylinders", "8")));
        const size_t Size = 32768;
        const double At = std::atof(Arg(argc, argv, "--at", "0"));
        const size_t End = At > 0.0 ? std::min(Left.size(), size_t(At * Rate)) : Left.size();
        const size_t Start = End > Size ? End - Size : 0u;
        const std::vector<double> Db = SpectrumDb(Left, Start, Size);
        double BestDb = -999.0, BestOrder = 0.0;
        for (double O = 0.5; O <= 24.0; O += 0.5) { const double M = OrderDb(Db, O, F0, Rate, Size); if (M > BestDb) { BestDb = M; BestOrder = O; } }
        std::printf("  %s @ %.0f rpm (left, %.2f–%.2f s): loudest order %.1f at %.1f dBFS; re it:", Label.c_str(), Rpm, Start / Rate, End / Rate, BestOrder, BestDb);
        for (double O : { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 12.0, 18.0, 24.0 }) std::printf(" %g:%+.1f", O, OrderDb(Db, O, F0, Rate, Size) - BestDb);
        double Num = 0.0, Den = 0.0; for (size_t K = 2; K < size_t(8000.0 * Size / Rate); ++K) { const double P = std::pow(10.0, Db[K] / 10.0); Num += P * double(K) * Rate / Size; Den += P; }
        double Above300 = -999.0, Top = -999.0; for (size_t K = 2; K < Size / 2; ++K) { Top = std::max(Top, Db[K]); if (K >= size_t(300.0 * Size / Rate)) Above300 = std::max(Above300, Db[K]); }
        std::printf("\n    centroid < 8 kHz %.0f Hz · above 300 Hz %+.1f dB re the loudest line · N/2 = %u\n", Num / std::max(1.0e-12, Den), Above300 - Top, N / 2u);
        // the JavaScript proof's [1] bar (Scratchpad/AcousticEditorRender.js): the firing order N/2 within 20 dB of the loudest order
        //    in pure-tone mode at a held rpm (at redline the LaFerrari's blowdown brings orders 9 / 12 up — the "silky-dense" F140
        //    series — so N/2 need not be the loudest; at idle it is)
        const double FiringDb = OrderDb(Db, double(N / 2u), F0, Rate, Size) - BestDb;
        char OrderLine[256];
        std::snprintf(OrderLine, sizeof(OrderLine), "%s: firing order N/2 = %u at %+.1f dB re the loudest order (%.1f) — within 20 dB", Label.c_str(), N / 2u, FiringDb, BestOrder);
        if (Has(argc, argv, "--gate")) Check(FiringDb >= -20.0, OrderLine);
    }

    if (const char* Png = Arg(argc, argv, "--png"))
    {
        std::vector<RpmPoint> Trace;
        if (const char* T = Arg(argc, argv, "--rpm-trace"))
        {
            std::string S(T); size_t Pos = 0;
            while (Pos < S.size()) { const size_t Semi = S.find(';', Pos); const std::string Pair = S.substr(Pos, Semi == std::string::npos ? std::string::npos : Semi - Pos); const size_t Comma = Pair.find(','); if (Comma != std::string::npos) Trace.push_back({ std::atof(Pair.c_str()), std::atof(Pair.c_str() + Comma + 1) }); if (Semi == std::string::npos) break; Pos = Semi + 1; }
        }
        else if (const char* R = Arg(argc, argv, "--orders")) Trace.push_back({ 0.0, std::atof(R) });
        WriteDiagram(Png, Left, Rate, Trace, uint32_t(std::atoi(Arg(argc, argv, "--cylinders", "8"))), Label);
    }
    if (Passed + Failed) std::printf("%s — %d pass, %d fail\n", Failed == 0 ? "ALL PASS" : "FAILURES", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
