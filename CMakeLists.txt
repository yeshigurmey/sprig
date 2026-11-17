cmake_minimum_required(VERSION 3.16)
project(sprig VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build when the user did not pick one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SPRIG_BUILD_TESTS "Build the unit tests" ON)
option(SPRIG_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Everything except main.cpp lives in a library, so the tests can link the
# compiler directly instead of launching the command line tool.
add_library(sprig_core
  src/ast_printer.cpp
  src/lexer.cpp
  src/parser.cpp
)
target_include_directories(sprig_core PUBLIC src)
target_compile_options(sprig_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

if(SPRIG_SANITIZE)
  target_compile_options(sprig_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(sprig_core PUBLIC -fsanitize=address,undefined)
endif()

if(SPRIG_BUILD_TESTS)
  enable_testing()

  # Use an installed GoogleTest if there is one, otherwise download it.
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
      GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(sprig_tests
    tests/lexer_test.cpp
    tests/parser_test.cpp
  )
  target_link_libraries(sprig_tests PRIVATE sprig_core GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(sprig_tests)

endif()
