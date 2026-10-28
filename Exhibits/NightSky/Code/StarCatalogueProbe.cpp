/*==========================================================================================================================================
                                                  STAR CATALOGUE PROBE
==========================================================================================================================================*/
// 🧩 Expose the actual catalogue decoder to an independent astronomy-model comparison.

#include "Engine/Rendering/CelestialModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

int main(int Count, char** Arguments)
{
    if (Count != 4) return 2;
    auto Number = [](const char* Text, double& Out) {
        char* End = nullptr; Out = std::strtod(Text,&End);
        return End != Text && *End == '\0' && std::isfinite(Out);
    };
    double Julian = 0, Latitude = 0, Longitude = 0;
    if (!Number(Arguments[1],Julian) || !Number(Arguments[2],Latitude) || !Number(Arguments[3],Longitude)) return 2;
    const auto Stars = Frontier::DecodeHipparcosCatalogue(std::cin,Julian,Latitude,Longitude);
    for (const auto& Star : Stars)
        std::printf("%.17g %.17g %.17g\n",Star.Direction.X,Star.Direction.Y,Star.Direction.Z);
    return 0;
}
