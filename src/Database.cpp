#include <Database.h>
#include <exception>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>

bool Database::connect_internal() {
  try {
    db_connection = std::make_unique<pqxx::connection>(db_connection_string);
    return db_connection && db_connection->is_open();
  } catch (const std::exception &e) {
    std::cout << "DB exception: " << e.what() << std::endl;
    return false;
  }
}

Database &Database::instance() {
  static Database instance;
  return instance;
}

bool Database::ensure_connection() {
  std::lock_guard<std::mutex> lock(db_mutex);

  if (db_connection && db_connection->is_open()) {
    return true;
  }

  for (int attempt = 0; attempt < max_reconnect_attempts; ++attempt) {
    if (connect_internal()) {
      return true;
    }

    std::this_thread::sleep_for(reconnect_delay);
  }

  return false;
}

bool Database::initialize(const std::string &connection_string) {
  std::lock_guard<std::mutex> lock(db_mutex);
  db_connection_string = connection_string;
  return connect_internal();
}

pqxx::result Database::execute_with_session_limits(const std::string &sql,
                                                   const pqxx::params &params,
                                                   int statement_timeout_ms,
                                                   int lock_timeout_ms,
                                                   int idle_timeout_ms,
                                                   bool read_only) {
  try {
    if (!ensure_connection()) {
      throw std::runtime_error("Failed to connect to database");
    }

    std::lock_guard<std::mutex> lock(db_mutex);
    pqxx::work txn(*db_connection);

    txn.exec(std::format("set local statement_timeout = '{}'", statement_timeout_ms));
    txn.exec(std::format("set local lock_timeout = '{}'", lock_timeout_ms));
    txn.exec(std::format("set local idle_in_transaction_session_timeout = '{}'",
                         idle_timeout_ms));
    if (read_only) {
      txn.exec("set local transaction_read_only = on");
    }

    pqxx::result result = txn.exec(pqxx::zview(sql), params);
    txn.commit();
    return result;
  } catch (const pqxx::broken_connection &e) {
    throw std::runtime_error(std::string("Database connection failed: ") + e.what());
  }
}

void Database::disconnect() {
  std::lock_guard<std::mutex> lock(db_mutex);
  db_connection.reset();
}

Database::~Database() { disconnect(); }
