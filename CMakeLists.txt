cmake_minimum_required(VERSION 3.24)

project(fit VERSION 0.1.0 DESCRIPTION "Fixed income toolkit" LANGUAGES CXX)

option(FIT_BUILD_TESTS "Build the unit and CLI tests" ON)
option(FIT_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FIT_WERROR "Treat compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Common compile settings for every first-party target.
add_library(fit_options INTERFACE)
target_compile_options(fit_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
if(FIT_WERROR)
  target_compile_options(fit_options INTERFACE -Werror)
endif()
if(FIT_SANITIZE)
  target_compile_options(fit_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fit_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(fit_core STATIC
  src/time/date.cpp
  src/time/calendar.cpp
  src/time/daycount.cpp
  src/time/schedule.cpp
  src/math/solver.cpp
  src/math/interpolation.cpp
  src/curve/yield_curve.cpp
  src/curve/bootstrap.cpp
  src/curve/curve_set.cpp
  src/bond/bond.cpp
  src/bond/analytics.cpp
  src/bond/horizon.cpp
  src/portfolio/portfolio.cpp
  src/io/csv.cpp
  src/io/table.cpp
)
target_include_directories(fit_core PUBLIC include)
target_link_libraries(fit_core PRIVATE fit_options)

add_executable(fit
  src/cli/main.cpp
  src/cli/args.cpp
  src/cli/common.cpp
  src/cli/cmd_curve.cpp
  src/cli/cmd_bond.cpp
  src/cli/cmd_portfolio.cpp
)
target_link_libraries(fit PRIVATE fit_core fit_options)

install(TARGETS fit RUNTIME DESTINATION bin)

if(FIT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
