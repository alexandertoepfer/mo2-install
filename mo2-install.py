"""
Plugin: Install Mods
Authors: MaskPlague & Griffin

This plugin enables the sequential installation of multiple mod archives
for a mod organizer using a C++ DLL for installation.
It sets up logging, registers a C++ log callback, and handles mod installations
by scanning for the required DLL in common directories.
"""

import mobase
import platform
import logging
import time
import threading
import os
import re
import sys
import ctypes

from pathlib import Path
from mobase import GuessedString

from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QFileDialog

# ------------------------------------------------------------------------------
# Logger Initialization
# ------------------------------------------------------------------------------
# Create a logger for this module and set the logging level to DEBUG.
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

# Clear any existing logging handlers to ensure a clean configuration.
if logger.hasHandlers():
    logger.handlers.clear()

# Configure a default file handler.
# The log file is created in a 'logs' subdirectory in the current working directory.
default_log_file = Path.cwd() / r"logs\mo2si.log"
default_log_file.parent.mkdir(parents=True, exist_ok=True)
default_handler = logging.FileHandler(str(default_log_file))
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
default_handler.setFormatter(formatter)
logger.addHandler(default_handler)

# Log a test message to confirm that logging is working correctly.
logger.debug("Logger configured successfully.")

# ------------------------------------------------------------------------------
# Callback Function Definition
# ------------------------------------------------------------------------------
# Define the callback function type that matches the expected C++ signature.
CALLBACK_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_char_p)

# ------------------------------------------------------------------------------
# Plugin Class: InstallMods
# ------------------------------------------------------------------------------
class InstallMods(mobase.IPluginTool):
    """
    Plugin tool for installing mod archives.

    This class implements the IPluginTool interface required by the mod organizer.
    It integrates with a C++ DLL to perform installations and manages the
    installation queue along with logging for each mod installation.
    """

    def __init__(self):
        super(InstallMods, self).__init__()
        self._parentWidget = None  # Reference to the parent GUI widget (if needed)
        self._log_callback = None  # Store the log callback instance to prevent garbage collection

    def init(self, organiser=mobase.IOrganizer, manager=mobase.IInstallationManager):
        """
        Initialize the plugin with references to the mod organizer and installation manager.

        Configures internal variables and registers the logging callback with the DLL.
        Logs system information for debugging purposes.
        """
        self.debug = True
        self.num = 0
        self._modList = []
        self.finished = True
        self._organizer = organiser
        self._manager = manager
        self._queue = []
        self.handler = None

        # Retrieve the last used download path from plugin settings.
        self.downloadLocation = self._organizer.pluginSetting(self.name(), "LastPath")

        # Log system and Python environment details.
        logger.info(sys.version)
        logger.info(sys.executable)
        logger.info(os.getcwd())
        logger.info(platform.architecture())

        # Register the Python log callback with the C++ DLL.
        self.setLog()
        return True

    @staticmethod
    def log_callback(message: bytes):
        """
        Callback function to log messages from the C++ DLL.

        Converts the received C-style string (bytes) into a Python string and logs it.
        """
        logger.info(message.decode("utf-8"))

    def setLog(self):
        """
        Register the Python log callback with the C++ DLL.

        This method loads the DLL, sets up the function signature for the log callback,
        and registers the callback function with the DLL. It also saves the callback
        reference to prevent it from being garbage-collected.
        """
        try:
            # Create a callback instance with the defined function signature.
            c_callback = CALLBACK_TYPE(InstallMods.log_callback)
            dll_path = self.find_dll()
            logger.info("Setting log callback using DLL: " + str(dll_path))

            # Load the DLL using ctypes.
            lib = ctypes.CDLL(str(dll_path))
            # Define the expected argument and return types for the setLogCallback function.
            lib.setLogCallback.argtypes = [CALLBACK_TYPE]
            lib.setLogCallback.restype = None
            # Register the callback with the DLL.
            lib.setLogCallback(c_callback)
            # Retain a reference to the callback to avoid garbage collection.
            self._log_callback = c_callback
            logger.debug("Log callback registered successfully.")
        except Exception as e:
            logger.error("Failed to set log callback: " + str(e))

    def find_dll(self, dll_name="mo2-installer.dll"):
        """
        Locate the required DLL for installation.

        Searches for the DLL in several common locations:
          - The current working directory.
          - A subdirectory 'dlls/mo2si' in the current working directory.
          - The directory where this script is located.
          - Directories listed in the system PATH.

        Returns:
            The resolved path to the DLL.

        Raises:
            FileNotFoundError: If the DLL cannot be found.
        """
        logger.info("Current path: " + str(Path.cwd()))
        search_paths = [
            Path.cwd(),  # Current working directory
            Path(Path.cwd() / r"dlls\mo2si"),  # Specific DLL subdirectory for Mo2
            Path(__file__).parent,  # Directory where this script is located
            *[Path(p) for p in os.getenv("PATH", "").split(os.pathsep) if Path(p).exists()]  # System PATH directories
        ]

        for path in search_paths:
            dll_path = path / dll_name
            if dll_path.exists():
                return dll_path.resolve()

        raise FileNotFoundError(f"Could not find {dll_name} in common locations.")

    def install(self, archive_path: str, install_path: str, dll_name="mo2-installer.dll") -> str:
        """
        Call the DLL's 'Install' function to perform a mod installation.

        Validates the existence of both the archive and installation paths,
        then calls the install function from the DLL.

        Args:
            archive_path (str): The path to the mod archive.
            install_path (str): The target installation directory.
            dll_name (str): Name of the DLL file to use.

        Returns:
            str: The output from the DLL function, decoded to a Python string.

        Raises:
            FileNotFoundError: If either the archive or installation path does not exist.
        """
        dll_path = self.find_dll(dll_name)
        logger.info("Using DLL: " + str(dll_path))
        lib = ctypes.CDLL(str(dll_path))

        # Define the expected function signature for install.
        lib.install.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        lib.install.restype = ctypes.c_char_p  # Expecting a C string as output

        archive_path = Path(archive_path)
        if not archive_path.exists():
            raise FileNotFoundError(f"Archive file not found: {archive_path}")

        install_path = Path(install_path)
        if not install_path.exists():
            raise FileNotFoundError(f"Mod file not found: {install_path}")

        # Encode paths to bytes for the C function.
        archive_path_encoded = str(archive_path).encode("utf-8")
        install_path_encoded = str(install_path).encode("utf-8")
        result = lib.install(ctypes.c_char_p(archive_path_encoded), ctypes.c_char_p(install_path_encoded))

        return result.decode("utf-8")  # Convert the returned C string to a Python string

    # ------------------------------------------------------------------------------
    # Plugin Metadata and UI Methods
    # ------------------------------------------------------------------------------
    def name(self) -> str:
        """Return the internal name of the plugin."""
        return "Install Mods"

    def localizedName(self) -> str:
        """Return the localized name of the plugin for display purposes."""
        return self.tr("Install Mods")

    def author(self) -> str:
        """Return the plugin authors."""
        return "MaskPlague & Griffin"

    def description(self):
        """Return a brief description of the plugin functionality."""
        return self.tr("Allows manual selection of multiple archives for sequential installation.")

    def version(self) -> mobase.VersionInfo:
        """Return the plugin version information."""
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.ALPHA)

    def settings(self):
        """
        Return the list of configurable settings for the plugin.

        In this case, it remembers the last opened path used for installations.
        """
        return [
            mobase.PluginSetting("LastPath", self.tr("Last opened path for installing."), "downloads"),
        ]

    def displayName(self):
        """Return the display name of the plugin."""
        return self.tr("Install Mods")

    def tooltip(self):
        """Return an empty tooltip text (can be customized if needed)."""
        return self.tr("")

    def icon(self):
        """Return a default icon for the plugin."""
        return QIcon()

    def display(self):
        """
        Open a file dialog for the user to select mod archive files.

        Saves the chosen directory to the plugin settings and initiates the installation queue.
        """
        self._queue = QFileDialog.getOpenFileNames(
            self._parentWidget,
            "Open File",
            self.downloadLocation,
            "Mod Archives (*.001 *.7z *.fomod *.zip *.rar)",
        )[0]
        if len(self._queue) > 0:
            # Update the last used download location based on the first selected file.
            pathGet = self._queue[0]
            self.downloadLocation = os.path.split(os.path.abspath(pathGet))[0]
            self._organizer.setPluginSetting(self.name(), "LastPath", self.downloadLocation)

        self._installQueue()
        return

    def getFiles(self):
        """This method is not used but required by the interface."""
        return

    def tr(self, text):
        """Helper function for translating plugin text."""
        return QCoreApplication.translate("Install Mod(s)", text)

    # ------------------------------------------------------------------------------
    # Internal Utility Methods
    # ------------------------------------------------------------------------------
    def _log(self, string):
        """
        Debug logging helper.

        If debug mode is enabled, prints a log message with an incrementing counter.
        """
        if self.debug:
            print("Install Multiple Mods log" + str(self.num) + ": " + string)
            self.num += 1

    def _configure_mod_logger(self, mod_dir: Path):
        """
        Reconfigure the logger to write into a mod-specific log file.

        This method creates (or reuses) an 'mo2si-install.log' file in the mod directory,
        and sets up a new file handler so that subsequent logs are directed there.

        Args:
            mod_dir (Path): The directory of the mod being installed.
        """
        mod_log_file = mod_dir / "mo2si-install.log"
        mod_log_file.parent.mkdir(parents=True, exist_ok=True)
        # Remove all current handlers.
        for handler in logger.handlers[:]:
            logger.removeHandler(handler)
        # Create a new file handler for the mod log file.
        mod_handler = logging.FileHandler(str(mod_log_file))
        mod_handler.setFormatter(formatter)
        logger.addHandler(mod_handler)
        logger.info("Logger reconfigured for mod directory: " + str(mod_dir))

    def _close_logger(self):
        """
        Close and flush all logger handlers.

        This method is used to ensure that all log files are properly written and closed
        after a mod installation is complete.
        """
        for handler in logger.handlers[:]:
            handler.flush()
            handler.close()
            logger.removeHandler(handler)
        logger.debug("Logger handlers closed.")

    def _installQueue(self):
        """
        Process the installation queue.

        If there are mod archive files in the queue and no installation is running,
        this method pops the next archive, prepares its mod name, creates a mod entry,
        reconfigures the logger for that mod, and calls the installation method.
        After installation, it refreshes the organizer and proceeds with the next item.
        """
        if self.finished and len(self._queue) != 0:
            self.finished = False
            path = self._queue.pop(0)
            base_name = os.path.basename(path)
            # Remove file extensions to derive a clean mod name.
            while '.' in base_name:
                base_name = os.path.splitext(base_name)[0]
            # Remove any leading digits or underscores/hyphens.
            base_name = re.sub(r'^\d+[-_]', '', base_name)

            # Create a new mod entry in the organizer.
            mod = self._organizer.createMod(base_name)
            mod_dir = Path(mod.absolutePath())
            self._configure_mod_logger(mod_dir)

            logger.info("Starting installation for mod: " + base_name)
            # Call the installation function from the DLL.
            path = self.install(path, mod.absolutePath())
            self._organizer.refresh()
            logger.info("Finished installation for mod: " + base_name)

            self._close_logger()
            time.sleep(0.2)
            self.finished = True
            self._installQueue()

        elif len(self._queue) == 0:
            self.finished = True

        return

# ------------------------------------------------------------------------------
# Plugin Factory Function
# ------------------------------------------------------------------------------
def createPlugin():
    """
    Factory function to create an instance of the InstallMods plugin.

    This is used by the mod organizer to load the plugin.
    """
    return InstallMods()
