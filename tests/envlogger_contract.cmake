if(NOT DEFINED LUMORA OR NOT DEFINED FIXTURE OR NOT DEFINED LOG)
    message(FATAL_ERROR "LUMORA, FIXTURE, and LOG are required")
endif()

file(REMOVE "${LOG}")
execute_process(
    COMMAND "${LUMORA}" --envlogger --envlogger-output "${LOG}" "${FIXTURE}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "envlogger fixture failed (${run_result}): ${run_stdout}${run_stderr}")
endif()
if(NOT EXISTS "${LOG}")
    message(FATAL_ERROR "envlogger did not create ${LOG}")
endif()
file(READ "${LOG}" log_data)
string(FIND "${log_data}" [=["api":"loadstring"]=] has_loadstring)
string(FIND "${log_data}" [=["api":"print"]=] has_print)
string(FIND "${log_data}" [=[envlogger-runtime-ok]=] has_print_args)
string(FIND "${log_data}" [=["api":"Instance:GetService"]=] has_method)
string(FIND "${log_data}" [=["api":"request"]=] has_request)
string(FIND "${log_data}" [=["event":"blocked-network"]=] has_blocked_network)
string(FIND "${log_data}" [=["truncated":false]=] has_summary)
foreach(found IN ITEMS has_loadstring has_print has_print_args has_method has_request has_blocked_network has_summary)
    if(${found} EQUAL -1)
        message(FATAL_ERROR "expected runtime log marker missing: ${found}")
    endif()
endforeach()
