/**
 * @file src.cpp
 * @brief C++ core for the MO2 Simple Installer DLL.
 *
 * @details
 * Provides a small set of utilities for archive extraction, ZIP creation,
 * logging, XML/JSON parsing and a single exported entry point, `install()`,
 * that performs FOMOD-aware installations. The implementation relies on
 * `libarchive` for archive handling, `pugixml` for XML/XPath and
 * `nlohmann::json` for JSON configuration.
 *
 * The exported API is designed for consumption via Python's `ctypes` and is
 * used by the Mod Organizer 2 tool plugin defined in `mo2-install.py`.
 *
 * @note This file targets Windows for console management, but most logic is
 *       platform-agnostic. The DLL visibility macro is defined accordingly.
 * @see mo2-install.py
 */

 #include <filesystem>
 #include <fstream>
 #include <iostream>
 #include <stdexcept>
 #include <unordered_map>
 #include <string>
 #include <random>
 #include <sstream>
 #include <format>
 
 #include <archive.h>
 #include <archive_entry.h>
 #include <nlohmann/json.hpp>
 #include <pugixml.hpp>
 
 namespace fs = std::filesystem;
 using json = nlohmann::json;
 
 #ifdef _WIN32
 #define EXPORT __declspec(dllexport)
 #else
 #define EXPORT __attribute__((visibility("default")))
 #endif
 
 #ifdef _WIN32
 #include <windows.h>
 #include <cstdio>
 
/**
 * @var g_allocatedConsole
 * @brief Tracks whether this process allocated the console window.
 * @details Used to ensure we only close the console we created and not a
 *          parent console that we attached to.
 */
bool g_allocatedConsole = false;
 
 #endif
 
/**
 * @brief Initialize a console window for logging/output on Windows.
 * @details Attempts to attach to a parent console; if none exists, allocates
 *          a new one and redirects `stdout` and `stderr` to it.
 * @note This function is a no-op on non-Windows platforms.
 */
void InitConsole()
 {
 #ifdef _WIN32
     if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
         // If no parent console is found, allocate a new one.
         AllocConsole();
         g_allocatedConsole = true;
     }
     AllocConsole();
     FILE* fpOut = nullptr;
     FILE* fpErr = nullptr;
     // Redirect stdout and stderr to the new console.
     freopen_s(&fpOut, "CONOUT$", "w", stdout);
     freopen_s(&fpErr, "CONOUT$", "w", stderr);
 #endif
 }
 
/**
 * @brief Close the console window if this process allocated it.
 * @details Posts a close message to the console window (if any) and calls
 *          `FreeConsole()`. The internal `g_allocatedConsole` flag is reset.
 * @note This function is a no-op on non-Windows platforms.
 */
void CloseConsoleIfOwned() {
 #ifdef _WIN32
     if (g_allocatedConsole) {
         HWND hwnd = GetConsoleWindow();
         if (hwnd) {
             PostMessage(hwnd, WM_CLOSE, 0, 0);
         }
         FreeConsole();
         g_allocatedConsole = false;
     }
 #endif
 }
 
/**
 * @var pluginFlags
 * @brief Global map of flag name -> value extracted from plugin XML.
 * @details Populated by `extractFlags()` and consumed by dependency checks
 *          in `areDependenciesMet()`.
 */
std::unordered_map<std::string, std::string> pluginFlags;
 
/**
 * @brief Copy data blocks from a libarchive reader to a writer.
 * @param ar Archive reader handle (source).
 * @param aw Archive writer handle (destination).
 * @return ARCHIVE_OK on success, or a libarchive error code.
 * @warning Errors are printed to `stderr` as reported by libarchive.
 */
static int copy_data(struct archive* ar, struct archive* aw) {
     const void* buff;
     size_t size;
     la_int64_t offset;
     while (true) {
         int r = archive_read_data_block(ar, &buff, &size, &offset);
         if (r == ARCHIVE_EOF)
             return ARCHIVE_OK;
         if (r < ARCHIVE_OK) {
             std::cerr << archive_error_string(ar) << "\n";
             return r;
         }
         r = archive_write_data_block(aw, buff, size, offset);
         if (r < ARCHIVE_OK) {
             std::cerr << archive_error_string(aw) << "\n";
             return r;
         }
     }
 }
 
/**
 * @brief Extract an archive using libarchive into a destination directory.
 * @param archivePath Absolute or relative path to the input archive file.
 * @param destDir Destination directory to extract into (created if missing).
 * @return 0 on success, non-zero on failure.
 * @details Preserves timestamps, permissions, ACLs and flags when possible.
 *          Any libarchive error messages are printed to `stderr`.
 */
int extractArchive(const std::string& archivePath, const std::string& destDir) {
     fs::create_directories(destDir);
     auto a = archive_read_new();
     auto ext = archive_write_disk_new();
     archive_read_support_filter_all(a);
     archive_read_support_format_all(a);
     if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
         std::cerr << "archive_read_open_filename() error: " << archive_error_string(a) << "\n";
         archive_read_free(a);
         archive_write_free(ext);
         return 1;
     }
     archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
     archive_write_disk_set_standard_lookup(ext);
 
     struct archive_entry* entry = nullptr;
     int r;
     while ((r = archive_read_next_header(a, &entry)) != ARCHIVE_EOF) {
         if (r < ARCHIVE_OK)
             std::cerr << archive_error_string(a) << "\n";
         if (r < ARCHIVE_WARN)
             return 1;
         fs::path fullOutputPath = fs::path(destDir) / archive_entry_pathname(entry);
         fs::create_directories(fullOutputPath.parent_path());
         archive_entry_set_pathname(entry, fullOutputPath.string().c_str());
         r = archive_write_header(ext, entry);
         if (r < ARCHIVE_OK)
             std::cerr << archive_error_string(ext) << "\n";
         else if (archive_entry_size(entry) > 0 && copy_data(a, ext) < ARCHIVE_OK)
             std::cerr << archive_error_string(ext) << "\n";
         if (archive_write_finish_entry(ext) < ARCHIVE_OK)
             std::cerr << archive_error_string(ext) << "\n";
     }
     archive_read_close(a);
     archive_read_free(a);
     archive_write_close(ext);
     archive_write_free(ext);
     return 0;
 }
 
/**
 * @brief Create a ZIP archive from all files under a folder.
 * @param folderPath Root folder to archive (recursively).
 * @param outputZip Destination ZIP file path to write.
 * @return 0 on success, non-zero on failure.
 * @note Directory entries are skipped; only regular files are added with
 *       default permissions.
 */
int createZip(const std::string& folderPath, const std::string& outputZip) {
     fs::create_directories(fs::path(outputZip).parent_path());
     auto a = archive_write_new();
     archive_write_set_format_zip(a);
     if (archive_write_open_filename(a, outputZip.c_str()) != ARCHIVE_OK) {
         std::cerr << "Failed to open " << outputZip << " for writing.\n";
         archive_write_free(a);
         return 1;
     }
     for (const auto& p : fs::recursive_directory_iterator(folderPath)) {
         if (fs::is_directory(p)) continue;
         auto entry = archive_entry_new();
         auto relPath = fs::relative(p.path(), folderPath).string();
         archive_entry_set_pathname(entry, relPath.c_str());
         archive_entry_set_size(entry, fs::file_size(p));
         archive_entry_set_filetype(entry, AE_IFREG);
         archive_entry_set_perm(entry, 0644);
         if (archive_write_header(a, entry) != ARCHIVE_OK) {
             std::cerr << "Could not write header for " << relPath << "\n";
             archive_entry_free(entry);
             continue;
         }
         std::ifstream ifs(p.path(), std::ios::binary);
         if (!ifs) {
             std::cerr << "Could not open file: " << p.path() << "\n";
             archive_entry_free(entry);
             continue;
         }
         char buffer[8192];
         while (ifs.read(buffer, sizeof(buffer)) || ifs.gcount())
             archive_write_data(a, buffer, ifs.gcount());
         archive_entry_free(entry);
     }
     archive_write_close(a);
     archive_write_free(a);
     return 0;
 }
 
/**
 * @brief Generate a random string by replacing '%' with random hex digits.
 * @param pattern Input pattern where '%' characters are placeholders.
 * @return Pattern with placeholders replaced by random lowercase hex digits.
 * @example generateRandom("fomod-%%%%-%%%%") -> "fomod-a3f1-09bc"
 */
std::string generateRandom(std::string pattern) {
     static const char hexDigits[] = "0123456789abcdef";
     std::random_device rd;
     std::mt19937 gen(rd());
     std::uniform_int_distribution<> dist(0, 15);
     for (auto& ch : pattern)
         if (ch == '%')
             ch = hexDigits[dist(gen)];
     return pattern;
 }
 
/**
 * @brief Extract condition flags from a plugin XML node.
 * @param pluginNode Plugin XML node containing a `<conditionFlags>` section.
 * @details Populates the global `pluginFlags` map with name/value pairs. The
 *          values are used to evaluate `<dependencies>` elsewhere.
 */
void extractFlags(const pugi::xml_node& pluginNode) {
     auto conditionFlagsNode = pluginNode.child("conditionFlags");
     if (conditionFlagsNode) {
         for (auto& flagNode : conditionFlagsNode.children("flag")) {
             std::string flagName = flagNode.attribute("name").value();
             std::string flagValue = flagNode.text().as_string(); // "On" or other values
             if (!flagName.empty()) {
                 pluginFlags[flagName] = flagValue;
             }
         }
     }
 }
 
/**
 * @typedef LogCallback
 * @brief Function pointer type for log message callbacks.
 * @details Receives a NUL-terminated UTF-8 message string.
 */
typedef void (*LogCallback)(const char*);
 
 // Global variable to hold the callback.
 static LogCallback g_logCallback = nullptr;
 
/**
 * @brief Log a message via callback (if set) and to stdout.
 * @param message Message content (prefixed with "[mo2si] ").
 * @note If a callback is registered via `setLogCallback`, it is invoked first.
 */
void log(std::string message) {
     if (g_logCallback) {
         g_logCallback((std::string("[mo2si] ") + message).c_str());
     }
     std::cout << "[mo2si] " << message << std::endl;
 }
 
/**
 * @brief Return a lowercase copy of the input string.
 * @param input ASCII/UTF-8 string to transform.
 * @return Lowercased copy of input.
 */
std::string to_lower(const std::string& input) {
     std::string output = input;
     std::transform(output.begin(), output.end(), output.begin(),
         [](unsigned char c) { return std::tolower(c); });
     return output;
 }
 
/**
 * @brief Evaluate flag dependencies defined in an XML `<dependencies>` node.
 * @param dependenciesNode XML node with `@operator` and `<flagDependency>` entries.
 * @return true if dependencies evaluate to satisfied, false otherwise.
 * @details Supports `operator="And"` and `operator="Or"`. Looks up flag
 *          values from the global `pluginFlags` map.
 */
bool areDependenciesMet(const pugi::xml_node& dependenciesNode) {
     std::string operatorType = dependenciesNode.attribute("operator").as_string("And");
     bool result = (operatorType == "And"); // "And" starts as true, "Or" starts as false
     for (auto& depNode : dependenciesNode.children("flagDependency")) {
         std::string flagName = depNode.attribute("flag").value();
         std::string requiredValue = depNode.attribute("value").value();
         bool flagExists = pluginFlags.find(flagName) != pluginFlags.end();
         bool flagMatches = flagExists && (pluginFlags[flagName] == requiredValue);
         if (operatorType == "And")
             result &= flagMatches;
         else if (operatorType == "Or")
             result |= flagMatches;
     }
     return result;
 }
 
 extern "C" {
 
   /**
    * @brief Set the global log callback used by `log()`.
    * @param callback Function pointer invoked for each log message.
    * @note Passing `nullptr` disables the callback and logs only to stdout.
    */
    EXPORT void setLogCallback(LogCallback callback) {
         g_logCallback = callback;
     }
 
   /**
    * @brief Install a mod from an input archive into a target directory.
    *
    * @details
    * - Extracts the archive to a temporary folder.
    * - If a `fomod/` folder is present, parses `ModuleConfig.xml` and optional
    *   JSON to resolve required/optional file mappings and conditional installs.
    * - If no `fomod/` is present, attempts to detect a main mod folder and
    *   copies its contents, using `moduleName` from JSON to disambiguate when
    *   multiple candidates exist. Falls back to copying the archive root.
    * - Copies the final output into the provided mod directory.
    *
    * @param archivePath Path to the input archive file.
    * @param modPath Destination mod directory where files are installed.
    * @return On success, pointer to a static string containing the destination
    *         path. On error, pointer to a static string describing the error.
    * @note Designed for use via Python `ctypes`; the returned pointer remains
    *       valid after the call because it points to a static buffer.
    */
    EXPORT const char* install(const char* archivePath, const char* modPath) {
 
         InitConsole();
         log("Initialization finished");
 
         auto parseJson = [&](const std::string& path) -> json {
             std::ifstream f(path);
             if (!f)
                 throw std::runtime_error("Cannot open JSON config: " + path);
             json j; f >> j;
             log(std::format("Loaded JSON: {}", path));
             return j;
             };
 
         auto parseXml = [&](const std::string& path) -> pugi::xml_document {
             pugi::xml_document doc;
             auto res = doc.load_file(path.c_str());
             if (!res)
                 throw std::runtime_error("Cannot parse XML (" + std::string(res.description()) + ")");
             log(std::format("Loaded XML: {}", path));
             return doc;
             };
 
         auto copyFolder = [&](const fs::path& src, const fs::path& dst) {
             if (!fs::exists(src)) {
                 log(std::format("Missing folder: {}", src.string()));
                 return;
             }
             fs::create_directories(dst);
             for (const auto& e : fs::recursive_directory_iterator(src))
                 try {
                 fs::copy(e.path(), dst / fs::relative(e.path(), src), fs::copy_options::overwrite_existing);
             }
             catch (const fs::filesystem_error& ex) {
                 log(std::format("Copy error: {}", std::string(ex.what())));
             }
             };
 
         auto copyFile = [&](const fs::path& src, const fs::path& dst) {
             if (!fs::exists(src)) {
                 log(std::format("Missing file: {}", src.string()));
                 return;
             }
             fs::create_directories(dst.parent_path());
             try {
                 fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
             }
             catch (const fs::filesystem_error& ex) {
                 log(std::format("Copy error: {}", std::string(ex.what())));
             }
             };
 
         static std::string outputPath;
         try {
             std::string inputArchive(archivePath);
             std::string modDirectory(modPath);
             auto baseTempDir = fs::temp_directory_path();
             auto newTempDir = baseTempDir / generateRandom("fomod-%%%%-%%%%");
             fs::create_directories(newTempDir / "archive");
 
             auto inputPath = fs::path(inputArchive);
             std::string jPath = (inputPath.parent_path() / (inputPath.stem().string() + ".json")).string();
 
             if (extractArchive(inputArchive, (newTempDir / "archive").string()) != 0)
                 throw std::runtime_error("Failed to extract archive: " + inputArchive);
 
             log(std::format("Archive extracted to '{}'", newTempDir.string()));
 
             fs::path archiveRoot = newTempDir / "archive";
             fs::path fomodFolder;
             for (const auto& p : fs::recursive_directory_iterator(archiveRoot))
                 if (p.is_directory() && to_lower(p.path().filename().string()) == "fomod" && fs::exists(p.path() / "ModuleConfig.xml")) {
                     fomodFolder = p.path();
                     break;
                 }
 
             if (fomodFolder.empty()) {
                 // --- begin replacement for nested-mod-structure block ---
 
                 log(std::format("No 'fomod' folder found; checking for nested mod structure in: {}", archiveRoot.string()));
 
                 // Load moduleName (lower-cased) if the JSON exists
                 std::string moduleNameLower;
                 if (fs::exists(jPath)) {
                     json configJson = parseJson(jPath);
                     if (configJson.contains("moduleName") && configJson["moduleName"].is_string()) {
                         moduleNameLower = to_lower(configJson["moduleName"].get<std::string>());
                         log(std::format("Detected moduleName '{}' in JSON", moduleNameLower));
                     }
                 }
 
                 // 1. Find all subfolders with a main-mod structure
                 std::vector<fs::path> mainModFolders;
                 for (const auto& entry : fs::directory_iterator(archiveRoot)) {
                     if (!entry.is_directory()) continue;
                     auto p = entry.path();
                     bool hasSKSE = fs::exists(p / "SKSE");
                     bool hasMeshes = fs::exists(p / "meshes");
                     bool hasTextures = fs::exists(p / "textures");
                     bool hasInterface = fs::exists(p / "interface");
                     bool hasSound = fs::exists(p / "sound");
                     bool hasScripts = fs::exists(p / "scripts");
                     bool hasSeq = fs::exists(p / "seq");
                     if (hasSKSE || hasMeshes || hasTextures || hasInterface || hasSound || hasScripts || hasSeq) {
                         mainModFolders.push_back(p);
                         log(std::format("   candidate mod folder: '{}'", p.filename().string()));
                     }
                 }
 
                 if (!mainModFolders.empty()) {
                     // 2a. Only one candidate: copy it directly
                     if (mainModFolders.size() == 1) {
                         auto& singleMod = mainModFolders.front();
                         log(std::format("Only one mod folder '{}' found; copying it", singleMod.filename().string()));
                         for (const auto& entry : fs::directory_iterator(singleMod)) {
                             if (fs::is_directory(entry))
                                 copyFolder(entry.path(), fs::path(modDirectory) / entry.path().filename());
                             else
                                 copyFile(entry.path(), fs::path(modDirectory) / entry.path().filename());
                         }
                     }
                     // 2b. Multiple candidates: use moduleName to pick exactly one
                     else {
                         if (moduleNameLower.empty()) {
                             throw std::runtime_error("Multiple mod folders detected but no moduleName in JSON to disambiguate.");
                         }
                         std::vector<fs::path> matches;
                         for (auto& p : mainModFolders) {
                             if (to_lower(p.filename().string()) == moduleNameLower) {
                                 matches.push_back(p);
                                 log(std::format("     matches moduleName: '{}'", p.filename().string()));
                             }
                         }
                         if (matches.size() != 1) {
                             throw std::runtime_error(
                                 matches.empty()
                                 ? ("moduleName '" + moduleNameLower + "' did not match any folder.")
                                 : ("moduleName '" + moduleNameLower + "' matched multiple folders.")
                             );
                         }
                         auto& chosen = matches.front();
                         log(std::format("Copying contents of chosen mod folder '{}'", chosen.filename().string()));
                         for (const auto& entry : fs::directory_iterator(chosen)) {
                             if (fs::is_directory(entry))
                                 copyFolder(entry.path(), fs::path(modDirectory) / entry.path().filename());
                             else
                                 copyFile(entry.path(), fs::path(modDirectory) / entry.path().filename());
                         }
                     }
 
                     outputPath = modDirectory;
                     CloseConsoleIfOwned();
                     return outputPath.c_str();
                 }
 
                 // 3. Fallback: no main-mod structure at all
                 log(std::format("No nested mod structure detected; copying all files from archive root to mod directory: {}", modDirectory));
                 copyFolder(archiveRoot, fs::path(modDirectory));
                 outputPath = modDirectory;
                 CloseConsoleIfOwned();
                 return outputPath.c_str();
 
                 // --- end replacement ---
             }
 
             // If a 'fomod' folder was found, proceed with normal XML/JSON-driven install...
             std::string xPath = (fomodFolder / "ModuleConfig.xml").string();
             std::string srcBase = fomodFolder.parent_path().string();
             std::string dstBase = (newTempDir / "unfomod").string();
             fs::create_directories(newTempDir / "unfomod");
 
             json config = parseJson(jPath);
             pugi::xml_document doc = parseXml(xPath);
 
             pugi::xpath_node_set requiredNodes = doc.select_nodes("//requiredInstallFiles/*");
             if (!requiredNodes.empty()) {
                 log("Processing required install files from XML...");
                 for (auto& node : requiredNodes) {
                     pugi::xml_node reqNode = node.node();
                     std::string nodeName = reqNode.name();
                     log(std::format("Mapped: {} -> {}",
                         (fs::path(srcBase) / reqNode.attribute("source").value()).string(),
                         (fs::path(dstBase) / reqNode.attribute("destination").value()).string()));
                     if (nodeName == "file")
                         copyFile(fs::path(srcBase) / reqNode.attribute("source").value(),
                             fs::path(dstBase) / reqNode.attribute("destination").value());
                     else if (nodeName == "folder")
                         copyFolder(fs::path(srcBase) / reqNode.attribute("source").value(),
                             fs::path(dstBase) / reqNode.attribute("destination").value());
                     else
                         log(std::format("Unknown node in requiredInstallFiles: {}", nodeName));
                 }
             }
             else {
                 log("No required install files found in XML");
             }
 
             if (config.contains("installFiles") && config["installFiles"].is_array()) {
                 for (auto& group : config["installFiles"]) {
                     std::string installStepName = group.value("installStep", "");
                     std::string groupName = group.value("group", "");
                     std::string pluginName = group["plugin"];
                     std::string xpath = "(//installStep[normalize-space(@name)=\"" + installStepName + "\"]//optionalFileGroups//group|"
                         "//optionalFileGroups//group[normalize-space(@name)=\"" + groupName + "\"])"
                         "//plugins/plugin[normalize-space(@name)=\"" + pluginName + "\"]";
                     log(std::format("Using XPath: {}", xpath));
                     for (auto& node : doc.select_nodes(xpath.c_str())) {
                         auto pluginNode = node.node();
                         log(std::format("Plugin found: \"{}\"", pluginNode.attribute("name").as_string()));
                         extractFlags(pluginNode);
                         pugi::xml_node installStepNode = pluginNode;
                         while (installStepNode && std::string(installStepNode.name()) != "installStep")
                             installStepNode = installStepNode.parent();
                         if (installStepNode) {
                             pugi::xml_node visibleNode = installStepNode.child("visible");
                             if (visibleNode && !areDependenciesMet(visibleNode)) {
                                 log(std::format("Skipping plugin \"{}\" due to unmet dependency flags",
                                     pluginNode.attribute("name").as_string()));
                                 continue;
                             }
                         }
                         pugi::xml_node filesNode = pluginNode.child("files");
                         if (filesNode) {
                             for (pugi::xml_node fileNode : filesNode.children()) {
                                 std::string nodeName = fileNode.name();
                                 log(std::format("Mapped: {} -> {}",
                                     (fs::path(srcBase) / fileNode.attribute("source").value()).string(),
                                     (fs::path(dstBase) / fileNode.attribute("destination").value()).string()));
                                 if (nodeName == "file") {
                                     copyFile(fs::path(srcBase) / fileNode.attribute("source").value(),
                                         fs::path(dstBase) / fileNode.attribute("destination").value());
                                 }
                                 else if (nodeName == "folder") {
                                     copyFolder(fs::path(srcBase) / fileNode.attribute("source").value(),
                                         fs::path(dstBase) / fileNode.attribute("destination").value());
                                 }
                                 else {
                                     log(std::format("Unknown node: {}", nodeName));
                                 }
                             }
                         }
                     }
                 }
             }
             else {
                 log("No valid installFiles in JSON");
             }
 
             for (auto& patternNode : doc.select_nodes("//conditionalFileInstalls/patterns/pattern")) {
                 auto pattern = patternNode.node();
                 auto dependenciesNode = pattern.child("dependencies");
                 if (dependenciesNode && !areDependenciesMet(dependenciesNode)) {
                     continue;
                 }
                 log("Dependency found");
                 for (auto& fileNode : pattern.child("files").children()) {
                     std::string nodeName = fileNode.name();
                     std::string source = fileNode.attribute("source").value();
                     std::string destination = fileNode.attribute("destination").value();
                     log(std::format("Mapped: {} -> {}",
                         (fs::path(srcBase) / source).string(),
                         (fs::path(dstBase) / destination).string()));
                     if (nodeName == "file") {
                         copyFile(fs::path(srcBase) / source, fs::path(dstBase) / destination);
                     }
                     else if (nodeName == "folder") {
                         copyFolder(fs::path(srcBase) / source, fs::path(dstBase) / destination);
                     }
                     else {
                         log(std::format("Unknown node: {}", nodeName));
                     }
                 }
             }
 
             log(std::format("FOMOD installation steps completed in '{}'", newTempDir.string()));
             fs::copy_file(jPath, fs::path(dstBase) / "mo2si.json", fs::copy_options::overwrite_existing);
 
             log(std::format("Copying unfomod files to mod directory: {}", modDirectory));
             copyFolder(fs::path(dstBase), fs::path(modDirectory));
 
             fs::remove_all(newTempDir);
             outputPath = modDirectory;
             CloseConsoleIfOwned();
             return outputPath.c_str();
         }
         catch (const std::exception& e) {
             log(std::format("Fatal error: {}", std::string(e.what())));
             CloseConsoleIfOwned();
             return e.what();
         }
     }
 }
 