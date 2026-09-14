# Project Zero Host harness — compiles the shipped .slang core as C++ and runs it.
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic -Werror
INCLUDES = -I. -I../Shaders

BIN = SkyViewport CpuPortDiff ReSTIRConvergence

all: $(BIN)

SkyViewport: SkyViewport.cpp CelHost.h SlangCompat.h SunPosition.h ../Shaders/CelestialCore.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ SkyViewport.cpp

# NOTE: the vendored spec includes "../../../DeviceExchange/OrientationClassifier.h"
# relative to itself; the up/up/up shim makes that resolve to the vendored copy.
CpuPortDiff: CpuPortDiff.cpp CelHost.h SlangCompat.h SunPosition.h CpuPort/CelestialIntegrator.cpp CpuPort/DeviceExchange/OrientationClassifier.cpp
	mkdir -p CpuPort/shim/a/b
	$(CXX) $(CXXFLAGS) $(INCLUDES) -ICpuPort -ICpuPort/shim/a/b -o $@ CpuPortDiff.cpp CpuPort/CelestialIntegrator.cpp CpuPort/DeviceExchange/OrientationClassifier.cpp

ReSTIRConvergence: ReSTIRConvergence.cpp CelHost.h SlangCompat.h SunPosition.h ../Shaders/CelestialCore.slang ../Shaders/CelestialReSTIR.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ReSTIRConvergence.cpp

clean:
	rm -f $(BIN) *.o

.PHONY: all clean
