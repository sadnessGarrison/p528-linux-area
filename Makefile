CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -I./submodules/p528-linux/include -I./src
MATHLIB = -lm
LDFLAGS_SHARED = -shared -Wl,-soname,libp528.so

OBJDIR = build/obj
SRC_LIB = $(wildcard submodules/p528-linux/src/*/*.cpp)
OBJ = $(patsubst submodules/p528-linux/src/%.cpp,$(OBJDIR)/%.o,$(SRC_LIB))
LIB = libp528.so
EXEC = p528-area

SRCS := src/P528LinuxArea.cpp

# Use absolute paths for cleaning to work from any subdirectory
BUILD_DIR = $(abspath build)
LIB_ABS = $(abspath $(LIB))
EXEC_ABS = $(abspath $(EXEC))

.PHONY: all clean clean-obj

all: $(LIB) $(EXEC) clean-obj

$(LIB): $(OBJ)
	$(CXX) $(LDFLAGS_SHARED) -o $@ $(OBJ) $(MATHLIB)

$(EXEC): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ -ldl

$(OBJDIR)/%.o: submodules/p528-linux/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(LIB_ABS) $(EXEC_ABS)

clean-obj:
	rm -rf $(BUILD_DIR)
