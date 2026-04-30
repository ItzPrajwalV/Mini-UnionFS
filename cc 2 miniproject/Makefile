# Mini Union File System using FUSE
# Makefile for compiling and installing the unionfs module

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse3
LDFLAGS = $(shell pkg-config --libs fuse3)
CFLAGS += $(shell pkg-config --cflags fuse3)

# Output binary
TARGET = mini_unionfs
SOURCE = mini_unionfs.c

# Installation directory
INSTALL_DIR = /usr/local/bin

# Default target
all: $(TARGET)

# Compile the main binary
$(TARGET): $(SOURCE)
	@echo "Compiling Mini Union FileSystem..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)
	@echo "Successfully compiled: $(TARGET)"

# Install binary to system
install: $(TARGET)
	@echo "Installing $(TARGET) to $(INSTALL_DIR)..."
	@if [ ! -d "$(INSTALL_DIR)" ]; then mkdir -p $(INSTALL_DIR); fi
	cp $(TARGET) $(INSTALL_DIR)/$(TARGET)
	chmod 755 $(INSTALL_DIR)/$(TARGET)
	@echo "Installation complete!"

# Uninstall binary
uninstall:
	@echo "Removing $(TARGET) from $(INSTALL_DIR)..."
	rm -f $(INSTALL_DIR)/$(TARGET)
	@echo "Uninstallation complete!"

# Clean build artifacts
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET)
	@echo "Clean complete!"

# Debug build with symbols
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

# Strip binary to reduce size
strip: $(TARGET)
	strip $(TARGET)

# Check for required dependencies
check-deps:
	@echo "Checking dependencies..."
	@command -v gcc >/dev/null 2>&1 || { echo "Error: gcc not found"; exit 1; }
	@command -v pkg-config >/dev/null 2>&1 || { echo "Error: pkg-config not found"; exit 1; }
	@pkg-config --exists fuse3 || { echo "Error: libfuse3-dev not found"; exit 1; }
	@echo "All dependencies found!"

# Help target
help:
	@echo "Mini Union FileSystem - Makefile targets:"
	@echo "  make              - Compile the mini_unionfs binary"
	@echo "  make debug        - Compile with debug symbols"
	@echo "  make install      - Install binary to $(INSTALL_DIR)"
	@echo "  make uninstall    - Remove binary from $(INSTALL_DIR)"
	@echo "  make clean        - Remove compiled binary"
	@echo "  make strip        - Remove debug symbols from binary"
	@echo "  make check-deps   - Check for required dependencies"
	@echo "  make help         - Show this help message"

.PHONY: all install uninstall clean debug strip check-deps help
