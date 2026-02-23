#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <pqxx/zview.hxx>

class Database {
private:
  std::string db_connection_string;
  std::unique_ptr<pqxx::connection> db_connection;

  mutable std::mutex db_mutex;

  const int max_reconnect_attempts{3};
  std::chrono::milliseconds reconnect_delay{1000};

  bool connect_internal();
  bool ensure_connection();
  void disconnect();

  // Singleton stuff
  Database() = default;
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  Database(Database &&) = delete;
  Database &operator=(Database &&) = delete;

  ~Database();

public:
  static Database &instance();
  bool initialize(const std::string &connection_string);
  pqxx::result execute_with_session_limits(const std::string &sql,
                                           const pqxx::params &params,
                                           int statement_timeout_ms,
                                           int lock_timeout_ms,
                                           int idle_timeout_ms,
                                           bool read_only);

  // Need to put the template method in the header file

  template <typename... Args>
  pqxx::result execute(const std::string &sql, Args... args) {
    try {
      if (!ensure_connection()) {
        throw std::runtime_error("Failed to connect to database");
      }

      pqxx::params params;
      (params.append(args), ...);

      std::lock_guard<std::mutex> lock(db_mutex);
      pqxx::work txn(*db_connection);
      pqxx::result result = txn.exec(pqxx::zview(sql), params);
      txn.commit();
      return result;

    } catch (const pqxx::broken_connection &e) {
      throw std::runtime_error(std::string("Database connection failed: ") +
                               e.what());
    }
  }
};
