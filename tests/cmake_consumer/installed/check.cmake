# Installs BUILD's development component into WORK/prefix, then configures, builds and runs the
# consumer against it (legacy-h A19). Inputs: BUILD, SOURCE, WORK, GENERATOR, CXX, SYSROOT,
# CXX_FLAGS and LINK_FLAGS (the installing build's, so a sanitizer build links its runtime).
file(REMOVE_RECURSE "${WORK}")
function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${ARGN}: exit ${code}\n${out}${err}")
  endif()
  message(STATUS "${out}")
endfunction()
run("${CMAKE_COMMAND}" --install "${BUILD}" --component development --prefix "${WORK}/prefix")
if(EXISTS "${WORK}/prefix/bin/detent-agent" OR EXISTS "${WORK}/prefix/bin/detent")
  message(FATAL_ERROR "the development component installed the binaries")
endif()
run("${CMAKE_COMMAND}" -S "${SOURCE}" -B "${WORK}/build" -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}" "-DCMAKE_EXE_LINKER_FLAGS=${LINK_FLAGS}" "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_OSX_SYSROOT=${SYSROOT}" "-DCMAKE_PREFIX_PATH=${WORK}/prefix")
run("${CMAKE_COMMAND}" --build "${WORK}/build")
run("${WORK}/build/detent_installed_check" "${WORK}/store")
