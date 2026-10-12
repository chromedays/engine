# Writes a text file as a C header holding one string: cmake -DINPUT=<file> -DOUTPUT=<header> -DNAME=<identifier> -P embed_text.cmake
# The text is split into string literals a line each, so the header reads like the file.
file(READ "${INPUT}" text)
string(REPLACE "\\" "\\\\" text "${text}")
string(REPLACE "\"" "\\\"" text "${text}")
string(REPLACE "\r" "" text "${text}")
string(REPLACE ";" "\\073" text "${text}") # a semicolon would split the list below
string(REPLACE "\n" ";" lines "${text}")
set(body "")
foreach(line IN LISTS lines)
    string(REPLACE "\\073" ";" line "${line}")
    string(APPEND body "    \"${line}\\n\"\n")
endforeach()
file(WRITE "${OUTPUT}.tmp" "// Generated from ${INPUT} by cmake/embed_text.cmake. Do not edit.\n#pragma once\nstatic const char ${NAME}[] =\n${body};\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
