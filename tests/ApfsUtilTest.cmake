file(MAKE_DIRECTORY "${TEST_DIR}")
execute_process(COMMAND "${FIXTURES}" "${TEST_DIR}"
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if (NOT "${result}" STREQUAL "0")
	message(FATAL_ERROR "Inventory tests failed (${result}):\n${output}\n${errors}")
endif()

function(run expected)
	execute_process(COMMAND "${APFSUTIL}" ${ARGN}
		WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result
		OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 5)
	if (NOT "${result}" STREQUAL "${expected}")
		message(FATAL_ERROR "apfsutil ${ARGN}: expected ${expected}, got ${result}\n${output}\n${errors}")
	endif()
	if ("${output}${errors}" MATCHES "Enter Password|Enter password|Password:")
		message(FATAL_ERROR "Unexpected password prompt: ${ARGN}")
	endif()
	set(output "${output}" PARENT_SCOPE)
	set(errors "${errors}" PARENT_SCOPE)
endfunction()

function(contains text pattern)
	if (NOT "${text}" MATCHES "${pattern}")
		message(FATAL_ERROR "Missing '${pattern}' in:\n${text}")
	endif()
endfunction()
