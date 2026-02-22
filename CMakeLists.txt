cmake_minimum_required(VERSION 3.22)

project(nissefar VERSION 0.1 DESCRIPTION "Nissefar")

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
  src/WebPageService.cpp
  src/DiffUtil.cpp
  src/Formatting.cpp
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
