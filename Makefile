CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -I./submodules/p528-linux/include -I./src
MATHLIB = -lm
LDFLAGS_SHARED = -shared -Wl,-soname,libp528.so

OBJDIR = build/obj
SRC_LIB = $(wildcard submodules/p528-linux/src/*/*.cpp)
OBJ = $(patsubst submodules/p528-linux/src/%.cpp,$(OBJDIR)/%.o,$(SRC_LIB))
APPDIR = apps
LIB = $(APPDIR)/libp528.so
EXEC = $(APPDIR)/p528-area
EXEC2 = $(APPDIR)/p528-hvd

SRCS := src/P528LinuxArea.cpp
SRCS2 := src/P528LinuxHvD.cpp

# Use absolute paths for cleaning to work from any subdirectory
BUILD_DIR = $(abspath build)
APP_DIR_ABS = $(abspath $(APPDIR))

.PHONY: all clean clean-obj

all: $(LIB) $(EXEC) $(EXEC2) clean-obj

$(LIB): $(OBJ)
	@mkdir -p $(APPDIR)
	$(CXX) $(LDFLAGS_SHARED) -o $@ $(OBJ) $(MATHLIB)

$(EXEC): $(SRCS) $(LIB)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ -L$(APPDIR) -lp528 -ldl

$(EXEC2): $(SRCS2) $(LIB)
	$(CXX) $(CXXFLAGS) $(SRCS2) -o $@ -L$(APPDIR) -lp528 -ldl

$(OBJDIR)/%.o: submodules/p528-linux/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(APP_DIR_ABS)

clean-obj:
	rm -rf $(BUILD_DIR)
