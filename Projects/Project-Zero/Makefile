# Project-Zero headless CPU reference (sandbox stand-in; the product renderer is the
# Vulkan + Slang stack built by Build/ToolchainSequence.ps1). Explicit file list:
# never glob Source/ — GameExecution.cpp (the Vulkan entry point) lives there.
CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
TARGET   := bin/Project-Zero-CpuReference
OBJDIR   := build/cpu

SRCS := \
	Source/CpuReferenceMain.cpp \
	Source/RendererHost.cpp \
	Source/RayTracingSolver.cpp \
	Source/SkyFogIntegrator.cpp \
	../../Engine/GeometricRaster/CameraProjection.cpp \
	../../Engine/DeviceExchange/OrientationClassifier.cpp \
	../../Engine/DeviceExchange/DiagnosticMetrics.cpp

OBJS := $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.cpp=.o)))

vpath %.cpp Source ../../Engine/GeometricRaster ../../Engine/DeviceExchange

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)
