# Project Zero Host harness — compiles the shipped .slang core as C++ and runs it.
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic -Werror
INCLUDES = -I. -I../Integration/Shaders

BIN = SkyViewport CpuPortDiff ReSTIRConvergence MediaProbe FogProbe FogViewport MaterialLevelViewport
# MaterialLevelViewport is the one harness that also needs the engine's interchange headers; it is in `all` because it
#   is the level's visual driver, but it is the only target that needs seated Vulkan headers (see VULKAN_CANDIDATES).

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

# The material library level (M10) rendered by the Project-Zero CPU stack: the engine's own level builder, material
#   records, BSDF text (Engine/Shaders/MaterialEvaluation.slang via the CPU-port shim), sky core and tone map. A Host/
#   harness — never part of the Vulkan TU list — but it consumes the same engine sources the kernel does.
# The engine's SwapchainExchange.h (reached from MaterialSwatchStructure.h) needs the Vulkan headers. Same three
#   candidates the materials gates search: a seated ExternalPackages/, $MATERIAL_SCENES_EXT, or the ~/.cache/m7 mirror
#   (git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers).
VULKAN_CANDIDATES := $(wildcard ExternalPackages) $(MATERIAL_SCENES_EXT) $(HOME)/.cache/m7
VULKAN_ROOT := $(firstword $(foreach C,$(VULKAN_CANDIDATES),$(if $(wildcard $(C)/Vulkan-Headers/include/vulkan/vulkan.h),$(C))))
VKINCLUDE := $(if $(VULKAN_ROOT),-I$(VULKAN_ROOT)/Vulkan-Headers/include,)

#   --gc-sections drops MaterialSwatchStructure::Export (and the SceneCodec/cgltf chain behind it): the viewport reads
#   the level's own triangles and records, it does not write the glTF the GPU imports — that is the app's and the M10
#   gate's job. Same trick the materials gates use to keep their link lines free of the codec dependencies they never call.
# The M9 filter, compiled 1:1 from Engine/Shaders/AtrousDenoise.slang: the viewport's --denoise panel is the shipped
#   à-trous chain, not a lookalike. StageAtrousDenoise.py (the gate's own transform, shared) writes the two halves here.
DENOISE_STAGE := $(CURDIR)/.staged-denoise

$(DENOISE_STAGE)/AtrousDenoise.cpu.1.h: ../../../Engine/Shaders/AtrousDenoise.slang ../../../Exhibits/Workbench/Materials/StageAtrousDenoise.py
	@mkdir -p $(DENOISE_STAGE)
	@cd ../../.. && DO_STAGE=$(DENOISE_STAGE) python3 Exhibits/Workbench/Materials/StageAtrousDenoise.py

MaterialLevelViewport: MaterialLevelViewport.cpp ../../../Engine/ContentInterchange/ShowcaseStructure.cpp ../../../Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp $(DENOISE_STAGE)/AtrousDenoise.cpu.1.h \
		../../../Engine/DisplayPresentation/ShadingTableCodec.cpp \
		../../../Engine/ContentInterchange/MaterialIndex.cpp \
		../../../Engine/ContentInterchange/MaterialSwatchStructure.cpp \
	../../../Engine/ContentInterchange/ShowcaseStructure.cpp \
		../../../Engine/GeometricRaster/CameraProjection.cpp \
		../Source/SkyFogIntegrator.cpp
	@test -n "$(VULKAN_ROOT)" || { echo "[MaterialLevelViewport] Vulkan-Headers not found — clone them first:"; \
	  echo "    git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers \$$HOME/.cache/m7/Vulkan-Headers"; exit 1; }
	$(CXX) -O2 -std=c++20 -Wall -Wextra -Werror -DFRONTIER_CPU_PORT -ffunction-sections -fdata-sections -Wl,--gc-sections $(VKINCLUDE) -I. -I../Shaders -I../Source -I../../../Engine/Shaders \
		-I../../../Engine/DisplayPresentation -I../../../Engine/ContentInterchange -I../../../Engine/DeviceExchange \
		-I../../../Engine/GeometricRaster -I../../../Exhibits/Workbench/Materials -I../../../Exhibits/Workbench/Editor \
		-I$(DENOISE_STAGE) \
		-o $@ MaterialLevelViewport.cpp \
		../../../Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp \
		../../../Engine/DisplayPresentation/ShadingTableCodec.cpp \
		../../../Engine/ContentInterchange/MaterialIndex.cpp \
		../../../Engine/ContentInterchange/MaterialSwatchStructure.cpp \
	../../../Engine/ContentInterchange/ShowcaseStructure.cpp \
		../../../Engine/GeometricRaster/CameraProjection.cpp \
		../../../Engine/DeviceExchange/OrientationClassifier.cpp \
		../../../Engine/GeometricRaster/SceneStructure.cpp \
		../../../Engine/GeometricRaster/GeometryStructure.cpp \
		../../../Engine/SpatialInterface/InterfaceStructure.cpp \
		../../../Engine/SpatialInterface/InterfaceSequence.cpp \
		../../../Engine/SpatialInterface/InterfaceLayoutCodec.cpp \
		../../../Engine/SpatialInterface/InterfaceLightProjection.cpp \
		../../../Engine/SpatialInterface/InterfacePointerProjection.cpp \
		../../../Engine/SpatialInterface/PaletteConfiguration.cpp \
		../../../Engine/DisplayPresentation/MotionIntegrator.cpp \
		../Source/InterfaceTrialSequence.cpp \
		../Source/SkyFogIntegrator.cpp

FogViewport: FogViewport.cpp CelHost.h ../Integration/Shaders/SlangInterchange.h SunPosition.h ../Integration/Shaders/SkySpecification.slang ../Integration/Shaders/FogSpecification.slang
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ FogViewport.cpp

clean:
	rm -f $(BIN) *.o

.PHONY: all clean
# .staged-denoise holds generated headers (StageAtrousDenoise.py output) — never committed.
