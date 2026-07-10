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

protected:
	//! Load tables for this schema
	void LoadEntries(ClientContext &context) override;

private:
	SnowflakeSchemaEntry &schema;
	SnowflakeConfig config;
	const string schema_name;
};
} // namespace snowflake
} // namespace duckdb
