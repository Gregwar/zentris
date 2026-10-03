#pragma once
// Small portability layer (Linux, macOS, Windows): paths, finding and running external tools.
#include <cstdio>
#include <filesystem>
#include <string>

namespace platform {

std::filesystem::path executableDir();  // folder of the running binary
std::filesystem::path homeDir();
std::filesystem::path cacheDir();       // per-user cache root (XDG / ~/Library/Caches / %LOCALAPPDATA%)
std::filesystem::path dataDir();        // per-user data root (XDG / ~/Library/Application Support / %APPDATA%)

// Full path of an executable found in PATH, or "" (".exe" is implied on Windows).
std::string findExecutable(const std::string& name);

// Quotes one argument for the platform shell used by run()/openRead().
std::string quoteArg(const std::string& arg);

// Runs a command line with stdout/stderr discarded; returns the exit code.
int run(const std::string& cmd);
// Runs a command line and returns a stream on its stdout (stderr discarded); close with closeRead().
FILE* openRead(const std::string& cmd);
int closeRead(FILE* f);

// Makes stdout line-buffered where supported (safe on every platform).
void lineBufferStdout();

} // namespace platform
