# Project Zero Host harness — compiles the shipped .slang core as C++ and runs it.
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic -Werror
INCLUDES = -I. -I../Integration/Shaders

BIN = SkyViewport CpuPortDiff ReSTIRConvergence MediaProbe FogProbe FogViewport

all: $(BIN)

SkyViewport: SkyViewport.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ SkyViewport.cpp

MediaProbe: MediaProbe.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ MediaProbe.cpp

# NOTE: the vendored spec includes "../../../DeviceExchange/OrientationClassifier.h"
# relative to itself; the shim/a/b include dir makes that resolve to the
# vendored copy under CpuPort/DeviceExchange/.
CpuPortDiff: CpuPortDiff.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h CpuPort/CelestialIntegrator.cpp CpuPort/DeviceExchange/OrientationClassifier.cpp
	mkdir -p CpuPort/shim/a/b
	$(CXX) $(CXXFLAGS) $(INCLUDES) -ICpuPort -ICpuPort/shim/a/b -o $@ CpuPortDiff.cpp CpuPort/CelestialIntegrator.cpp CpuPort/DeviceExchange/OrientationClassifier.cpp

ReSTIRConvergence: ReSTIRConvergence.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang ../Integration/Shaders/ReSTIRSequence.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ReSTIRConvergence.cpp

FogProbe: FogProbe.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang ../Integration/Shaders/FogSpecification.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ FogProbe.cpp

FogViewport: FogViewport.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang ../Integration/Shaders/FogSpecification.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ FogViewport.cpp

clean:
	rm -f $(BIN) *.o

.PHONY: all clean
