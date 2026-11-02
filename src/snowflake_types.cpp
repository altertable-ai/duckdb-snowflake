#include "snowflake_types.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/exception/conversion_exception.hpp"

namespace duckdb {
namespace snowflake {

static bool TryParseInt(const string &value, int &out) {
	try {
		out = std::stoi(value);
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

static LogicalType DecimalFromParams(const string &snowflake_type_str, const string &params, bool number_kind) {
	auto comma_pos = params.find(',');
	int precision = 18;
	int scale = 0;

	if (comma_pos == string::npos) {
		if (!TryParseInt(params, precision)) {
			throw ConversionException("Invalid precision '%s' in type: %s", params, snowflake_type_str);
		}
	} else {
		if (!TryParseInt(params.substr(0, comma_pos), precision)) {
			throw ConversionException("Invalid precision '%s' in type: %s", params.substr(0, comma_pos),
			                          snowflake_type_str);
		}
		if (!TryParseInt(params.substr(comma_pos + 1), scale)) {
			throw ConversionException("Invalid scale '%s' in type: %s", params.substr(comma_pos + 1),
			                          snowflake_type_str);
		}
	}

	if (precision < 1 || precision > 38) {
		throw ConversionException("%s precision %d out of range (1-38)", number_kind ? "NUMBER" : "DECIMAL", precision);
	}
	if (scale < 0 || scale > precision) {
		throw ConversionException("%s scale %d invalid (must be 0-%d)", number_kind ? "NUMBER" : "DECIMAL", scale,
		                          precision);
	}

	if (number_kind) {
		return ConvertNumber(static_cast<uint8_t>(precision), static_cast<uint8_t>(scale));
	}
	return LogicalType::DECIMAL(static_cast<uint8_t>(precision), static_cast<uint8_t>(scale));
}

LogicalType SnowflakeTypeToLogicalType(const std::string &snowflake_type_str, int32_t numeric_precision,
                                       int32_t numeric_scale) {
	string normalized_type = StringUtil::Upper(snowflake_type_str);
	normalized_type = StringUtil::Replace(normalized_type, " ", "");

	auto paren_pos = normalized_type.find('(');
	string base_type = normalized_type.substr(0, paren_pos);

	// Prefer INFORMATION_SCHEMA NUMERIC_PRECISION/SCALE when the caller has them.
	if (numeric_precision >= 1 && (base_type == "NUMBER" || base_type == "DECIMAL" || base_type == "NUMERIC")) {
		int32_t scale = numeric_scale < 0 ? 0 : numeric_scale;
		if (numeric_precision > 38 || scale > numeric_precision) {
			throw ConversionException("%s precision/scale (%d,%d) out of range", base_type, numeric_precision, scale);
		}
		if (base_type == "NUMBER") {
			return ConvertNumber(static_cast<uint8_t>(numeric_precision), static_cast<uint8_t>(scale));
		}
		return LogicalType::DECIMAL(static_cast<uint8_t>(numeric_precision), static_cast<uint8_t>(scale));
	}

	if (base_type.find("INT") != std::string::npos) {
		if (base_type == "TINYINT") {
			return LogicalType::TINYINT;
		} else if (base_type == "SMALLINT") {
			return LogicalType::SMALLINT;
		} else if (base_type == "BIGINT") {
			return LogicalType::BIGINT;
		}
		return LogicalType::INTEGER;
	}

	if (base_type == "VARCHAR" || base_type == "STRING" || base_type == "TEXT" || base_type == "CHAR" ||
	    base_type == "CHARACTER" || base_type == "NCHAR" || base_type == "NVARCHAR") {
		return LogicalType::VARCHAR;
	}

	if (base_type == "BOOLEAN") {
		return LogicalType::BOOLEAN;
	}

	// Snowflake FLOAT is IEEE-754 binary64; ADBC emits float64. Map all float
	// aliases to DOUBLE so catalog types match the Arrow bind schema.
	if (base_type == "FLOAT" || base_type == "FLOAT4" || base_type == "REAL" || base_type == "DOUBLE" ||
	    base_type == "FLOAT8" || base_type == "DOUBLEPRECISION") {
		return LogicalType::DOUBLE;
	}

	if (base_type == "DECIMAL" || base_type == "NUMERIC") {
		if (paren_pos == std::string::npos) {
			return LogicalType::DECIMAL(18, 0);
		}
		auto close_paren_pos = normalized_type.find(')');
		if (close_paren_pos == std::string::npos) {
			throw InvalidInputException("Expected closing ')' for DECIMAL type: " + snowflake_type_str);
		}
		return DecimalFromParams(snowflake_type_str,
		                         normalized_type.substr(paren_pos + 1, close_paren_pos - paren_pos - 1), false);
	}

	if (base_type == "NUMBER") {
		if (paren_pos == std::string::npos) {
			// Snowflake NUMBER without parameters is NUMBER(38,0). DECIMAL(38,0)
			// matches the ADBC Arrow bind schema; DOUBLE does not.
			return ConvertNumber(38, 0);
		}
		auto close_paren_pos = normalized_type.find(')');
		if (close_paren_pos == std::string::npos) {
			throw InvalidInputException("Expected closing ')' for NUMBER type: " + snowflake_type_str);
		}
		return DecimalFromParams(snowflake_type_str,
		                         normalized_type.substr(paren_pos + 1, close_paren_pos - paren_pos - 1), true);
	}

	if (base_type == "DATE") {
		return LogicalType::DATE;
	}
	if (base_type == "TIME") {
		return LogicalType::TIME;
	}
	if (base_type == "TIMESTAMP_LTZ" || base_type == "TIMESTAMP_TZ" || base_type == "TIMESTAMPTZ") {
		return LogicalType::TIMESTAMP_TZ;
	}
	if (base_type == "TIMESTAMP" || base_type == "TIMESTAMP_NTZ" || base_type == "DATETIME") {
		return LogicalType::TIMESTAMP;
	}

	if (base_type == "BINARY" || base_type == "VARBINARY" || base_type == "BYTE" || base_type == "BYTES") {
		return LogicalType::BLOB;
	}

	if (base_type == "GEOGRAPHY" || base_type == "GEOMETRY") {
		return LogicalType::GEOMETRY();
	}

	// VARIANT/OBJECT/ARRAY arrive as UTF-8 JSON from the ADBC driver.
	if (base_type == "VARIANT" || base_type == "OBJECT" || base_type == "ARRAY") {
		return LogicalType::VARCHAR;
	}

	return LogicalType::VARCHAR;
}

LogicalType ConvertNumber(uint8_t precision, uint8_t scale) {
	// For integer types (scale = 0), map to appropriate integer type based on
	// precision
	if (scale == 0) {
		if (precision <= 2U) {
			return LogicalType::TINYINT; // -128 to 127
		}
		if (precision <= 4U) {
			return LogicalType::SMALLINT; // -32,768 to 32,767
		}
		if (precision <= 9U) {
			return LogicalType::INTEGER; // -2,147,483,648 to 2,147,483,647
		}
		if (precision <= 18U) {
			return LogicalType::BIGINT; // -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
		}
	}

	// For any type with scale > 0 or precision > 18, use DECIMAL to maintain
	// exact precision This ensures no loss of precision for financial/monetary
	// calculations
	return LogicalType::DECIMAL(precision, scale);
}
} // namespace snowflake
} // namespace duckdb
