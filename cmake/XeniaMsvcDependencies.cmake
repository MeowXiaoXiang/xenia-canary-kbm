# MSVC can emit UTF-8 diagnostics on an ANSI Windows installation. CMake's
# compiler detection uses ENCODING AUTO, which can corrupt the /showIncludes
# prefix and silently leave Ninja objects with no header dependencies.
function(xe_detect_msvc_include_prefix)
  if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "Ninja" OR
     NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    return()
  endif()

  set(_prefix "")
  foreach(_lang C CXX)
    if(NOT CMAKE_${_lang}_COMPILER_LOADED)
      continue()
    endif()
    separate_arguments(_arg1 NATIVE_COMMAND "${CMAKE_${_lang}_COMPILER_ARG1}")
    execute_process(
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/build/msvc_showincludes.py"
        "${CMAKE_BINARY_DIR}/CMakeFiles/XeniaShowIncludes"
        "${CMAKE_${_lang}_COMPILER}" ${_arg1}
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _detected
      ERROR_VARIABLE _error
      ENCODING UTF-8
    )
    if(NOT "${_result}" STREQUAL "0" OR "${_detected}" STREQUAL "")
      message(FATAL_ERROR "Cannot detect ${_lang} /showIncludes prefix: ${_error}")
    endif()
    if(NOT "${_prefix}" STREQUAL "" AND NOT "${_prefix}" STREQUAL "${_detected}")
      message(FATAL_ERROR "C and C++ /showIncludes prefixes differ")
    endif()
    set(_prefix "${_detected}")
    set(CMAKE_${_lang}_CL_SHOWINCLUDES_PREFIX "${_prefix}" PARENT_SCOPE)
  endforeach()
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_prefix}" PARENT_SCOPE)
endfunction()
