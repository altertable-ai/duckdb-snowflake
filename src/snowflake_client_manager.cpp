#include "snowflake_client_manager.hpp"
#include "snowflake_debug.hpp"

namespace duckdb {
namespace snowflake {

SnowflakeClientManager &SnowflakeClientManager::GetInstance() {
	static SnowflakeClientManager instance;
	return instance;
}

shared_ptr<SnowflakeClient> SnowflakeClientManager::GetConnection(const SnowflakeConfig &config) {
	std::lock_guard<std::mutex> lock(connection_mutex);

	auto it = connections.find(config);
	if (it != connections.end() && it->second->IsConnected()) {
		// Validate the connection is still alive (handles token expiration)
		if (it->second->TestConnection()) {
			DPRINT("Reusing existing valid connection\n");
			return it->second;
		}
		// Connection is stale (e.g., token expired), remove it
		DPRINT("Cached connection is stale, removing and creating new connection\n");
		connections.erase(it);
	}

	// Create new connection
	DPRINT("Creating new Snowflake connection\n");
	auto connection = make_shared_ptr<SnowflakeClient>();
	connection->Connect(config);

	connections[config] = connection;
	return connection;
}

void SnowflakeClientManager::ReleaseConnection(const SnowflakeConfig &config) {
	std::lock_guard<std::mutex> lock(connection_mutex);
	connections.erase(config);
}

void SnowflakeClientManager::InvalidateConnection(const SnowflakeConfig &config) {
	std::lock_guard<std::mutex> lock(connection_mutex);
	auto it = connections.find(config);
	if (it != connections.end()) {
		DPRINT("Invalidating cached connection (auth error or expiration)\n");
		connections.erase(it);
	}
}

shared_ptr<SnowflakeClient> SnowflakeClientManager::GetFreshConnection(const SnowflakeConfig &config) {
	std::lock_guard<std::mutex> lock(connection_mutex);

	// Remove any existing connection
	auto it = connections.find(config);
	if (it != connections.end()) {
		DPRINT("Removing existing connection to create fresh one\n");
		connections.erase(it);
	}

	// Create new connection
	DPRINT("Creating fresh Snowflake connection\n");
	auto connection = make_shared_ptr<SnowflakeClient>();
	connection->Connect(config);

	connections[config] = connection;
	return connection;
}

} // namespace snowflake
} // namespace duckdb
