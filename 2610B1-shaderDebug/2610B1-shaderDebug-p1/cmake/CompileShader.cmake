file(READ "${SOURCE}" SHADER_TEXT)
# GLSL requires #version before conditional directives. Keep the original
# source unchanged so it can also be copied into a C++ header.
string(REGEX MATCH "#version[^\r\n]*" VERSION_LINE "${SHADER_TEXT}")
if(VERSION_LINE)
    string(REPLACE "${VERSION_LINE}" "" SHADER_TEXT "${SHADER_TEXT}")
    set(SHADER_TEXT "${VERSION_LINE}\n${SHADER_TEXT}")
endif()
get_filename_component(STAGED_DIRECTORY "${STAGED}" DIRECTORY)
file(MAKE_DIRECTORY "${STAGED_DIRECTORY}")
file(WRITE "${STAGED}" "${SHADER_TEXT}")
execute_process(
    COMMAND "${COMPILER}" -V "${STAGED}" -o "${OUTPUT}"
    RESULT_VARIABLE COMPILE_RESULT
)
if(NOT COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR "Shader compilation failed: ${SOURCE}")
endif()
