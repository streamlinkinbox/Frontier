# ============================================================================================================================================
#  Projects/Project-Drive/Makefile — headless CPU references (sandbox stand-in), mirroring Project-Zero's Makefile discipline.
# ============================================================================================================================================
#  The product renderer is the Vulkan + Slang + ImGui stack (built by CMake `linux-app` / the Windows ToolchainSequence, once
#  GameExecution is wired — see README). These two targets are the GPU-free references, buildable with plain g++:
#
#     make telemetry   -> bin/DriveTelemetry    physics -> Diagnostics/{telemetry.csv,timing.log,run.log}
#     make run         -> build both and run them into Diagnostics/
#
#  Explicit file lists only — never glob Source/ (Project-Zero's rot-guard rule).

CXX       := g++
CXXFLAGS  := -std=c++20 -O2 -Wall -Wextra
VEHICLE   := ../../Engine/PhysicalDynamics/Vehicle
BIN       := bin
DIAG      := Diagnostics

VEHICLE_SRCS := \
	$(VEHICLE)/VehicleSolver.cpp \
	$(VEHICLE)/VehicleGeometry.cpp \
	$(VEHICLE)/Aerodynamics.cpp \
	$(VEHICLE)/XPBDSoftTyre.cpp \
	$(VEHICLE)/PacejkaMagicFormula.cpp \
	$(VEHICLE)/TyreSlipDynamics.cpp \
	$(VEHICLE)/Drivetrain.cpp

.PHONY: all telemetry run clean
all: telemetry

telemetry: $(BIN)/DriveTelemetry

$(BIN)/DriveTelemetry: Source/DriveTelemetry.cpp Source/DriveCourse.h $(VEHICLE_SRCS) | $(BIN)
	$(CXX) $(CXXFLAGS) -I$(VEHICLE) -ISource Source/DriveTelemetry.cpp $(VEHICLE_SRCS) -o $@

$(BIN) $(DIAG):
	@mkdir -p $@

run: all | $(DIAG)
	./$(BIN)/DriveTelemetry  $(DIAG)

clean:
	rm -rf $(BIN)
