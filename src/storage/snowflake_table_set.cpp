#include "storage/snowflake_table_set.hpp"
#include "storage/snowflake_table_entry.hpp"
#include "snowflake_client_manager.hpp"
#include "snowflake_debug.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {
namespace snowflake {

static const vector<SnowflakeColumn> *FindTableColumns(const unordered_map<string, vector<SnowflakeColumn>> &columns,
                                                       const string &table_name) {
	auto exact = columns.find(table_name);
	if (exact != columns.end()) {
		return &exact->second;
	}
	for (const auto &entry : columns) {
		if (StringUtil::CIEquals(entry.first, table_name)) {
			return &entry.second;
		}
	}
	return nullptr;
}

void SnowflakeTableSet::LoadEntries(ClientContext &context) {
	auto lease = SnowflakeClientManager::GetInstance().Acquire(config);
	auto table_names = lease->ListTables(context, schema_name);

	// One INFORMATION_SCHEMA.COLUMNS roundtrip for the whole schema. This fills
	// TableCatalogEntry::columns before publication so DESCRIBE / duckdb_columns()
	// work without a table scan. Arrow bind schema stays lazy (ViaQuery) so we
	// do not regress #44 or GeoArrow tagging.
	unordered_map<string, vector<SnowflakeColumn>> columns_by_table;
	try {
		columns_by_table = lease->ListColumns(context, schema_name);
	} catch (std::exception &ex) {
		DPRINT("SnowflakeTableSet::LoadEntries: ListColumns failed for %s: %s\n", schema_name.c_str(), ex.what());
	}

	for (const auto &table_name : table_names) {
		CreateTableInfo info;
		info.table = table_name;
		info.schema = schema_name;
		info.catalog = schema.catalog.GetName();
		info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		info.temporary = false;

		if (auto table_columns = FindTableColumns(columns_by_table, table_name)) {
			for (const auto &col : *table_columns) {
				info.columns.AddColumn(ColumnDefinition(col.name, col.type));
			}
		}

		auto table_entry = make_uniq<SnowflakeTableEntry>(schema.catalog, schema, info, config);
		entries[table_name] = std::move(table_entry);
	}
}
} // namespace snowflake
} // namespace duckdb
