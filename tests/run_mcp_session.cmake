# Replay a recorded MCP session against a copy of a chart, the way /chart edits it.
#
#   cmake -DCLI=comprendo-cli -DSOURCE=chart.sqlite -DPROJECT=dir -DSESSION=session.jsonl -P run_mcp_session.cmake
#
# Fails when the server exits with an error or any reply is a JSON-RPC error or
# a tool error (ids in the session refer to nodes of SOURCE).

foreach(variable CLI SOURCE PROJECT SESSION)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "run_mcp_session.cmake: -D${variable}=... is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${PROJECT}/.comprendo")
file(MAKE_DIRECTORY "${PROJECT}/.comprendo")
file(COPY_FILE "${SOURCE}" "${PROJECT}/.comprendo/chart.sqlite")

execute_process(
  COMMAND "${CLI}" mcp --root "${PROJECT}"
  INPUT_FILE "${SESSION}"
  OUTPUT_VARIABLE replies
  RESULT_VARIABLE exit_code
)
message("${replies}")
if(NOT exit_code EQUAL 0)
  message(FATAL_ERROR "comprendo-cli mcp exited with ${exit_code}")
endif()
if(replies MATCHES "\"isError\":true" OR replies MATCHES "\"error\":{")
  message(FATAL_ERROR "the MCP session reported an error")
endif()
