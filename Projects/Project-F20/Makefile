#=============================================================================================================================================
# 📦 Project-F20/Makefile — High-Performance Incremental Linux Compilation for Project-F20
#=============================================================================================================================================

CXX := g++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Werror -pedantic -DFRONTIER_DEVELOPMENT -pthread
INCLUDES := -I../.. -I.

GAME_SRCS := \
	Source/VehicleSolver.cpp \
	Source/TrackSequence.cpp \
	Source/GameHost.cpp \
	Source/GameExecution.cpp

GAME_OBJS := $(GAME_SRCS:.cpp=.o)

ENGINE_DIR := ../..
ENGINE_OBJS := \
	$(ENGINE_DIR)/DeviceExchange/VulkanExchange.o \
	$(ENGINE_DIR)/DeviceExchange/ByteSpace.o \
	$(ENGINE_DIR)/DeviceExchange/TaskScheduler.o \
	$(ENGINE_DIR)/DeviceExchange/ExecutionQueue.o \
	$(ENGINE_DIR)/DeviceExchange/VendorClassifier.o \
	$(ENGINE_DIR)/DeviceExchange/OrientationClassifier.o \
	$(ENGINE_DIR)/DeviceExchange/WindowExchange.o \
	$(ENGINE_DIR)/DeviceExchange/InputExchange.o \
	$(ENGINE_DIR)/DeviceExchange/RenderTargetExchange.o \
	$(ENGINE_DIR)/DeviceExchange/DiagnosticMetrics.o \
	$(ENGINE_DIR)/PhysicalDynamics/RigidBodySolver.o \
	$(ENGINE_DIR)/PhysicalDynamics/DeformableSolver.o \
	$(ENGINE_DIR)/PhysicalDynamics/LocomotionSolver.o \
	$(ENGINE_DIR)/PhysicalDynamics/WorldSpace.o \
	$(ENGINE_DIR)/VolumetricDynamics/LevelSetSpace.o \
	$(ENGINE_DIR)/VolumetricDynamics/FluidSolver.o \
	$(ENGINE_DIR)/VolumetricDynamics/ParticleIntegrator.o \
	$(ENGINE_DIR)/GeometricRaster/GeometryStructure.o \
	$(ENGINE_DIR)/GeometricRaster/CameraProjection.o \
	$(ENGINE_DIR)/GeometricRaster/VisibilityProjection.o \
	$(ENGINE_DIR)/GeometricRaster/RasterSequence.o \
	$(ENGINE_DIR)/GeometricRaster/MaterialCodec.o \
	$(ENGINE_DIR)/PhotometricIllumination/ClusteredSpace.o \
	$(ENGINE_DIR)/PhotometricIllumination/DirectIlluminationIntegrator.o \
	$(ENGINE_DIR)/PhotometricIllumination/GlobalIlluminationIntegrator.o \
	$(ENGINE_DIR)/PhotometricIllumination/AtmosphereIntegrator.o \
	$(ENGINE_DIR)/PlatformInterchange/AcousticStructure.o \
	$(ENGINE_DIR)/PlatformInterchange/AcousticIntegrator.o \
	$(ENGINE_DIR)/PlatformInterchange/VoiceExchange.o \
	$(ENGINE_DIR)/PlatformInterchange/OnlineInterchange.o \
	$(ENGINE_DIR)/DisplayPresentation/ThemeStructure.o \
	$(ENGINE_DIR)/DisplayPresentation/VectorCodec.o \
	$(ENGINE_DIR)/DisplayPresentation/FontCodec.o \
	$(ENGINE_DIR)/DisplayPresentation/ControlCentrePanel.o \
	$(ENGINE_DIR)/DisplayPresentation/WorkspacePanel.o \
	$(ENGINE_DIR)/DisplayPresentation/CycleScheduler.o \
	$(ENGINE_DIR)/DisplayPresentation/FidelityClassifier.o \
	$(ENGINE_DIR)/DisplayPresentation/FrontierHost.o

TARGET := bin/Project-F20

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(ENGINE_OBJS) $(GAME_OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(ENGINE_DIR)/%.o: $(ENGINE_DIR)/%.cpp
	$(MAKE) -C $(ENGINE_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(GAME_OBJS) bin/Project-F20
