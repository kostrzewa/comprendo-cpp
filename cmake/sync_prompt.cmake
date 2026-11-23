# Keep the Claude Code skill's body equal to the shared /chart prompt.
#
#   cmake -DMODE=check -DPROMPT=prompts/chart.md -DSKILL=claude_plugin/skills/chart/SKILL.md -P cmake/sync_prompt.cmake
#   cmake -DMODE=write ...   (rewrite the skill body from the prompt)
#
# The skill keeps its own YAML front matter; everything after it is the prompt.

foreach(variable MODE PROMPT SKILL)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "sync_prompt.cmake: -D${variable}=... is required")
  endif()
endforeach()

file(READ "${PROMPT}" prompt)
file(READ "${SKILL}" skill)

string(SUBSTRING "${skill}" 0 4 opening)
if(NOT opening STREQUAL "---\n")
  message(FATAL_ERROR "${SKILL}: expected YAML front matter starting with ---")
endif()
string(SUBSTRING "${skill}" 4 -1 rest)
string(FIND "${rest}" "\n---\n" closing)
if(closing EQUAL -1)
  message(FATAL_ERROR "${SKILL}: front matter is not closed with ---")
endif()
math(EXPR header_length "4 + ${closing} + 5")
string(SUBSTRING "${skill}" 0 ${header_length} header)
set(expected "${header}\n${prompt}")

if(MODE STREQUAL "write")
  file(WRITE "${SKILL}" "${expected}")
elseif(NOT skill STREQUAL expected)
  message(
    FATAL_ERROR
      "${SKILL} differs from ${PROMPT}; run: cmake -DMODE=write -DPROMPT=${PROMPT} -DSKILL=${SKILL} -P cmake/sync_prompt.cmake"
  )
endif()
