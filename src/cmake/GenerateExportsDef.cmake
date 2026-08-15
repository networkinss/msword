# Converts the generated opuscmd_native.inc into a module-definition (.def)
# file listing the same exports.
#
# Why: the original command dispatch resolves each command function at runtime
# with GetProcAddress against WORD1.exe itself, so every command function must
# appear in the executable's export table. MKCMD emits that list as
#   #pragma comment(linker, "/export:Fn")
# directives, which only MSVC's linker honors -- GNU ld ignores the .drectve
# section (the "corrupt .drectve" warning at link time). Feeding ld a .def
# with the same names produces the same export table. The .inc stays the
# single source of truth; this script only re-expresses it.
#
# Invoked as:
#   cmake -DINPUT=<opuscmd_native.inc> -DOUTPUT=<word1_exports.def> -P GenerateExportsDef.cmake

if(NOT DEFINED INPUT)
    message(FATAL_ERROR "GenerateExportsDef: INPUT not set")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "GenerateExportsDef: OUTPUT not set")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "GenerateExportsDef: input not found: ${INPUT}")
endif()

file(STRINGS "${INPUT}" inc_lines)

set(exports "EXPORTS\n")
set(count 0)
foreach(line IN LISTS inc_lines)
    if(line MATCHES "/export:([A-Za-z_][A-Za-z0-9_]*)")
        string(APPEND exports "    ${CMAKE_MATCH_1}\n")
        math(EXPR count "${count} + 1")
    endif()
endforeach()

if(count EQUAL 0)
    message(FATAL_ERROR "GenerateExportsDef: no /export: directives found in ${INPUT}")
endif()

file(WRITE "${OUTPUT}" "${exports}")
message(STATUS "GenerateExportsDef: wrote ${count} exports to ${OUTPUT}")
