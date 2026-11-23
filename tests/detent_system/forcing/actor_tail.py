"""Print member lifecycle events and the last output of each authority member from a run's events.jsonl."""
import json, sys
from collections import defaultdict
out = defaultdict(str)
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    try:
        e = json.loads(line)
    except ValueError:
        continue
    kind = e.get("event") or e.get("kind") or e.get("type")
    if e.get("name", "").startswith("authority") and "text" in e:
        out[e["name"]] += e["text"]
    elif kind and kind != "actor_output" and any(w in json.dumps(e) for w in ("member", "outage", "exit", "code")):
        print("event:", json.dumps(e)[:300])
for name, text in sorted(out.items()):
    print(f"=== {name} (last 3000 bytes)")
    print(text[-3000:])
