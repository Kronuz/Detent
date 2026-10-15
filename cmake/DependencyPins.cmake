# First declaration wins, including dependencies fetched by sibling libraries.
function(detent_declare_dependencies)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies.json")
  file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies.json" manifest)
  string(JSON count LENGTH "${manifest}")
  math(EXPR last "${count} - 1")
  foreach(index RANGE ${last})
    foreach(field name repository revision source_subdir)
      string(JSON ${field} GET "${manifest}" ${index} ${field})
    endforeach()
    string(LENGTH "${revision}" revision_length)
    if(NOT revision_length EQUAL 40 OR NOT revision MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "Dependency ${name} requires a full immutable commit SHA")
    endif()
    FetchContent_Declare(${name}
      GIT_REPOSITORY "${repository}"
      GIT_TAG "${revision}"
      SOURCE_SUBDIR "${source_subdir}")
  endforeach()
endfunction()
detent_declare_dependencies()
