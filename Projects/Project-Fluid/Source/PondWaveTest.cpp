#include "PondBody.h"
#include "PondWave.h"
#include <cmath>
#include <iostream>

namespace PF = Frontier::ProjectFluid;

int main() {
    PF::PondWave pond;
    if (pond.Columns() != 144 || pond.Rows() != 96) return 1;

    const float quiet = pond.Energy();
    for (int i = 0; i < 120; ++i) pond.Advance(1.0f / 60.0f);
    if (pond.Energy() != quiet) return 2;

    pond.Disturb(0, 0, .65f, .3f);
    const float start = pond.Energy();
    if (!(start > 0)) return 3;
    for (int i = 0; i < 600; ++i) pond.Advance(1.0f / 60.0f);
    const float damped = pond.Energy();
    if (!std::isfinite(damped) || pond.ClampHits() != 0 || damped >= start) return 4;
    if (std::abs(pond.Sample(0, 0)) > .55f) return 5;

    PF::PondBody body;
    body.Reset();
    body.Grab(.2f, .6f);
    for (int i = 0; i < 180; ++i) {
        if (i == 45) body.Release();
        body.Update(pond, 1.0f / 60.0f);
        pond.Advance(1.0f / 60.0f);
    }
    if (body.Wakes() == 0 || !pond.IsWet(body.X(), body.Z(), .48f) || pond.ClampHits() != 0) return 7;

    bool rejected = false;
    try { pond.Step(pond.StableDt() * 1.01f); }
    catch (const std::range_error&) { rejected = true; }
    if (!rejected) return 6;

    std::cout << pond.Columns() << 'x' << pond.Rows() << " pond; dt " << pond.StableDt()
              << "; damping energy " << start << " -> " << damped
              << "; duck wakes " << body.Wakes() << "; 0 clamps\n";
}
