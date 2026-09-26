// The inspector Field-of-view knob must sit on its fraction: 55 on 20..120.
// The sampler walks the slider column of the Tabs sheet, gathers the bright knobs by
// The sampler walks the slider column of the Tabs sheet, gathers the bright knobs by
//    size (24-pixel discs; the smaller switch knobs fall out), and checks each knob's
//    horizontal middle against the fraction math, top to bottom.
#include <cmath>
#include <cstdio>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct Blob {
    int X0, Y0, X1, Y1;
};

} // namespace

int main(int ArgCount, char** Args) {
    if (ArgCount < 2) {
        std::printf("usage: EditorKnobCheck Sheet.png\n");
        return 1;
    }
    int W = 0, H = 0, N = 0;
    unsigned char* Pixels = stbi_load(Args[1], &W, &H, &N, 4);
    if (Pixels == nullptr) {
        std::printf("cannot open %s\n", Args[1]);
        return 1;
    }
    // Brightness test over the slider column: the tracks run x 1166..1258, and the
    //    orbit/disc rows sit between y 140 and y 460 on the Tabs sheet.
    const int X0 = 1166, X1 = 1260, Y0 = 176, Y1 = 496;   // below the shade strip
    std::vector<unsigned char> Seen((X1 - X0) * (Y1 - Y0), 0);
    auto Bright = [&](int X, int Y) -> bool {
        const unsigned char* P = Pixels + (Y * W + X) * 4;
        return P[0] >= 190 && P[1] >= 190 && P[2] >= 190;
    };
    std::vector<Blob> Blobs;
    std::vector<int> Stack;
    for (int Y = Y0; Y < Y1; ++Y) {
        for (int X = X0; X < X1; ++X) {
            const int Slot = (Y - Y0) * (X1 - X0) + (X - X0);
            if (Seen[Slot] != 0 || !Bright(X, Y)) {
                continue;
            }
            Blob Found{X, Y, X, Y};
            Stack.clear();
            Stack.push_back(Slot);
            Seen[Slot] = 1;
            while (!Stack.empty()) {
                const int At = Stack.back();
                Stack.pop_back();
                const int BX = X0 + (At % (X1 - X0));
                const int BY = Y0 + (At / (X1 - X0));
                if (BX < Found.X0) {
                    Found.X0 = BX;
                }
                if (BX > Found.X1) {
                    Found.X1 = BX;
                }
                if (BY < Found.Y0) {
                    Found.Y0 = BY;
                }
                if (BY > Found.Y1) {
                    Found.Y1 = BY;
                }
                const int Dx[4] = {1, -1, 0, 0};
                const int Dy[4] = {0, 0, 1, -1};
                for (int K = 0; K < 4; ++K) {
                    const int NX = BX + Dx[K];
                    const int NY = BY + Dy[K];
                    if (NX < X0 || NX >= X1 || NY < Y0 || NY >= Y1) {
                        continue;
                    }
                    const int Next = (NY - Y0) * (X1 - X0) + (NX - X0);
                    if (Seen[Next] == 0 && Bright(NX, NY)) {
                        Seen[Next] = 1;
                        Stack.push_back(Next);
                    }
                }
            }
            const int BW = Found.X1 - Found.X0 + 1;
            const int BH = Found.Y1 - Found.Y0 + 1;
            if (BW >= 18 && BW <= 28 && BH >= 18 && BH <= 28) {
                Blobs.push_back(Found);
            }
        }
    }
    stbi_image_free(Pixels);

    for (size_t i = 0; i < Blobs.size(); ++i) {
        for (size_t j = i + 1; j < Blobs.size(); ++j) {
            if (Blobs[j].Y0 < Blobs[i].Y0) {
                Blob Swap = Blobs[i];
                Blobs[i]  = Blobs[j];
                Blobs[j]  = Swap;
            }
        }
    }
    std::printf("knobs found: %d\n", (int)Blobs.size());
    for (const Blob& Found : Blobs) {
        std::printf("  middle %d,%d size %dx%d\n", (Found.X0 + Found.X1) / 2,
            (Found.Y0 + Found.Y1) / 2, Found.X1 - Found.X0 + 1, Found.Y1 - Found.Y0 + 1);
    }
    // The settled travel runs 1205..1232 (track 1193..1244 with the theme's 8-pixel scrollbar seated:
    //    the Camera sheet overflows the shortened column; 12-pixel knob radius); the middle the 0.35
    //    fraction asks for is 1214.
    const int Want[1] = {1214};
    if (Blobs.size() != 1) {
        std::printf("want 1 knob\n");
        return 1;
    }
    for (int i = 0; i < 1; ++i) {
        const int Middle = (Blobs[(size_t)i].X0 + Blobs[(size_t)i].X1) / 2;
        if (std::abs(Middle - Want[i]) > 4) {
            std::printf("knob %d sits at %d, want %d\n", i, Middle, Want[i]);
            return 1;
        }
    }
    std::printf("the knobs sit on their fractions\n");
    return 0;
}
