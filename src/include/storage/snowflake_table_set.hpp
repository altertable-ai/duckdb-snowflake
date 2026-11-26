#pragma once

#include "snowflake_catalog_set.hpp"
#include "snowflake_client.hpp"
#include "snowflake_schema_entry.hpp"

namespace duckdb {
namespace snowflake {

//! SnowflakeTableSet represents a set of tables in Snowflake
class SnowflakeTableSet : public SnowflakeCatalogSet {
public:
	SnowflakeTableSet(SnowflakeSchemaEntry &schema, SnowflakeConfig config, const string &schema_name)
	    : SnowflakeCatalogSet(schema.catalog), schema(schema), config(std::move(config)), schema_name(schema_name) {
	}

	//! Fill this schema's tables from a database-wide listing, unless already loaded
	void Preload(const vector<string> &table_names,
	             const unordered_map<string, vector<SnowflakeColumn>> &columns_by_table);

protected:
	//! Load tables for this schema
	void LoadEntries(ClientContext &context) override;

private:
	void AddEntries(const vector<string> &table_names,
	                const unordered_map<string, vector<SnowflakeColumn>> &columns_by_table);

	SnowflakeSchemaEntry &schema;
	SnowflakeConfig config;
	const string schema_name;
};
} // namespace snowflake
} // namespace duckdb
