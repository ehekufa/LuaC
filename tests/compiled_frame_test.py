import json
import subprocess
import sys

lines = subprocess.check_output([sys.argv[1]], text=True).splitlines()
assert len(lines) == 4, lines
initial, moved, other, error = map(json.loads, lines)
assert initial["ok"] and initial["output"] == "Compiled project ready\n"
assert initial["entities"][0]["x"] == 0 and initial["entities"][0]["width"] == 24
assert initial["camera"] == {"x": 0, "y": 0, "zoom": 1}
assert initial["texts"][0]["text"] == 'Hello "\nПривет\0tail\ufffd'
assert initial["love"] is None and initial["error"] is None
assert moved["ok"] and moved["output"] == "" and moved["entities"][0]["x"] == 5
assert other["output"] == "Compiled project ready\n" and other["entities"][0]["x"] == 0
assert not error["ok"] and 'project"\nname' in error["error"]
assert "frame time" in error["error"]
print("Compiled frame JSON and session isolation: OK")
