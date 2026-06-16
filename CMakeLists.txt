cmake_minimum_required(VERSION 3.22)

project(nissefar VERSION 0.1 DESCRIPTION "Nissefar")

enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH
    "FetchContent base directory" FORCE)

FetchContent_Declare(
  cpp_httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.20.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(cpp_httplib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/Nissefar.cpp
  src/Config.cpp
  src/Database.cpp
  src/DbOps.cpp
  src/LlmService.cpp
  src/DiscordEventService.cpp
  src/GoogleDocsService.cpp
  src/YoutubeService.cpp
  src/UrlSafety.cpp
  src/HtmlTextExtract.cpp
  src/HormuzStatus.cpp
  src/Sentiment.cpp
  src/WebPageService.cpp
  src/VideoSummaryService.cpp
  src/CalculationService.cpp
  src/NordPoolService.cpp
  src/DiffUtil.cpp
  src/Utf8Display.cpp
  src/Formatting.cpp
  src/SqlSafety.cpp
  src/AnalyticsQuery.cpp
  src/ChatGptAuth.cpp
  src/ChatGptDeviceAuth.cpp
  src/HttpClient.cpp
  src/CodexClient.cpp
)

add_compile_definitions(DPP_CORO=ON)

find_package(DPP REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_library(PQXX_LIB pqxx)
find_library(PQ_LIB pq)

target_link_libraries(${PROJECT_NAME}
  ${DPP_LIBRARIES}
  ${PQXX_LIB}
  ${PQ_LIB}
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

target_include_directories(${PROJECT_NAME} PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
  ${DPP_INCLUDE_DIR}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
 )

add_executable(url_safety_tests
  tests/UrlSafetyTests.cpp
  src/UrlSafety.cpp
)

target_include_directories(url_safety_tests PRIVATE
  include/
)

set_target_properties(url_safety_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME url_safety_tests COMMAND url_safety_tests)

add_executable(formatting_tests
  tests/FormattingTests.cpp
  src/Utf8Display.cpp
)

target_include_directories(formatting_tests PRIVATE
  include/
)

set_target_properties(formatting_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME formatting_tests COMMAND formatting_tests)

add_executable(sql_safety_tests
  tests/SqlSafetyTests.cpp
  src/SqlSafety.cpp
)

target_include_directories(sql_safety_tests PRIVATE
  include/
)

set_target_properties(sql_safety_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME sql_safety_tests COMMAND sql_safety_tests)

add_executable(analytics_query_tests
  tests/AnalyticsQueryTests.cpp
  src/AnalyticsQuery.cpp
)

target_include_directories(analytics_query_tests PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

set_target_properties(analytics_query_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME analytics_query_tests COMMAND analytics_query_tests)

add_executable(nord_pool_service_tests
  tests/NordPoolServiceTests.cpp
  src/NordPoolService.cpp
  src/HttpClient.cpp
)

target_include_directories(nord_pool_service_tests PRIVATE
  include/
)

target_link_libraries(nord_pool_service_tests
  CURL::libcurl
)

set_target_properties(nord_pool_service_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME nord_pool_service_tests COMMAND nord_pool_service_tests)

add_executable(html_text_extract_tests
  tests/HtmlTextExtractTests.cpp
  src/HtmlTextExtract.cpp
)

target_include_directories(html_text_extract_tests PRIVATE
  include/
)

set_target_properties(html_text_extract_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME html_text_extract_tests COMMAND html_text_extract_tests)

add_executable(hormuz_status_tests
  tests/HormuzStatusTests.cpp
  src/HormuzStatus.cpp
)

target_include_directories(hormuz_status_tests PRIVATE
  include/
)

set_target_properties(hormuz_status_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME hormuz_status_tests COMMAND hormuz_status_tests)

add_executable(sentiment_tests
  tests/SentimentTests.cpp
  src/Sentiment.cpp
)

target_include_directories(sentiment_tests PRIVATE
  include/
)

set_target_properties(sentiment_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME sentiment_tests COMMAND sentiment_tests)

add_executable(chatgpt_auth_tests
  tests/ChatGptAuthTests.cpp
  src/ChatGptAuth.cpp
  src/ChatGptDeviceAuth.cpp
  src/HttpClient.cpp
)

target_include_directories(chatgpt_auth_tests PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

target_compile_definitions(chatgpt_auth_tests PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(chatgpt_auth_tests
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

set_target_properties(chatgpt_auth_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME chatgpt_auth_tests COMMAND chatgpt_auth_tests)

add_executable(chatgpt_device_auth_tests
  tests/ChatGptDeviceAuthTests.cpp
  src/ChatGptDeviceAuth.cpp
  src/HttpClient.cpp
)

target_include_directories(chatgpt_device_auth_tests PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

target_compile_definitions(chatgpt_device_auth_tests PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(chatgpt_device_auth_tests
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

set_target_properties(chatgpt_device_auth_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME chatgpt_device_auth_tests COMMAND chatgpt_device_auth_tests)

add_executable(codex_client_tests
  tests/CodexClientTests.cpp
  src/CodexClient.cpp
  src/HttpClient.cpp
)

target_include_directories(codex_client_tests PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

target_compile_definitions(codex_client_tests PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(codex_client_tests
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

set_target_properties(codex_client_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME codex_client_tests COMMAND codex_client_tests)

add_executable(nissefar-chatgpt-auth
  src/chatgpt_auth_main.cpp
  src/ChatGptAuth.cpp
  src/ChatGptDeviceAuth.cpp
  src/HttpClient.cpp
)

target_include_directories(nissefar-chatgpt-auth PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

target_compile_definitions(nissefar-chatgpt-auth PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(nissefar-chatgpt-auth
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

set_target_properties(nissefar-chatgpt-auth PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_executable(codex_probe
  src/codex_probe_main.cpp
  src/ChatGptAuth.cpp
  src/ChatGptDeviceAuth.cpp
  src/HttpClient.cpp
  src/CodexClient.cpp
)

target_include_directories(codex_probe PRIVATE
  include/
  ${cpp_httplib_SOURCE_DIR}
)

target_compile_definitions(codex_probe PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(codex_probe
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

set_target_properties(codex_probe PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_executable(diff_util_tests
  tests/DiffUtilTests.cpp
  src/DiffUtil.cpp
)

target_include_directories(diff_util_tests PRIVATE
  include/
)

set_target_properties(diff_util_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME diff_util_tests COMMAND diff_util_tests)
