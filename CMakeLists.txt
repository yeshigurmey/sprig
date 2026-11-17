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
  src/bytecode.cpp
  src/codegen.cpp
  src/driver.cpp
  src/lexer.cpp
  src/optimizer.cpp
  src/parser.cpp
  src/sema.cpp
  src/vm.cpp
)
target_include_directories(sprig_core PUBLIC src)
target_compile_options(sprig_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

if(SPRIG_SANITIZE)
  target_compile_options(sprig_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(sprig_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(sprig src/main.cpp)
target_link_libraries(sprig PRIVATE sprig_core)

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
    tests/sema_test.cpp
    tests/optimizer_test.cpp
    tests/vm_test.cpp
    tests/examples_test.cpp
  )
  target_link_libraries(sprig_tests PRIVATE sprig_core GTest::gtest_main)
  # Lets examples_test.cpp find the example programs no matter where the tests are run from.
  target_compile_definitions(sprig_tests PRIVATE SPRIG_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")

  include(GoogleTest)
  gtest_discover_tests(sprig_tests)

  # End to end checks of the command line tool itself: what it prints for a
  # good program, a program with a compile error, and one that fails while running.
  add_test(NAME cli_runs_program COMMAND sprig ${CMAKE_CURRENT_SOURCE_DIR}/examples/gcd.sprig)
  set_tests_properties(cli_runs_program PROPERTIES PASS_REGULAR_EXPRESSION "^21\n21\n1\ntrue\n$")

  add_test(NAME cli_reports_compile_error COMMAND sprig ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/type_error.sprig)
  set_tests_properties(cli_reports_compile_error PROPERTIES
    PASS_REGULAR_EXPRESSION "type_error.sprig:3:7: error: 'y' is not declared")

  add_test(NAME cli_reports_runtime_error COMMAND sprig ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/runtime_error.sprig)
  set_tests_properties(cli_reports_runtime_error PROPERTIES
    PASS_REGULAR_EXPRESSION "runtime_error.sprig:6: runtime error: division by zero")

  add_test(NAME cli_rejects_unknown_option COMMAND sprig --nope)
  set_tests_properties(cli_rejects_unknown_option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option")
endif()
