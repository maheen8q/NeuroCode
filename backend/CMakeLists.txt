cmake_minimum_required(VERSION 3.20)
project(neurocode LANGUAGES CXX)

# T3 owns this file. T1 / T4: send the target lines you need, do not edit.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

# ---------------------------------------------------------------------------
# nc_storage: Database, UserRepo, JobRepo, ModelRepo.
# Needs only SQLite and nlohmann/json, so the storage tests build without Drogon or LibTorch.
# ---------------------------------------------------------------------------
file(GLOB NC_STORAGE_SOURCES CONFIGURE_DEPENDS src/storage/*.cpp)
add_library(nc_storage STATIC ${NC_STORAGE_SOURCES})
target_include_directories(nc_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nc_storage PUBLIC SQLite::SQLite3 nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(nc_storage PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# neurocode server. Disable with -DNC_BUILD_SERVER=OFF to build only the storage tests.
# ---------------------------------------------------------------------------
option(NC_BUILD_SERVER "Build the neurocode server (needs Drogon and LibTorch)" ON)

if(NC_BUILD_SERVER)
  find_package(Drogon 1.9 CONFIG REQUIRED)
  find_package(Torch REQUIRED)   # pass -DCMAKE_PREFIX_PATH=/path/to/libtorch
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

  # Everything under src/ except main.cpp and storage/ (already in nc_storage).
  # Folders that do not exist yet (api/, services/, engine/, ...) simply match nothing.
  file(GLOB_RECURSE NC_APP_SOURCES CONFIGURE_DEPENDS
       src/api/*.cpp src/services/*.cpp src/common/*.cpp
       src/ml/*.cpp src/engine/*.cpp src/sandbox/*.cpp)
  # These are standalone command-line tools with their own main(); keep them out of the server.
  list(FILTER NC_APP_SOURCES EXCLUDE REGEX "src/engine/tools/")
  add_executable(neurocode src/main.cpp ${NC_APP_SOURCES})
  target_include_directories(neurocode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(neurocode PRIVATE nc_storage Drogon::Drogon "${TORCH_LIBRARIES}")

  # libclang is needed once T4's engine/parse/ files are in the tree.
  file(GLOB_RECURSE NC_ENGINE_PARSE CONFIGURE_DEPENDS src/engine/parse/*.cpp)
  if(NC_ENGINE_PARSE)
    find_library(NC_LIBCLANG NAMES clang libclang
                 PATH_SUFFIXES llvm llvm-14/lib llvm-15/lib llvm-16/lib llvm-17/lib llvm-18/lib)
    find_path(NC_CLANG_INCLUDE clang-c/Index.h
              PATH_SUFFIXES llvm llvm-14/include llvm-15/include llvm-16/include llvm-17/include llvm-18/include)
    if(NC_LIBCLANG AND NC_CLANG_INCLUDE)
      target_link_libraries(neurocode PRIVATE ${NC_LIBCLANG})
      target_include_directories(neurocode PRIVATE ${NC_CLANG_INCLUDE})
    else()
      message(FATAL_ERROR "src/engine/parse/ needs libclang-dev (set NC_LIBCLANG / NC_CLANG_INCLUDE)")
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests. T3 adds test_storage / test_api; T1 and T4 send their own target lines.
# ---------------------------------------------------------------------------
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(test_storage tests/test_storage.cpp)
  target_link_libraries(test_storage PRIVATE nc_storage GTest::gtest_main)
  target_compile_definitions(test_storage PRIVATE NC_DB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/db")
  add_test(NAME storage COMMAND test_storage)
else()
  message(STATUS "GoogleTest not found: test_storage is not built (apt install libgtest-dev)")
endif()
