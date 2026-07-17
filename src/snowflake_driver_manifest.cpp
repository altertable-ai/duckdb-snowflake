#include "snowflake_driver_manifest.hpp"
#include "snowflake_debug.hpp"

#include <cstdlib>
#include <fstream>

namespace duckdb {
namespace snowflake {

// Platform tuple as spelled in ADBC driver manifests (dbc writes macos_arm64,
// linux_amd64, ...). DuckDB's own osx_* spelling is accepted as an alias.
#if defined(_WIN32)
static const char *const MANIFEST_OS = "windows";
static const char *const MANIFEST_OS_ALIAS = "windows";
#elif defined(__APPLE__)
static const char *const MANIFEST_OS = "macos";
static const char *const MANIFEST_OS_ALIAS = "osx";
#else
static const char *const MANIFEST_OS = "linux";
static const char *const MANIFEST_OS_ALIAS = "linux";
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
static const char *const MANIFEST_ARCH = "arm64";
#else
static const char *const MANIFEST_ARCH = "amd64";
#endif

static std::string TrimWs(const std::string &s) {
	auto begin = s.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) {
		return "";
	}
	auto end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

// Extract a TOML string value: quoted (single or double, possibly followed by a
// comment) or bare-until-comment. Returns "" for anything else.
static std::string ExtractTomlString(const std::string &raw) {
	auto value = TrimWs(raw);
	if (value.empty()) {
		return "";
	}
	if (value[0] == '\'' || value[0] == '"') {
		auto close = value.find(value[0], 1);
		if (close == std::string::npos) {
			return "";
		}
		return value.substr(1, close - 1);
	}
	auto hash = value.find('#');
	if (hash != std::string::npos) {
		value = TrimWs(value.substr(0, hash));
	}
	return value;
}

std::vector<std::string> GetAdbcManifestDirs() {
	std::vector<std::string> dirs;

	// 1. Explicit override: a list of manifest directories.
	if (const char *adbc_path = std::getenv("ADBC_DRIVER_PATH")) {
#if defined(_WIN32)
		const char sep = ';';
#else
		const char sep = ':';
#endif
		std::string paths(adbc_path);
		size_t start = 0;
		while (start <= paths.size()) {
			auto end = paths.find(sep, start);
			auto part = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (!TrimWs(part).empty()) {
				dirs.push_back(TrimWs(part));
			}
			if (end == std::string::npos) {
				break;
			}
			start = end + 1;
		}
	}

	// 2. Active conda environment: dbc installs here when a conda env is active.
	if (const char *conda = std::getenv("CONDA_PREFIX")) {
		if (*conda) {
			dirs.push_back(std::string(conda) + "/etc/adbc/drivers");
		}
	}

	// 3. Per-user config dir, then 4. system dir.
	const char *home = std::getenv("HOME");
#if defined(_WIN32)
	if (const char *localappdata = std::getenv("LOCALAPPDATA")) {
		if (*localappdata) {
			dirs.push_back(std::string(localappdata) + "\\ADBC\\Drivers");
		}
	}
#elif defined(__APPLE__)
	if (home && *home) {
		dirs.push_back(std::string(home) + "/Library/Application Support/ADBC/Drivers");
	}
	dirs.emplace_back("/Library/Application Support/ADBC/Drivers");
#else
	const char *xdg = std::getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) {
		dirs.push_back(std::string(xdg) + "/adbc/drivers");
	} else if (home && *home) {
		dirs.push_back(std::string(home) + "/.config/adbc/drivers");
	}
	dirs.emplace_back("/etc/adbc/drivers");
#endif
	return dirs;
}

std::string ResolveDriverFromManifestDir(const std::string &dir) {
	std::ifstream manifest(dir + "/snowflake.toml");
	if (!manifest.is_open()) {
		return "";
	}

	const std::string platform_key = std::string(MANIFEST_OS) + "_" + MANIFEST_ARCH;
	const std::string platform_alias = std::string(MANIFEST_OS_ALIAS) + "_" + MANIFEST_ARCH;
	std::string plain_shared;    // [Driver] shared = '<path>'
	std::string platform_shared; // [Driver.shared] <platform> = '<path>'
	std::string section;
	std::string line;
	while (std::getline(manifest, line)) {
		auto trimmed = TrimWs(line);
		if (trimmed.empty() || trimmed[0] == '#') {
			continue;
		}
		if (trimmed[0] == '[') {
			auto close = trimmed.find(']');
			section = close == std::string::npos ? "" : trimmed.substr(1, close - 1);
			continue;
		}
		auto eq = trimmed.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		auto key = TrimWs(trimmed.substr(0, eq));
		auto value = ExtractTomlString(trimmed.substr(eq + 1));
		if (value.empty()) {
			continue;
		}
		if (section == "Driver" && key == "shared") {
			plain_shared = value;
		} else if (section == "Driver.shared" && (key == platform_key || key == platform_alias)) {
			platform_shared = value;
		}
	}

	auto resolved = !platform_shared.empty() ? platform_shared : plain_shared;
	if (!resolved.empty()) {
		DPRINT("Driver manifest %s/snowflake.toml resolves to: %s\n", dir.c_str(), resolved.c_str());
	}
	return resolved;
}

} // namespace snowflake
} // namespace duckdb
