#include "storage/snowflake_table_entry.hpp"
#include "storage/snowflake_catalog.hpp"
#include "snowflake_debug.hpp"
#include "snowflake_client_manager.hpp"
#include "snowflake_scan.hpp"
#include "snowflake_arrow_utils.hpp"
#include "snowflake_query_builder.hpp"
#include "duckdb/common/arrow/nanoarrow/nanoarrow.h"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/function/table/arrow.hpp"
#include "duckdb/function/table/arrow/arrow_duck_schema.hpp"

namespace duckdb {
namespace snowflake {

//! Deep-copy a previously cached ArrowSchema into `dst`. Uses nanoarrow's
//! ArrowSchemaDeepCopy so `dst` ends up with its own release-callback-owned
//! memory, decoupled from the cached source. `src` is taken by non-const ref
//! because the nanoarrow C signature requires a mutable pointer even though
//! the operation only reads from it.
static void CloneCachedSchema(ArrowSchema &src, ArrowSchema &dst) {
	auto rc = duckdb_nanoarrow::ArrowSchemaDeepCopy(&src, &dst);
	if (rc != 0) {
		throw IOException("Failed to deep-copy cached Snowflake Arrow schema (nanoarrow rc=%d)", rc);
	}
}

//! Build the `SELECT *` query that SnowflakeGetArrowSchema/ADBC needs to
//! resolve the table's Arrow schema. Shared between LoadColumnsAndSchema (one
//! roundtrip at catalog-list time) and GetScanFunction (factory query used at
//! scan time) so both refer to the same source table.
static string BuildSelectAllQuery(const SnowflakeConfig &config, const string &schema_name, const string &table_name) {
	return "SELECT * FROM " + QuoteSnowflakeIdentifier(config.database) + "." + QuoteSnowflakeIdentifier(schema_name) +
	       "." + QuoteSnowflakeIdentifier(table_name);
}

void SnowflakeTableEntry::LoadColumnsAndSchema(ClientContext &context) {
	DPRINT("SnowflakeTableEntry::LoadColumnsAndSchema called for %s.%s.%s\n", client->GetConfig().database.c_str(),
	       schema.name.c_str(), name.c_str());

	auto &config = client->GetConfig();
	string query = BuildSelectAllQuery(config, schema.name, name);

	auto &client_manager = SnowflakeClientManager::GetInstance();
	auto connection = client_manager.GetConnection(config);
	auto factory = make_uniq<SnowflakeArrowStreamFactory>(connection, query);

	// One ADBC ExecuteSchema roundtrip per table to resolve the Arrow schema.
	// Lightweight: it's a metadata-only call, not a full query execution.
	ArrowSchemaWrapper schema_root;
	SnowflakeGetArrowSchema(reinterpret_cast<ArrowArrayStream *>(factory.get()), schema_root.arrow_schema);

	// Derive DuckDB names/types from the Arrow schema.
	ArrowTableSchema arrow_table;
	ArrowTableFunction::PopulateArrowTableSchema(context, arrow_table, schema_root.arrow_schema);
	const auto names = arrow_table.GetNames();
	const auto return_types = arrow_table.GetTypes();

	// Cache a deep copy of the Arrow schema so GetScanFunction can build bind
	// data without paying another Snowflake roundtrip.
	cached_schema_root = make_uniq<ArrowSchemaWrapper>();
	CloneCachedSchema(schema_root.arrow_schema, cached_schema_root->arrow_schema);

	// Populate the inherited columns ColumnList. After this method returns the
	// entry is published into the catalog set and any future read of
	// `columns` (e.g. duckdb_columns(), plan binding) sees a fully-built list
	// without taking any lock.
	for (idx_t i = 0; i < static_cast<idx_t>(names.size()); i++) {
		DPRINT("  Column: %s, Type: %s\n", names[i].c_str(), return_types[i].ToString().c_str());
		columns.AddColumn(ColumnDefinition(names[i], return_types[i]));
	}
}

TableFunction SnowflakeTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	DPRINT("SnowflakeTableEntry::GetScanFunction called for table %s.%s.%s\n", client->GetConfig().database.c_str(),
	       schema.name.c_str(), name.c_str());

	if (!cached_schema_root || !cached_schema_root->arrow_schema.release) {
		// Invariant: SnowflakeTableSet::LoadEntries must have called
		// LoadColumnsAndSchema before publishing this entry. If we hit this,
		// some other code path constructed the entry and skipped the load.
		throw InternalException("SnowflakeTableEntry %s.%s.%s used before LoadColumnsAndSchema", catalog.GetName(),
		                        schema.name, name);
	}

	auto &config = client->GetConfig();
	string query = BuildSelectAllQuery(config, schema.name, name);
	DPRINT("SnowflakeTableEntry: Query = '%s'\n", query.c_str());

	// TODO consider maintaining a thread-safe pool of connections in client, so
	// we can use the client within SnowflakeTableEntry instead of creating a new
	// client
	auto &client_manager = SnowflakeClientManager::GetInstance();
	auto connection = client_manager.GetConnection(config);

	auto factory = make_uniq<SnowflakeArrowStreamFactory>(connection, query);
	DPRINT("SnowflakeTableEntry: Created factory at %p\n", (void *)factory.get());

	// Apply pushdown settings from catalog options
	auto &snowflake_catalog = catalog.Cast<SnowflakeCatalog>();
	const auto &catalog_options = snowflake_catalog.GetOptions();
	factory->filter_pushdown_enabled = catalog_options.enable_pushdown;
	factory->projection_pushdown_enabled = catalog_options.enable_pushdown;
	DPRINT("SnowflakeTableEntry: Pushdown %s (enable_pushdown=%s)\n",
	       catalog_options.enable_pushdown ? "ENABLED" : "DISABLED",
	       catalog_options.enable_pushdown ? "true" : "false");

	auto snowflake_bind_data = make_uniq<SnowflakeScanBindData>(std::move(factory));

	// Set pushdown settings on bind_data (critical for avoiding crashes!)
	snowflake_bind_data->projection_pushdown_enabled = catalog_options.enable_pushdown;

	// `cached_schema_root` was populated once at LoadColumnsAndSchema time and
	// is immutable afterwards, so this read is race-free with any concurrent
	// reader of the same entry (e.g. duckdb_columns(), plan binding on another
	// query).
	DPRINT("SnowflakeTableEntry: Cloning cached Arrow schema (no SF roundtrip)\n");
	CloneCachedSchema(cached_schema_root->arrow_schema, snowflake_bind_data->schema_root.arrow_schema);

	// Use the new DuckDB API to populate the arrow table schema
	ArrowTableFunction::PopulateArrowTableSchema(context, snowflake_bind_data->arrow_table,
	                                             snowflake_bind_data->schema_root.arrow_schema);
	const auto names = snowflake_bind_data->arrow_table.GetNames();
	const auto return_types = snowflake_bind_data->arrow_table.GetTypes();
	snowflake_bind_data->all_types = return_types;

	// Set column names on factory for filter building (maps column indices to
	// names)
	snowflake_bind_data->factory->column_names = names;

	DPRINT("SnowflakeTableEntry: Setting bind_data at %p\n", (void *)snowflake_bind_data.get());
	bind_data = std::move(snowflake_bind_data);

	DPRINT("SnowflakeTableEntry: Returning GetSnowflakeTableScanFunction "
	       "(pushdown %s)\n",
	       catalog_options.enable_pushdown ? "enabled" : "disabled");
	return GetSnowflakeTableScanFunction(catalog_options.enable_pushdown);
}

unique_ptr<BaseStatistics> SnowflakeTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	throw NotImplementedException("Snowflake does not support getting statistics for tables");
}

TableStorageInfo SnowflakeTableEntry::GetStorageInfo(ClientContext &context) {
	TableStorageInfo result;
	// Don't fetch row count to avoid ADBC statement conflicts
	// Snowflake is read-only, so exact cardinality isn't critical
	result.cardinality = 0;
	result.index_info = vector<IndexInfo>();
	return result;
}

} // namespace snowflake
} // namespace duckdb
