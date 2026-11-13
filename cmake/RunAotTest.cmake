if(NOT DEFINED LUAC OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "RunAotTest requires LUAC, SOURCE and OUTPUT")
endif()
execute_process(
    COMMAND "${LUAC}" --aot "${SOURCE}" -o "${OUTPUT}"
    WORKING_DIRECTORY "${WORKDIR}"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "AOT compilation failed: ${compile_output}${compile_error}")
endif()
execute_process(
    COMMAND "${OUTPUT}"
    WORKING_DIRECTORY "${WORKDIR}"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "AOT program failed: ${run_output}${run_error}")
endif()
if(NOT DEFINED EXPECTED)
    set(EXPECTED "LuaC compatibility: OK")
endif()
if(NOT run_output MATCHES "${EXPECTED}")
    message(FATAL_ERROR "Unexpected AOT output: ${run_output}")
endif()
