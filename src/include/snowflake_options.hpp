#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/enums/access_mode.hpp"
#include <map>

namespace duckdb {
namespace snowflake {

struct SnowflakeOptions {
	//! How to handle database access (READ_ONLY or READ_WRITE)
	AccessMode access_mode = AccessMode::READ_WRITE;

	//! Enable filter and projection pushdown to Snowflake.
	//! When enabled, DuckDB will modify queries to push WHERE filters and column
	//! selections to Snowflake. Default is false for backward compatibility -
	//! users must explicitly opt-in.
	bool enable_pushdown = false;

	//! Enable GEOGRAPHY/GEOMETRY column detection via catalog metadata.
	//! When enabled, the extension queries INFORMATION_SCHEMA.COLUMNS to detect
	//! geo columns, wraps them with ST_ASWKB() for WKB output, and annotates
	//! the Arrow schema with geoarrow.wkb so DuckDB imports as native GEOMETRY.
	//! Default is false because it adds an extra metadata query per table scan.
	//! Workaround for Snowflake not exposing the original geo type in query
	//! response metadata (SNOWFLAKE_TYPE='object' is ambiguous with VARIANT).
	bool enable_geo = false;

	//! Whether to treat table and column names from Snowflake as case-sensitive.
	//! If false (default), names will be converted to lowercase to match DuckDB's
	//! typical behavior.
	// bool case_sensitive_names = false;

	//! How long (in seconds) to cache Snowflake table metadata.
	//! A value of 0 disables metadata caching.
	// uint32_t metadata_cache_ttl_seconds = 3600;

	//! Custom parameters to set on the Snowflake session upon connection.
	// std::map<string, string> session_parameters;
};

} // namespace snowflake
} // namespace duckdb
