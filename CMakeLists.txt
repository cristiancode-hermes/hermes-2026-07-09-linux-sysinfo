cmake_minimum_required(VERSION 3.28)
project(linux-sysinfo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (FetchContent) ──
include(FetchContent)

# nlohmann_json — structured output (JSON)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog — logging
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
)
FetchContent_MakeAvailable(spdlog)

# Catch2 — testing framework (v3)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.0
)
FetchContent_MakeAvailable(Catch2)

# ── Sources ──
file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)

add_executable(linux-sysinfo ${SOURCES} ${HEADERS})
target_include_directories(linux-sysinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Enlazar dependencias
target_link_libraries(linux-sysinfo PRIVATE
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  pthread
)

# ── Tests ──
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS linux-sysinfo DESTINATION bin)
