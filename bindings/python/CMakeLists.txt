# Section 5's "API surface (bindings and injection)" made concrete for
# Python: a thin pybind11 wrapper around detent::Client, not a
# reimplementation of any evaluation/parsing logic. Requires Python3
# development headers (Python.h) -- opt in with -DDETENT_BUILD_PYTHON=ON.
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

include(FetchContent)
FetchContent_Declare(pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.13.6)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(detent_python detent_python.cc)
target_link_libraries(detent_python PRIVATE detent::detent)
set_target_properties(detent_python PROPERTIES OUTPUT_NAME "detent")

add_custom_target(detent_python_test
  COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=$<TARGET_FILE_DIR:detent_python>"
          ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_detent.py
  DEPENDS detent_python
  COMMENT "python3 bindings/python/tests/test_detent.py"
  VERBATIM)
