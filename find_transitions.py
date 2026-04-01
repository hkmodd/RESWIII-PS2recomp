import sys

logfile = "logs/agent_run_latest.log"
prev = ""
count = 0
with open(logfile, "r", encoding="utf-8", errors="replace") as f:
    for i, line in enumerate(f, 1):
        line = line.rstrip()
        if line != prev:
            print(f"L{i}: {line[:200]}")
            count += 1
            if count >= 60:
                break
        prev = line
