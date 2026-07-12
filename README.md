# windhawk-mods (agents-ctx)

A local development project for [Windhawk](https://windhawk.net/) mods. Each mod
is a single C++ source file that Windhawk compiles into a DLL and injects into
target processes.

## Project layout

```
.
├── mods/                          # Mod source files live here (one *.wh.cpp per mod)
│   └── taskbar-left-text.wh.cpp   # Shows custom text on the taskbar
├── .vscode/
│   ├── windhawk_headers_1.7.3/    # Offline API headers (for IntelliSense + local checks)
│   │   ├── windhawk_api.h
│   │   └── windhawk_utils.h
│   └── c_cpp_properties.json      # VS Code C/C++ IntelliSense config
├── Scripts/
│   └── Test-ModCompile.ps1        # Local syntax-check for mods (no Windhawk needed)
├── compile_flags.txt              # clangd/clang tooling flags (applies to all mods)
├── .clang-format                  # Matches the official windhawk-mods style
└── README.md
```

## Anatomy of a mod

A `.wh.cpp` file has up to four sections, all in one file:

1. **`// ==WindhawkMod==`** — metadata: `@id`, `@name`, `@include` (target exe),
   `@compilerOptions`, `@author`, `@version`, `@license`, ...
2. **`// ==WindhawkModReadme==`** — Markdown shown to users in Windhawk.
3. **`// ==WindhawkModSettings==`** — YAML schema of user-editable settings.
4. **C++ code** — implement callbacks. The engine injects the Windhawk API, so
   you normally do **not** `#include` the headers yourself when compiling inside
   Windhawk; they are only needed for local tooling.

Key callbacks:

| Callback               | When it runs                                         |
|------------------------|------------------------------------------------------|
| `Wh_ModInit`           | First thing; set up hooks here. Return `TRUE`.       |
| `Wh_ModAfterInit`      | After hooks are applied.                             |
| `Wh_ModSettingsChanged`| When the user changes settings (can request reload). |
| `Wh_ModBeforeUninit`   | Before hooks are removed.                            |
| `Wh_ModUninit`         | After hooks are removed; clean up.                   |

Reference: <https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod>

## Local development workflow

### 1. Syntax-check a mod (no Windhawk required)

```powershell
./Scripts/Test-ModCompile.ps1                              # check all mods
./Scripts/Test-ModCompile.ps1 mods/taskbar-left-text.wh.cpp   # check one file
```

The script runs a `-fsyntax-only` pass mirroring Windhawk's flags (C++23,
mingw target) using the offline API stubs in `.vscode/windhawk_headers_1.7.3`.
It auto-detects a working compiler: a native `clang++` if present, otherwise
MSYS2's `g++` (ucrt64/mingw64). Pass `-Compiler <path>` to force one.

> Only Windhawk's bundled Clang produces the final injected DLL, so a clean
> local check is necessary but not sufficient — always test the live mod in
> Windhawk.

### 2. Editor support

- **VS Code (C/C++ extension):** `.vscode/c_cpp_properties.json` is preconfigured
  with `WH_MOD`/`WH_EDITING` and the header include path.
- **clangd / clang-format:** drop `compile_flags.txt` and `.clang-format` are
  already set up. Run `clang-format -i mods/*.wh.cpp` to match the official
  style (Chromium-based, 4-space indent).

### 3. Load the mod in Windhawk

1. Open Windhawk → **Create new mod** (Dev mode).
2. Paste the contents of your `.wh.cpp` file.
3. Set the metadata `@id` and `@include` to your target process.
4. Enable **Detailed logging** while iterating; `Wh_Log` output appears in the
   editor log pane.
5. Debugging tips: <https://github.com/ramensoftware/windhawk/wiki/Debugging-the-mods>

## Creating a new mod

1. Copy `mods/taskbar-left-text.wh.cpp` to `mods/<your-mod-id>.wh.cpp`.
2. Update the `@id`, `@name`, `@description`, and `@include` lines.
3. Replace the hook in `Wh_ModInit` with your own.
4. Run `./Scripts/Test-ModCompile.ps1` to sanity-check syntax.

To submit to the official collection later, follow the rules in the
[windhawk-mods](https://github.com/ramensoftware/windhawk-mods) repository.
