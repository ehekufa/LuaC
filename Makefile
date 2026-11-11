CC ?= cc
CXX ?= c++
AR ?= ar
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
WARNINGS = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
RUNTIME_PATH = $(abspath src/runtime.h)
BUILD = build
DL_LIBS =
SHARED_EXT = so
SHARED_FLAG = -shared
GUI_LIBS = $(shell pkg-config --libs glfw3 vulkan 2>/dev/null)
GUI_CFLAGS = $(shell pkg-config --cflags glfw3 vulkan 2>/dev/null)
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Linux)
  DL_LIBS = -ldl
endif
ifeq ($(OS),Windows_NT)
  SHARED_EXT = dll
  EXE = .exe
endif
ifneq ($(findstring MINGW,$(UNAME_S)),)
  SHARED_EXT = dll
  EXE = .exe
endif
ifeq ($(UNAME_S),Darwin)
  DL_LIBS =
  SHARED_EXT = dylib
  SHARED_FLAG = -dynamiclib
endif

.PHONY: all clean test install libraries gui android-check
all: luac

luac: src/luac.c src/luac.h src/runtime.h src/luac_libraries.h
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -DLUAC_RUNTIME_PATH=\"$(RUNTIME_PATH)\" src/luac.c -o $@ $(DL_LIBS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/luac.o: src/luac.c src/luac.h src/runtime.h src/luac_libraries.h | $(BUILD)
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -fPIC -DLUAC_LIBRARY_ONLY -DLUAC_BUILD_SHARED -DLUAC_RUNTIME_PATH=\"$(RUNTIME_PATH)\" -c src/luac.c -o $@

libraries: $(BUILD)/libluac.a $(BUILD)/libluac.$(SHARED_EXT)

$(BUILD)/libluac.a: $(BUILD)/luac.o
	$(AR) rcs $@ $<

$(BUILD)/libluac.$(SHARED_EXT): $(BUILD)/luac.o
	$(CC) $(SHARED_FLAG) $< -o $@

# Nuklear stays C99 and uses only the fixed arena supplied by the Studio.
$(BUILD)/nuklear.o: gui/nuklear_impl.c gui/third_party/nuklear.h | $(BUILD)
	$(CC) -std=c99 $(CFLAGS) $(WARNINGS) -I gui -c gui/nuklear_impl.c -o $@

$(BUILD)/ui-test$(EXE): tests/ui_test.cpp gui/nuklear_canvas.hpp gui/geometry.hpp $(BUILD)/nuklear.o
	$(CXX) -std=c++17 $(CXXFLAGS) $(WARNINGS) -I gui tests/ui_test.cpp $(BUILD)/nuklear.o -o $@

$(BUILD)/engine-test$(EXE): tests/engine_test.cpp src/engine.hpp | $(BUILD)
	$(CXX) -std=c++17 $(CXXFLAGS) $(WARNINGS) -I src tests/engine_test.cpp -o $@

# Optional desktop Vulkan editor. Requires glfw3, vulkan and glslangValidator.
$(BUILD)/shader.vert.spv: gui/shader.vert | $(BUILD)
	glslangValidator -V $< -o $@
$(BUILD)/shader.frag.spv: gui/shader.frag | $(BUILD)
	glslangValidator -V $< -o $@

gui: $(BUILD)/shader.vert.spv $(BUILD)/shader.frag.spv $(BUILD)/nuklear.o
	$(CXX) -std=c++17 $(CXXFLAGS) $(GUI_CFLAGS) -I src -I gui gui/app.cpp gui/vulkan.cpp $(BUILD)/nuklear.o -o $(BUILD)/luac-gui$(EXE) $(GUI_LIBS)

test: luac $(BUILD)/ui-test$(EXE) $(BUILD)/engine-test$(EXE)
	./tests/run.sh

install: src/luac.c src/luac.h src/runtime.h src/luac_libraries.h src/engine.hpp libraries
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/share/luac
	install -m644 src/runtime.h $(DESTDIR)$(PREFIX)/share/luac/runtime.h
	install -m644 src/luac_libraries.h $(DESTDIR)$(PREFIX)/share/luac/luac_libraries.h
	install -m644 src/luac.h $(DESTDIR)$(PREFIX)/include/luac.h
	install -m644 src/engine.hpp $(DESTDIR)$(PREFIX)/include/engine.hpp
	install -m644 $(BUILD)/libluac.a $(DESTDIR)$(PREFIX)/lib/libluac.a
	install -m755 $(BUILD)/libluac.$(SHARED_EXT) $(DESTDIR)$(PREFIX)/lib/libluac.$(SHARED_EXT)
	$(CC) -std=gnu99 $(CFLAGS) $(WARNINGS) -DLUAC_RUNTIME_PATH=\"$(PREFIX)/share/luac/runtime.h\" src/luac.c -o $(DESTDIR)$(PREFIX)/bin/luac $(DL_LIBS)

clean:
	rm -f luac luac.exe a.luac.c
	rm -rf $(BUILD) tests/.tmp
