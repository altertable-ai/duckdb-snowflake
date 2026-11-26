#pragma once

#include "snowflake_catalog_set.hpp"
#include "snowflake_schema_entry.hpp"
#include "snowflake_client.hpp"

namespace duckdb {
namespace snowflake {
class SnowflakeSchemaSet : public SnowflakeCatalogSet {
public:
	SnowflakeSchemaSet(Catalog &catalog, SnowflakeConfig config)
	    : SnowflakeCatalogSet(catalog), config(std::move(config)) {
	}

	//! Fetches all schemas from Snowflake and creates SnowflakeSchemaEntry
	//! objects for each
	void LoadEntries(ClientContext &context) override;

	//! Loads every schema's tables and columns in two database-wide roundtrips instead of two per
	//! schema. Schemas whose tables are already loaded keep them; on failure, each schema still
	//! loads lazily on its own.
	void PreloadTables(ClientContext &context);

private:
	SnowflakeConfig config;
	mutex preload_lock;
	bool tables_preloaded = false;
};
} // namespace snowflake
} // namespace duckdb
