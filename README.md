# **MO2 Installer (mo2-install)** 🛠️

A **lightweight installer** that extracts mod archives, interprets **FOMOD XML/JSON** configurations, and installs files into a target mod directory. Designed to simplify mod installation for **Mod Organizer 2 (MO2)** users.

## **📝 Overview**

* **C++ Core** (`src.cpp`): Compiled into a **Windows DLL** (`mo2-installer.dll`).
* **Python Plugin** (`mo2-install.py`): Integrates the DLL with **Mod Organizer 2** as a tool for streamlined mod management.

## **🌟 Features**

* **🗒️ FOMOD Handling**:
  Parses `ModuleConfig.xml` (using **PugiXML**) and JSON configurations for scripted installs.

* **🔄 Conditional Installs**:
  Evaluates flag dependencies from plugins and groups for flexible installation.

* **📜 Structured Logging**:
  Logs are captured by the C++ log callback and bridged to Python, allowing per-mod reconfiguration of log outputs.

## **⚙️ Requirements**

Before you begin, make sure you have:

* **Windows (x64)** for building and running the DLL.
* **MSVC (Visual Studio 2022)** or a compatible C++ compiler.
* Required libraries:
  * **libarchive**
  * **PugiXML**
  * **nlohmann/json** (header-only)
* **Python 3.10+** with **PyQt6** and the **MO2 SDK/runtime** available.

## **🔧 Install and Use (MO2 Plugin)**

1. **Copy `mo2-install.py`** to your MO2 plugins/tools directory, or load it into your MO2 environment.
2. Ensure **`mo2-installer.dll`** is discoverable (see search paths). Example layout:

```
<MO2 profile or working dir>/
  dlls/
    mo2si/
      mo2-installer.dll
  logs/
```

### **🔘 Running the Tool**

In **Mod Organizer 2**, open the **"Install Mods"** tool:

* Use the **file picker** to select mod archives (`*.001`, `*.7z`, `*.fomod`, `*.zip`, `*.rar`).
* The plugin creates a target mod folder based on the archive name and calls the DLL.

## **⚙️ Configuration Files**

* **`ModuleConfig.xml` (FOMOD)**: Contains plugin/group configurations and file mappings.
* **`<archive_stem>.json` (optional)**: Config file next to the archive, used to define `moduleName` and `installFiles` for the installer logic.


