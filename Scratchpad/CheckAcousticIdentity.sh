#!/usr/bin/env bash
# Gate: the C++ voice (Engine/PlatformInterchange/AcousticIntegrator) is the AudioEditor's JavaScript voice — sample for sample.
#
# The JavaScript renders the reference set below into Scratchpad/Reference/*.f64 (raw float64, git-ignored, ≈ 130 MB), then
#    Scratchpad/AcousticIdentityTest renders the same cars / pulls / seeds / slicing in C++ and holds every sample to ±1e-6
#    (measured ≈ 1e-12: libm's 1-ulp exp/sin differences through the feedback sections) and every counter equal. The .txt
#    echo beside each dump (counters, final smoothed demand, meters) is committed so a reader can see what the bar was met
#    against without regenerating anything.
#
# Reference set: every pull of the LaFerrari (the rev-3 voice: kernels, intake, formants, silencer, pan split), the 918 (rev 2,
#    NA) on the pull and the blips, the GT-R (rev 2, turbo: whine, rush, anti-lag / blow-off, transients) on the pull and the
#    overrun script, the Agera R (loud turbos, 25 Hz wastegate flutter + thump) on the pull and the overrun script, the Demon
#    (supercharger layer: rotor pulsation, bypass, bark) on the pull and the blips, plus a pure-tone LaFerrari pull, a
#    cockpit-listener pull and a 37-frame-slice blip (slice-size handling against the JavaScript, not just against itself).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

command -v node >/dev/null 2>&1 || { echo "[AcousticIdentity] node absent - cannot render the JavaScript reference"; exit 1; }
[ -f ExternalPackages/tomlpp/include/toml++/toml.hpp ] || { echo "[AcousticIdentity] ExternalPackages/tomlpp missing (git submodule update --init -- ExternalPackages/tomlpp)"; exit 1; }

Dump() { node Scratchpad/AcousticEditorRender.js --dump "$@" || { echo "[AcousticIdentity] JavaScript dump failed: $*"; exit 1; }; }
rm -f Scratchpad/Reference/*.f64
for Pull in idle sweep pull steady blip overrun limiter; do Dump FerrariLaFerrari "$Pull" full 64; done
Dump Porsche918Spyder pull full 64
Dump Porsche918Spyder blip full 64
Dump NissanGtrNismo pull full 64
Dump NissanGtrNismo overrun full 64
Dump KoenigseggAgeraR pull full 64
Dump KoenigseggAgeraR overrun full 64
Dump DodgeDemon pull full 64
Dump DodgeDemon blip full 64
Dump FerrariLaFerrari pull 4 64 0x5EED1234 pure
Dump FerrariLaFerrari pull 4 64 0x5EED1234 full cockpit
Dump FerrariLaFerrari blip full 37

g++ -std=c++20 -O2 -Wall -Wextra -I. -IEngine -IExternalPackages/tomlpp/include \
    Scratchpad/AcousticIdentityTest.cpp \
    Engine/PlatformInterchange/AcousticStructure.cpp Engine/PlatformInterchange/AcousticIntegrator.cpp \
    Projects/Project-Dyno/Source/DynoSequence.cpp \
    -o /tmp/AcousticIdentityTest || { echo "[AcousticIdentity] COMPILE FAILED"; exit 1; }
/tmp/AcousticIdentityTest | tee Scratchpad/AcousticIdentityTest.log
S=${PIPESTATUS[0]}
[ $S -eq 0 ] && echo "[AcousticIdentity] OK"
exit $S
