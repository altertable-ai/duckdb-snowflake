#pragma once

#include "snowflake_client.hpp"
#include "snowflake_config.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>

namespace duckdb {
namespace snowflake {

class SnowflakeClientManager {
public:
	static SnowflakeClientManager &GetInstance();

	shared_ptr<SnowflakeClient> GetConnection(const SnowflakeConfig &config);

	void ReleaseConnection(const SnowflakeConfig &config);

	//! Invalidate a connection (e.g., on auth error) and force reconnection on next GetConnection
	void InvalidateConnection(const SnowflakeConfig &config);

	//! Get a fresh connection, bypassing the cache (for retry after auth failure)
	shared_ptr<SnowflakeClient> GetFreshConnection(const SnowflakeConfig &config);

private:
	SnowflakeClientManager() = default;
	std::unordered_map<SnowflakeConfig, shared_ptr<SnowflakeClient>, SnowflakeConfigHash> connections;
	std::mutex connection_mutex;
};

} // namespace snowflake
} // namespace duckdb
