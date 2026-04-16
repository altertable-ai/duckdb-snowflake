#include "storage/snowflake_table_entry.hpp"
#include "storage/snowflake_catalog.hpp"
#include "snowflake_debug.hpp"
#include "snowflake_client_manager.hpp"
#include "snowflake_scan.hpp"
#include "snowflake_arrow_utils.hpp"
#include "snowflake_client.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/function/table/arrow.hpp"
#include "duckdb/function/table/arrow/arrow_duck_schema.hpp"
#include <algorithm>

namespace duckdb {
namespace snowflake {

TableFunction SnowflakeTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	DPRINT("SnowflakeTableEntry::GetScanFunction called for table %s.%s.%s\n", client->GetConfig().database.c_str(),
	       schema.name.c_str(), name.c_str());

	auto &config = client->GetConfig();
	string fqn = config.database + "." + schema.name + "." + name;

	// Start with SELECT * for schema discovery
	string query = "SELECT * FROM " + fqn;

	auto &client_manager = SnowflakeClientManager::GetInstance();
	auto connection = client_manager.GetConnection(config);

	auto factory = make_uniq<SnowflakeArrowStreamFactory>(connection, query);

	// Apply pushdown settings from catalog options
	auto &snowflake_catalog = catalog.Cast<SnowflakeCatalog>();
	const auto &catalog_options = snowflake_catalog.GetOptions();
	factory->filter_pushdown_enabled = catalog_options.enable_pushdown;
	factory->projection_pushdown_enabled = catalog_options.enable_pushdown;

	auto snowflake_bind_data = make_uniq<SnowflakeScanBindData>(std::move(factory));

	// Fetch Arrow schema (lightweight - doesn't execute the full query)
	SnowflakeGetArrowSchema(reinterpret_cast<ArrowArrayStream *>(snowflake_bind_data->factory.get()),
	                        snowflake_bind_data->schema_root.arrow_schema);

	// Detect GEOGRAPHY/GEOMETRY columns when geo support is enabled.
	// Uses INFORMATION_SCHEMA.COLUMNS (catalog metadata) because Snowflake's query
	// response metadata is ambiguous: SNOWFLAKE_TYPE='object' matches both geo types
	// and VARIANT/OBJECT columns. Catalog metadata reliably reports GEOGRAPHY/GEOMETRY.
	// This is opt-in (ENABLE_GEO TRUE) because it adds an extra metadata query per table.
	if (catalog_options.enable_geo) {
		auto geo_cols = client->DetectGeoColumns(context, schema.name, name);

		if (!geo_cols.empty()) {
			snowflake_bind_data->factory->geo_column_names = std::move(geo_cols);

			// Patch the Arrow schema for detected geo columns:
			//   1. Change format from "u" (utf8) to "z" (binary) to match ST_ASWKB output
			//   2. Annotate with geoarrow.wkb extension type so DuckDB maps to GEOMETRY
			auto &arrow_schema = snowflake_bind_data->schema_root.arrow_schema;

			for (int64_t i = 0; i < arrow_schema.n_children; i++) {
				auto *child = arrow_schema.children[i];
				if (!child || !child->name) {
					continue;
				}
				if (!snowflake_bind_data->factory->geo_column_names.count(child->name)) {
					continue;
				}

				DPRINT("SnowflakeTableEntry: Detected geo column '%s' at index %lld\n", child->name, i);

				// Patch format from "u" (utf8) to "z" (binary) to match ST_ASWKB output.
				// Use strdup so the schema's release callback can free() it.
				child->format = strdup("z");

				// Annotate with geoarrow.wkb extension type so DuckDB maps to GEOMETRY.
				// Copy to malloc'd memory because the Arrow schema's release callback owns it.
				auto metadata = BuildArrowMetadata("ARROW:extension:name", "geoarrow.wkb");
				size_t meta_size = sizeof(int32_t) + strlen("ARROW:extension:name") + sizeof(int32_t) +
				                   strlen("geoarrow.wkb") + sizeof(int32_t);
				char *meta_buf = static_cast<char *>(malloc(meta_size));
				std::copy(metadata.get(), metadata.get() + meta_size, meta_buf);
				child->metadata = meta_buf;

				DPRINT("Patched '%s' -> binary + geoarrow.wkb\n", child->name);
			}
		}
	}

	// DuckDB's Arrow scanner needs projection_pushdown for Arrow extension types
	// (geoarrow.wkb → GEOMETRY) and count(*), regardless of user's pushdown setting
	snowflake_bind_data->projection_pushdown_enabled = true;

	// Use the new DuckDB API to populate the arrow table schema
	vector<string> names;
	vector<LogicalType> return_types;
	ArrowTableFunction::PopulateArrowTableSchema(context, snowflake_bind_data->arrow_table,
	                                             snowflake_bind_data->schema_root.arrow_schema);
	names = snowflake_bind_data->arrow_table.GetNames();
	return_types = snowflake_bind_data->arrow_table.GetTypes();
	snowflake_bind_data->all_types = return_types;

	snowflake_bind_data->factory->column_names = names;

	if (!columns_loaded) {
		for (idx_t i = 0; i < static_cast<idx_t>(names.size()); i++) {
			DPRINT("  Column: %s, Type: %s\n", names[i].c_str(), return_types[i].ToString().c_str());
			columns.AddColumn(ColumnDefinition(names[i], return_types[i]));
		}
		columns_loaded = true;
	}

	bind_data = std::move(snowflake_bind_data);
	return GetSnowflakeTableScanFunction(catalog_options.enable_pushdown);
}

unique_ptr<BaseStatistics> SnowflakeTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	throw NotImplementedException("Snowflake does not support getting statistics for tables");
}

TableStorageInfo SnowflakeTableEntry::GetStorageInfo(ClientContext &context) {
	TableStorageInfo result;
	result.cardinality = 0;
	result.index_info = vector<IndexInfo>();
	return result;
}

} // namespace snowflake
} // namespace duckdb
