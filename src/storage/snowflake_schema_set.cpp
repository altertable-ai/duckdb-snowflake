#include "storage/snowflake_schema_set.hpp"
#include "storage/snowflake_table_set.hpp"
#include "snowflake_client_manager.hpp"
#include "snowflake_debug.hpp"

namespace duckdb {
namespace snowflake {
void SnowflakeSchemaSet::LoadEntries(ClientContext &context) {
	DPRINT("SnowflakeSchemaSet::LoadEntries called\n");
	auto lease = SnowflakeClientManager::GetInstance().Acquire(config);
	vector<string> schema_names = lease->ListSchemas(context);
	DPRINT("Got %zu schemas from ListSchemas\n", schema_names.size());

	for (const auto &schema_name : schema_names) {
		DPRINT("Creating schema entry for: %s\n", schema_name.c_str());
		auto schema_info = make_uniq<CreateSchemaInfo>();
		schema_info->SetSchema(Identifier(schema_name));
		auto schema_entry = make_uniq<SnowflakeSchemaEntry>(catalog, schema_name, *schema_info, config);
		entries.insert({schema_name, std::move(schema_entry)});
	}
	DPRINT("SnowflakeSchemaSet::LoadEntries completed with %zu entries\n", entries.size());
}

void SnowflakeSchemaSet::PreloadTables(ClientContext &context) {
	lock_guard<mutex> lock(preload_lock);
	if (tables_preloaded) {
		return;
	}
	tables_preloaded = true;

	auto lease = SnowflakeClientManager::GetInstance().Acquire(config);
	unordered_map<string, vector<string>> tables_by_schema;
	try {
		tables_by_schema = lease->ListAllTables(context);
	} catch (std::exception &ex) {
		DPRINT("SnowflakeSchemaSet::PreloadTables: ListAllTables failed: %s\n", ex.what());
		return;
	}
	unordered_map<string, unordered_map<string, vector<SnowflakeColumn>>> columns_by_schema;
	try {
		columns_by_schema = lease->ListAllColumns(context);
	} catch (std::exception &ex) {
		DPRINT("SnowflakeSchemaSet::PreloadTables: ListAllColumns failed: %s\n", ex.what());
	}

	const vector<string> no_tables;
	const unordered_map<string, vector<SnowflakeColumn>> no_columns;
	Scan(context, [&](CatalogEntry &entry) {
		auto tables = tables_by_schema.find(entry.name.GetIdentifierName());
		auto columns = columns_by_schema.find(entry.name.GetIdentifierName());
		entry.Cast<SnowflakeSchemaEntry>().PreloadTables(tables == tables_by_schema.end() ? no_tables : tables->second,
		                                                 columns == columns_by_schema.end() ? no_columns
		                                                                                    : columns->second);
	});
}
} // namespace snowflake
} // namespace duckdb
