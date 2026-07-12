# Windhawk mods - local development Makefile
# Runs with GNU Make (cmd.exe shell on Windows). Run `make` or `make help`.
#
# Mods are compiled into DLLs by the Windhawk engine at runtime, so "building"
# here means a local Clang/g++ syntax check via the offline API stubs. Always do
# a final test inside Windhawk.

MODS := $(wildcard mods/*.wh.cpp)
# Target a single mod: make check MOD=mods/example-mod.wh.cpp
TARGETS := $(if $(MOD),$(MOD),$(MODS))

CLANG_FORMAT ?= $(if $(wildcard C:/Users/Gunte/scoop/apps/llvm/current/bin/clang-format.exe),C:/Users/Gunte/scoop/apps/llvm/current/bin/clang-format.exe,clang-format)
PS_SCRIPT    := Scripts\Test-ModCompile.ps1

.PHONY: help check test format format-check list clean

help: ## Show available targets
	@echo Windhawk mods - make targets:
	@echo.
	@echo   make check           Syntax-check all mods locally (no Windhawk needed)
	@echo   make check MOD=...   Check a single mod file
	@echo   make format          Format all mods in place with clang-format
	@echo   make format-check    Verify formatting without changing files
	@echo   make list            List discovered mod files
	@echo   make clean           Remove generated build artifacts
	@echo.
	@echo Detected mods: $(MODS)

list: ## List discovered mods
	@echo $(MODS)

check test: ## Syntax-check mods with the local toolchain
	powershell -NoProfile -ExecutionPolicy Bypass -File $(PS_SCRIPT) $(if $(MOD),-Path "$(MOD)")

format: ## Format mods in place
	$(CLANG_FORMAT) -i $(TARGETS)

format-check: ## Check formatting without modifying files
	$(CLANG_FORMAT) --dry-run -Werror $(TARGETS)

clean: ## Remove build artifacts
	powershell -NoProfile -Command "Remove-Item -Recurse -Force bin,obj,build -ErrorAction SilentlyContinue; Get-ChildItem -Recurse mods -Include *.o,*.obj,*.dll,*.pdb -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue; Write-Output Cleaned"
