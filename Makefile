#============================================================================================================================================
# 📦 Frontier/Makefile — High-Performance Cross-Platform Compilation for Linux
#============================================================================================================================================

include BuildConfiguration/LinuxConfiguration.mk

.PHONY: all clean run vectorlib

all: $(TARGET)

vectorlib: lib/libFrontierVector.a

lib/libFrontierVector.a: DisplayPresentation/VectorCodec.o
	@mkdir -p lib
	ar rcs $@ $^

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJS) bin/FrontierEngine lib/libFrontierVector.a
