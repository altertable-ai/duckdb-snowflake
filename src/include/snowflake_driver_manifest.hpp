#pragma once

#include <string>
#include <vector>

namespace duckdb {
namespace snowflake {

// Directories searched for ADBC driver manifests (snowflake.toml), split to
// preserve the spec's search order around the Windows registry (which sits
// between the environment-driven dirs and the per-OS dirs, see
// ResolveDriverFromRegistry):
//   env dirs      - ADBC_DRIVER_PATH entries (colon-separated, semicolon on
//                   Windows) and the active conda prefix (dbc installs there
//                   when a conda env is active)
//   platform dirs - the per-user config dir, then the system dir
std::vector<std::string> GetAdbcManifestEnvDirs();
std::vector<std::string> GetAdbcManifestPlatformDirs();

// Both lists concatenated in search order; used for error reporting.
std::vector<std::string> GetAdbcManifestDirs();

// Read <dir>/snowflake.toml and return the shared-library path it names for the
// current platform, or "" when the manifest is absent or names none. Targeted
// TOML read, not a full parser: handles `shared = '<path>'` under [Driver] and
// platform-keyed entries under [Driver.shared] (dbc writes e.g. macos_arm64;
// DuckDB-style osx_* keys are accepted as aliases), single- or double-quoted.
// Best-effort by design: a malformed manifest yields "" and the caller falls
// through to the next search location, never a hard failure.
std::string ResolveDriverFromManifestDir(const std::string &dir);

// Windows registry manifests: HKEY_CURRENT_USER (user level) or
// HKEY_LOCAL_MACHINE (system level) SOFTWARE\ADBC\Drivers\snowflake, value
// "driver" = path to the shared library. This is how `dbc install snowflake`
// registers the driver on Windows — it writes no .toml there at all — and per
// the ADBC manifest spec the user-level registry key is consulted before the
// %LOCALAPPDATA% manifest dir, the system-level key after it. Returns "" when
// the key is absent, on error, and always on non-Windows platforms.
std::string ResolveDriverFromRegistry(bool system_level);

} // namespace snowflake
} // namespace duckdb
