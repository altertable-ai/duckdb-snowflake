#include "snowflake_client_manager.hpp"
#include "snowflake_debug.hpp"

namespace duckdb {
namespace snowflake {

ConnectionLease::~ConnectionLease() {
	Reset();
}

void ConnectionLease::Reset() {
	if (client && manager) {
		manager->Return(config, std::move(client), reusable);
	}
	manager = nullptr;
	client = nullptr;
}

SnowflakeClientManager &SnowflakeClientManager::GetInstance() {
	static SnowflakeClientManager instance;
	return instance;
}

ConnectionLease SnowflakeClientManager::Acquire(const SnowflakeConfig &config) {
	// Try to reuse an idle connection for this config.
	shared_ptr<SnowflakeClient> client;
	{
		std::lock_guard<std::mutex> lock(pool_mutex);
		auto it = idle_connections.find(config);
		if (it != idle_connections.end() && !it->second.empty()) {
			client = std::move(it->second.back());
			it->second.pop_back();
		}
	}

	if (client) {
		// Validate the reused connection outside the pool lock (TestConnection may
		// do a round-trip). If it's stale (e.g. token expired) drop it and create
		// a fresh one below.
		if (client->IsConnected() && client->TestConnection()) {
			DPRINT("Reusing pooled Snowflake connection\n");
			return ConnectionLease(*this, config, std::move(client));
		}
		DPRINT("Pooled connection is stale, discarding and reconnecting\n");
		client.reset();
	}

	// No usable idle connection — create a dedicated one.
	DPRINT("Creating new Snowflake connection\n");
	client = make_shared_ptr<SnowflakeClient>();
	client->Connect(config);
	return ConnectionLease(*this, config, std::move(client));
}

void SnowflakeClientManager::Return(const SnowflakeConfig &config, shared_ptr<SnowflakeClient> client, bool reusable) {
	if (!client) {
		return;
	}
	if (!reusable || !client->IsConnected()) {
		// Not reusable (auth error) or already disconnected: let it be torn down
		// (the by-value `client` disconnects when destroyed after this returns).
		return;
	}
	std::lock_guard<std::mutex> lock(pool_mutex);
	auto &idle = idle_connections[config];
	if (idle.size() < MAX_IDLE_PER_CONFIG) {
		idle.push_back(std::move(client));
	}
	// Over the idle cap: drop it (destroyed after the lock is released).
}

void SnowflakeClientManager::DrainIdle(const SnowflakeConfig &config) {
	std::vector<shared_ptr<SnowflakeClient>> to_close;
	{
		std::lock_guard<std::mutex> lock(pool_mutex);
		auto it = idle_connections.find(config);
		if (it != idle_connections.end()) {
			to_close = std::move(it->second);
			idle_connections.erase(it);
		}
	}
	// Destroy (disconnect) the drained connections outside the lock.
}

} // namespace snowflake
} // namespace duckdb
