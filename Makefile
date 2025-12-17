CC = x86_64-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres
CFLAGS = -Wall -O2 -Iinclude -municode
LDFLAGS = -municode -lkernel32 -luser32

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/injector.exe

.PHONY: all clean

all: $(TARGET)

example: $(TARGET)
	$(MAKE) -C example

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	$(MAKE) -C example clean

help:
	@echo "Proton DLL Injector - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  example - Build the injector and example DLL"
	@echo "  all     - Build the injector (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - x86_64-w64-mingw32-gcc (MinGW-w64 cross-compiler)"
	@echo ""
	@echo "Usage example:"
	@echo "  make"
	@echo "  make clean"
