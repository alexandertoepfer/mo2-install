## MO2 Simple Installer (mo2-install)

A lightweight installer that extracts mod archives, interprets FOMOD XML/JSON when present, and installs files into a target mod directory. It consists of:

- C++ core (`src.cpp`): builds into a Windows DLL (`mo2-installer.dll`).
- Python plugin (`mo2-install.py`): integrates the DLL with Mod Organizer 2 as a tool.

### Features
- **Archive extraction**: Uses libarchive to read common formats (7z/zip/rar, etc.).
- **ZIP creation**: Utility to package folders as ZIPs.
- **FOMOD handling**: Parses `ModuleConfig.xml` with PugiXML and JSON config for scripted installs.
- **Conditional installs**: Evaluates flag dependencies from plugins/groups.
- **Fallback install**: Sensible behavior when no `fomod/` exists (nested mod structure detection and copy).
- **Structured logging**: C++ log callback bridged to Python; per-mod log reconfiguration.

## Requirements
- Windows (x64) for building and running the DLL
- MSVC (Visual Studio 2022) or compatible compiler
- CMake (optional but recommended)
- Libraries (link/include as appropriate):
  - libarchive
  - PugiXML
  - nlohmann/json (header-only)
- Python 3.10+ (for MO2 environment) with PyQt6 and MO2 SDK/runtime available

### File Overview
- `src.cpp`: C++ implementation of the installer entrypoint `install()` plus helpers. Exports:
  - `setLogCallback(LogCallback)`
  - `install(const char* archivePath, const char* modPath)`
- `mo2-install.py`: MO2 tool plugin wiring the log callback and calling the DLL `install()`.

## Build (C++ DLL)
You need a DLL named `mo2-installer.dll` on your system PATH or in a discoverable folder. The plugin searches:
- Current working directory
- `dlls/mo2si` under CWD
- Script directory
- Directories in `%PATH%`

### Option A: Quick MSVC build (single file)
```bat
REM Developer Command Prompt for VS
cl /std:c++20 /O2 /EHsc /MD /LD src.cpp /Fe:mo2-installer.dll ^
  /I path\\to\\nlohmann_json\\include /I path\\to\\pugixml\\include ^
  path\\to\\libarchive.lib pugixml.lib
```
Ensure lib directories are on the linker path and DLL dependencies are available at runtime.

### Option B: CMake (recommended)
```cmake
cmake_minimum_required(VERSION 3.20)
project(mo2_installer LANGUAGES CXX)
add_library(mo2-installer SHARED src.cpp)
target_compile_features(mo2-installer PRIVATE cxx_std_20)
# Adjust these to your environment
# target_include_directories(mo2-installer PRIVATE <json_include> <pugixml_include>)
# target_link_libraries(mo2-installer PRIVATE libarchive pugixml)
# target_compile_definitions(mo2-installer PRIVATE _WIN32)
```
Build:
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
The resulting `mo2-installer.dll` should be placed where the Python plugin can find it (see search paths above).

## Install and Use (MO2 Plugin)
- Copy `mo2-install.py` into your MO2 plugins/tools location, or load it in your MO2 plugin environment.
- Ensure `mo2-installer.dll` is resolvable (see search paths). A typical layout:
```
<MO2 profile or working dir>/
  dlls/
    mo2si/
      mo2-installer.dll
  logs/
```

### Running the tool
Inside MO2, open the tool “Install Mods”:
- A file picker lets you select archives: `*.001 *.7z *.fomod *.zip *.rar`.
- The plugin creates a target mod (derived from archive name) and calls the DLL.
- Logs are written to `logs/mo2si.log` initially; during install they are reconfigured per-mod (`<mod>/mo2si-install.log`).

## Program Flow
1. Python plugin loads DLL and registers the log callback.
2. User selects one or more archives; the queue is processed sequentially.
3. C++ `install()` extracts to a temp dir and searches for a `fomod/` folder:
   - If found: parse `ModuleConfig.xml` and optional JSON alongside the archive to map and copy files.
   - If not found: detect main mod folder(s) and copy, using `moduleName` (from JSON) when multiple candidates exist.
4. Files are copied into the MO2 mod directory.

## Configuration Files
- `ModuleConfig.xml` (FOMOD): Controls plugin/groups and file mappings.
- `<archive_stem>.json` (optional, next to archive): May include `moduleName` and `installFiles` entries used by the installer logic in `src.cpp`.

## Logging
- C++ emits messages via `log()`; Python bridges them to MO2 logs.
- Default log file: `logs\\mo2si.log` (relative to CWD). During per-mod install, logs go to `<mod>/mo2si-install.log`.

## Doxygen (C++)
`src.cpp` contains Doxygen-style comments for public functions and helpers. To generate docs:
```bash
doxygen -g
# Edit Doxyfile INPUT to include src.cpp, then:
doxygen
```

## Development Tips
- Keep the C++ DLL functions `extern "C"` compatible for stable ctypes interop.
- Avoid throwing across the DLL boundary; return error strings as implemented.
- Ensure third-party DLLs required by `libarchive` are present in the same directory as `mo2-installer.dll` or on PATH.

## License
Add your preferred license here (e.g., MIT). If using third-party libraries, respect their licenses.

