#pragma once

// File access for the studio's own data (projects, presets, collections).
// Geode 5 exposes std::filesystem::path while Geode v6 (GD 2.2091) uses
// asp::fs::Path; everything here takes and returns std::filesystem::path so
// the rest of the mod builds against both.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace files {

std::filesystem::path saveDir();
std::filesystem::path configDir();

// Creates the directory (and parents) if needed and returns it.
std::filesystem::path ensureDir(std::filesystem::path const& dir);

std::optional<std::string> readText(std::filesystem::path const& file);

// Writes to a temporary file first, then replaces the target, so a crash
// mid-write never leaves a truncated project behind.
bool writeTextSafe(std::filesystem::path const& file, std::string const& text, std::string* error = nullptr);

// Regular files directly inside `dir`, sorted by name.
std::vector<std::filesystem::path> listFiles(std::filesystem::path const& dir);

// UTF-8 file name (std::filesystem::path::string() can throw on Windows).
std::string fileName(std::filesystem::path const& file);

void openFolder(std::filesystem::path const& dir);

}  // namespace files
