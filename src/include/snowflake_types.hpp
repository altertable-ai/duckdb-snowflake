#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {
namespace snowflake {
//! Map a Snowflake INFORMATION_SCHEMA data type to a DuckDB LogicalType.
//! `numeric_precision` / `numeric_scale` come from NUMERIC_PRECISION / NUMERIC_SCALE
//! when available (-1 if the column was NULL); they take precedence over any
//! `(p,s)` suffix in `snowflake_type_str` for NUMBER/DECIMAL/NUMERIC.
LogicalType SnowflakeTypeToLogicalType(const std::string &snowflake_type_str, int32_t numeric_precision = -1,
                                       int32_t numeric_scale = -1);
LogicalType ConvertNumber(uint8_t precision, uint8_t scale);
} // namespace snowflake
} // namespace duckdb
