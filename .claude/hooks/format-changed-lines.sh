#!/bin/bash
# PostToolUse(Write|Edit): clang-format only the lines changed in our own sources (main/).
# Tracked files: format just the lines that differ from HEAD (avoids reformatting untouched code).
# New untracked files: format the whole file.
f=$(jq -r '.tool_input.file_path // .tool_response.filePath // empty')
case "$f" in
  */main/*.c|*/main/*.cpp|*/main/*.h|*/main/*.hpp) ;;
  *) exit 0 ;;
esac
cd "${CLAUDE_PROJECT_DIR:-$(dirname "$0")/../..}" || exit 0
if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
  git clang-format --force --quiet -- "$f" >/dev/null
else
  clang-format -i "$f"
fi
exit 0
