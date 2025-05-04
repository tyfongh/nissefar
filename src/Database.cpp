#include <Database.h>
#include <exception>
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

void Database::disconnect() {
  std::lock_guard<std::mutex> lock(db_mutex);
  db_connection.reset();
}

Database::~Database() { disconnect(); }
