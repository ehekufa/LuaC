CC ?= cc
CXX ?= c++
AR ?= ar
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
WARNINGS = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
RUNTIME_PATH = $(abspath src/runtime.h)
BUILD = build
DL_LIBS = -ldl
SHARED_EXT = so
SHARED_FLAG = -shared
GUI_LIBS = $(shell pkg-config --libs glfw3 vulkan 2>/dev/null)
GUI_CFLAGS = $(shell pkg-config --cflags glfw3 vulkan 2>/dev/null)
ifeq ($(OS),Windows_NT)
  DL_LIBS =
  SHARED_EXT = dll
  EXE = .exe
endif
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
  DL_LIBS =
  SHARED_EXT = dylib
  SHARED_FLAG = -dynamiclib
endif

.PHONY: all clean test install libraries gui android-check
all: luac

luac: src/luac.c src/luac.h src/runtime.h
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -DLUAC_RUNTIME_PATH=\"$(RUNTIME_PATH)\" src/luac.c -o $@ $(DL_LIBS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/luac.o: src/luac.c src/luac.h src/runtime.h | $(BUILD)
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -fPIC -DLUAC_LIBRARY_ONLY -DLUAC_BUILD_SHARED -DLUAC_RUNTIME_PATH=\"$(RUNTIME_PATH)\" -c src/luac.c -o $@

libraries: $(BUILD)/libluac.a $(BUILD)/libluac.$(SHARED_EXT)

$(BUILD)/libluac.a: $(BUILD)/luac.o
	$(AR) rcs $@ $<

$(BUILD)/libluac.$(SHARED_EXT): $(BUILD)/luac.o
	$(CC) $(SHARED_FLAG) $< -o $@

# Optional desktop Vulkan editor. Requires glfw3, vulkan and glslangValidator.
$(BUILD)/shader.vert.spv: gui/shader.vert | $(BUILD)
	glslangValidator -V $< -o $@
$(BUILD)/shader.frag.spv: gui/shader.frag | $(BUILD)
	glslangValidator -V $< -o $@

gui: $(BUILD)/shader.vert.spv $(BUILD)/shader.frag.spv
	$(CXX) -std=c++17 $(CXXFLAGS) $(GUI_CFLAGS) -I src -I gui gui/app.cpp gui/vulkan.cpp -o $(BUILD)/luac-gui$(EXE) $(GUI_LIBS)

test: luac
	./tests/run.sh

install: src/luac.c src/luac.h src/runtime.h
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/luac
	install -m644 src/runtime.h $(DESTDIR)$(PREFIX)/share/luac/runtime.h
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -DLUAC_RUNTIME_PATH=\"$(PREFIX)/share/luac/runtime.h\" src/luac.c -o $(DESTDIR)$(PREFIX)/bin/luac $(DL_LIBS)

clean:
	rm -f luac luac.exe a.luac.c
	rm -rf $(BUILD) tests/.tmp
