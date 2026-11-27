add_executable(fit_unit_tests
  unit/main.cpp
  unit/test_time.cpp
  unit/test_math.cpp
  unit/test_curve.cpp
  unit/test_curve_set.cpp
  unit/test_parametric.cpp
  unit/test_bond.cpp
  unit/test_portfolio.cpp
  unit/test_io.cpp
)
target_include_directories(fit_unit_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(fit_unit_tests PRIVATE fit_core fit_options)
target_compile_definitions(fit_unit_tests PRIVATE
  FIT_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/data")

add_test(NAME unit COMMAND fit_unit_tests)

# The release tool's tests (tools/release.py), including the check that VERSION and
# CHANGELOG.md agree. Python is a development tool only, never a build dependency.
find_package(Python3 3.10 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME release-tool
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
else()
  message(STATUS "Python 3.10+ not found: the release-tool tests are skipped")
endif()

find_program(BASH_PROGRAM bash)
if(BASH_PROGRAM)
  add_test(NAME cli
    COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_cli_tests.sh $<TARGET_FILE:fit>
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
