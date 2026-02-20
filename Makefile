CC32 = i686-w64-mingw32-gcc
CC64 = x86_64-w64-mingw32-gcc
CFLAGS = -Wall -O2 -Iinclude -municode
LDFLAGS = -municode -lkernel32 -luser32

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS32 = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/32/%.o)
OBJECTS64 = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/64/%.o)
TARGET32 = $(BIN_DIR)/injector32.exe
TARGET64 = $(BIN_DIR)/injector64.exe

.PHONY: all clean help example

all: $(TARGET32) $(TARGET64)

example: all
	$(MAKE) -C example

$(TARGET32): $(OBJECTS32) | $(BIN_DIR)
	$(CC32) $(OBJECTS32) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(TARGET64): $(OBJECTS64) | $(BIN_DIR)
	$(CC64) $(OBJECTS64) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(BUILD_DIR)/32/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/32
	$(CC32) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/64/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/64
	$(CC64) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/32:
	mkdir -p $(BUILD_DIR)/32

$(BUILD_DIR)/64:
	mkdir -p $(BUILD_DIR)/64

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	$(MAKE) -C example clean

help:
	@echo "Proton DLL Injector - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build injector32.exe and injector64.exe (default)"
	@echo "  example - Build the injector and example DLL"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - i686-w64-mingw32-gcc   (MinGW-w64 32-bit cross-compiler)"
	@echo "  - x86_64-w64-mingw32-gcc (MinGW-w64 64-bit cross-compiler)"
