cmake_minimum_required(VERSION 3.22)

project(nissefar VERSION 0.1 DESCRIPTION "Nissefar")

enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  ollama_hpp
  GIT_REPOSITORY https://github.com/jmont-dev/ollama-hpp.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ollama_hpp)

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
  src/WebPageService.cpp
  src/VideoSummaryService.cpp
  src/CalculationService.cpp
  src/DiffUtil.cpp
  src/Utf8Display.cpp
  src/Formatting.cpp
  src/SqlSafety.cpp
  src/AnalyticsQuery.cpp
)

add_compile_definitions(DPP_CORO=ON)

find_package(DPP REQUIRED)
find_library(PQXX_LIB pqxx)
find_library(PQ_LIB pq)

target_link_libraries(${PROJECT_NAME}
  ${DPP_LIBRARIES}
  ${PQXX_LIB}
  ${PQ_LIB}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  include/
  ${ollama_hpp_SOURCE_DIR}/include
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
  ${ollama_hpp_SOURCE_DIR}/include
)

set_target_properties(analytics_query_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME analytics_query_tests COMMAND analytics_query_tests)

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
