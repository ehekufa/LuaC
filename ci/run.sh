#!/bin/sh
# Run a CI command and retain compiler diagnostics as a commit comment on failure.
set -u
LOG="${RUNNER_TEMP:-/tmp}/luac-command.log"
"$@" >"$LOG" 2>&1
STATUS=$?
cat "$LOG"
if [ "$STATUS" -ne 0 ] && [ "${CI:-}" = "true" ]; then
  export LUAC_LOG="$LOG"
  python - <<'PY'
import json, os
from pathlib import Path
text = Path(os.environ['LUAC_LOG']).read_text(errors='replace')[-50000:]
title = os.environ.get('CI_LOG_NAME', 'Native build command')
Path(os.environ['LUAC_LOG'] + '.json').write_text(json.dumps({
    'body': '### ' + title + '\n\n```text\n' + text + '\n```'
}))
PY
  if command -v gh >/dev/null 2>&1; then
    GH=gh
  else
    GH='/c/Program Files/GitHub CLI/gh.exe'
  fi
  "$GH" api "repos/$GITHUB_REPOSITORY/commits/$GITHUB_SHA/comments" \
    --input "$LOG.json" || true
fi
exit "$STATUS"
