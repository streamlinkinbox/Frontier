#=============================================================================================================================================
# 📦 Frontier/BuildConfiguration/LinuxConfiguration.mk — Linux Makefile Directives
#=============================================================================================================================================

CXX := g++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Werror -pedantic -DFRONTIER_DEVELOPMENT -pthread
INCLUDES := -I.

SRCS := \
	DeviceExchange/VulkanExchange.cpp \
	DeviceExchange/ByteSpace.cpp \
	DeviceExchange/TaskScheduler.cpp \
	DeviceExchange/ExecutionQueue.cpp \
	DeviceExchange/VendorClassifier.cpp \
	DeviceExchange/OrientationClassifier.cpp \
	DeviceExchange/WindowExchange.cpp \
	DeviceExchange/InputExchange.cpp \
	DeviceExchange/RenderTargetExchange.cpp \
	DeviceExchange/DiagnosticMetrics.cpp \
	PhysicalDynamics/RigidBodySolver.cpp \
	PhysicalDynamics/DeformableSolver.cpp \
	PhysicalDynamics/LocomotionSolver.cpp \
	PhysicalDynamics/WorldSpace.cpp \
	VolumetricDynamics/LevelSetSpace.cpp \
	VolumetricDynamics/FluidSolver.cpp \
	VolumetricDynamics/ParticleIntegrator.cpp \
	GeometricRaster/GeometryStructure.cpp \
	GeometricRaster/CameraProjection.cpp \
	GeometricRaster/VisibilityProjection.cpp \
	GeometricRaster/RasterSequence.cpp \
	GeometricRaster/MaterialCodec.cpp \
	PhotometricIllumination/ClusteredSpace.cpp \
	PhotometricIllumination/DirectIlluminationIntegrator.cpp \
	PhotometricIllumination/GlobalIlluminationIntegrator.cpp \
	PhotometricIllumination/AtmosphereIntegrator.cpp \
	PlatformInterchange/AcousticStructure.cpp \
	PlatformInterchange/AcousticIntegrator.cpp \
	PlatformInterchange/VoiceExchange.cpp \
	PlatformInterchange/OnlineInterchange.cpp \
	DisplayPresentation/ThemeStructure.cpp \
	DisplayPresentation/VectorCodec.cpp \
	DisplayPresentation/FontCodec.cpp \
	DisplayPresentation/ControlCentrePanel.cpp \
	DisplayPresentation/WorkspacePanel.cpp \
	DisplayPresentation/CycleScheduler.cpp \
	DisplayPresentation/FidelityClassifier.cpp \
	DisplayPresentation/FrontierHost.cpp \
	DisplayPresentation/EngineExecution.cpp

OBJS := $(SRCS:.cpp=.o)
TARGET := bin/FrontierEngine
