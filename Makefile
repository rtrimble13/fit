# Common tasks. Run `make help` for the list. Every target wraps commands documented in
# CONTRIBUTING.md and docs/versioning-and-releases.md, which also work without make.

PYTHON       ?= python3
CLANG_FORMAT ?= clang-format
BASE         ?= origin/main
RELEASE      := $(PYTHON) tools/release.py
SOURCES       = $(shell find include src tests \( -name '*.hpp' -o -name '*.cpp' \))

.DEFAULT_GOAL := help
.PHONY: help build test release-build format format-check versions release-plan package check clean

help: ## Show this help
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  %-14s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## Configure and build the dev preset (Debug, ASan + UBSan)
	cmake --preset dev
	cmake --build --preset dev

test: build ## Run every test under the dev preset
	ctest --preset dev

release-build: ## Configure, build and test the release preset
	cmake --preset release
	cmake --build --preset release
	ctest --preset release

format: ## Format the C++ sources in place
	$(CLANG_FORMAT) -i $(SOURCES)

format-check: ## Check formatting as CI does
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

versions: ## Check VERSION and CHANGELOG.md, and the version bump against BASE (default origin/main)
	$(RELEASE) validate
	$(RELEASE) check-versions --base $(BASE)

release-plan: release-build ## Show what the next merge to main would release
	$(RELEASE) release --dry-run

package: release-build ## Build the release archive and its checksum into dist/
	$(RELEASE) package

check: test format-check versions ## The checks CI runs on a pull request (with one compiler)

clean: ## Remove build output and release archives
	rm -rf build dist
