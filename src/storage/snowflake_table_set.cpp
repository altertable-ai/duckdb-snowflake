#include "storage/snowflake_table_set.hpp"
#include "storage/snowflake_table_entry.hpp"
#include "snowflake_debug.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"

namespace duckdb {
namespace snowflake {
void SnowflakeTableSet::LoadEntries(ClientContext &context) {
	auto table_names = client->ListTables(context, schema_name);

	for (const auto &table_name : table_names) {
		CreateTableInfo info;
		info.table = table_name;
		info.schema = schema_name;
		info.catalog = schema.catalog.GetName();
		info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		info.temporary = false;

		auto table_entry = make_uniq<SnowflakeTableEntry>(schema.catalog, schema, info, client);

		// Resolve columns + cached Arrow schema BEFORE inserting into `entries`.
		// Once inserted, the entry is reachable from other threads via Scan/
		// GetEntry/duckdb_columns/plan binding, and any further mutation of
		// `columns` would race with those readers. Doing the work here keeps the
		// catalog set's `load_lock` (held by SnowflakeCatalogSet::TryLoadEntries)
		// as the publication barrier — the entry is never visible with a
		// half-built columns list.
		try {
			table_entry->LoadColumnsAndSchema(context);
		} catch (std::exception &ex) {
			// One broken Snowflake table shouldn't break the whole catalog list.
			// Skip it; users can investigate via Snowflake directly.
			DPRINT("SnowflakeTableSet::LoadEntries: skipping %s.%s: %s\n", schema_name.c_str(), table_name.c_str(),
			       ex.what());
			continue;
		}

		entries[table_name] = std::move(table_entry);
	}
}
} // namespace snowflake
} // namespace duckdb
