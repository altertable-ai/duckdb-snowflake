#pragma once

#include <string>
#include <vector>

namespace duckdb {
namespace snowflake {

// Directories searched for ADBC driver manifests (snowflake.toml), in priority
// order: ADBC_DRIVER_PATH entries (colon-separated, semicolon on Windows), the
// active conda prefix (dbc installs there when a conda env is active), the
// per-user config dir, then the system dir. Windows registry locations
// (HKEY_*\SOFTWARE\ADBC\Drivers) are a follow-up — see issue #50.
std::vector<std::string> GetAdbcManifestDirs();

// Read <dir>/snowflake.toml and return the shared-library path it names for the
// current platform, or "" when the manifest is absent or names none. Targeted
// TOML read, not a full parser: handles `shared = '<path>'` under [Driver] and
// platform-keyed entries under [Driver.shared] (dbc writes e.g. macos_arm64;
// DuckDB-style osx_* keys are accepted as aliases), single- or double-quoted.
// Best-effort by design: a malformed manifest yields "" and the caller falls
// through to the next search location, never a hard failure.
std::string ResolveDriverFromManifestDir(const std::string &dir);

} // namespace snowflake
} // namespace duckdb
