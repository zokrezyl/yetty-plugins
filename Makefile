# Yetty Plugins Build System
# Usage: make <target>

# Use system tools for most builds (avoid nix)
SYSTEM_PATH := /usr/bin:/bin:/usr/local/bin:$(PATH)

# Build directories (with debug/release suffix)
BUILD_DIR_DESKTOP_DEBUG := build-desktop-debug
BUILD_DIR_DESKTOP_RELEASE := build-desktop-release

# Legacy build directories (for backward compatibility)
BUILD_DIR_DESKTOP := build-desktop

# CMake options
CMAKE := cmake
CMAKE_GENERATOR := -G Ninja
CMAKE_RELEASE := -DCMAKE_BUILD_TYPE=Release
CMAKE_DEBUG := -DCMAKE_BUILD_TYPE=Debug

# Default target - show help
.PHONY: all
all: help

#=============================================================================
# Desktop
#=============================================================================

.PHONY: config-desktop-debug
config-desktop-debug: ## Configure desktop debug build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG)

.PHONY: config-desktop-release
config-desktop-release: ## Configure desktop release build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE)

.PHONY: build-desktop-debug
build-desktop-debug: ## Build desktop debug
	@if [ ! -f "$(BUILD_DIR_DESKTOP_DEBUG)/build.ninja" ]; then $(MAKE) config-desktop-debug; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_DEBUG) --parallel

.PHONY: build-desktop-release
build-desktop-release: ## Build desktop release
	@if [ ! -f "$(BUILD_DIR_DESKTOP_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_RELEASE) --parallel

#=============================================================================
# Convenience aliases (default to release)
#=============================================================================

.PHONY: config-desktop build-desktop
config-desktop: config-desktop-release ## Alias for config-desktop-release
build-desktop: build-desktop-release ## Alias for build-desktop-release

#=============================================================================
# Clean
#=============================================================================

.PHONY: clean
clean: ## Clean all build directories
	rm -rf $(BUILD_DIR_DESKTOP_DEBUG) $(BUILD_DIR_DESKTOP_RELEASE) $(BUILD_DIR_DESKTOP)

#=============================================================================
# Help
#=============================================================================

.PHONY: help
help:
	@echo "Yetty Plugins Build System"
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-24s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Build outputs:"
	@echo "  build-desktop-{debug,release}/plugins/*.so"
	@echo "  build-desktop-{debug,release}/lib/libyetty_core.so"
