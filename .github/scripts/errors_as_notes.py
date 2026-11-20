#!/usr/bin/env python3
"""Turns a build log's errors into notes on the run, which can be read
without signing in (the log itself can't). usage: errors_as_notes.py LOG"""
import os, re, sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
title = sys.argv[2] if len(sys.argv) > 2 else "build errors"
wanted = sys.argv[3] if len(sys.argv) > 3 else r"\b(fatal )?error\b|\bLNK\d+|undefined reference|ninja: (build stopped|error)|CMake Error"
root = os.getcwd().replace("\\", "/").lower()
seen, lines = set(), []
for raw in text.splitlines():
    line = raw.strip().replace("\\", "/")
    low = line.lower()
    if not re.search(wanted, line, re.I):
        continue
    at = low.find(root)
    if at >= 0:
        line = line[:at] + line[at + len(root) + 1:]
    if line not in seen:
        seen.add(line)
        lines.append(line)
print(f"{len(lines)} different error lines")
# A note holds a few thousand characters, and a step may leave ten of each kind.
chunks, current = [], ""
for line in lines:
    if len(current) + len(line) > 3600:
        chunks.append(current)
        current = ""
    current += line[:700] + "\n"
if current:
    chunks.append(current)
kinds = ["error"] * 9 + ["warning"] * 10 + ["notice"] * 10
for number, chunk in enumerate(chunks[:len(kinds)]):
    body = chunk.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::{kinds[number]} title={title} {number + 1} of {len(chunks)}::{body}")
if not lines:
    tail = "\n".join(text.splitlines()[-40:]).replace("%", "%25").replace("\n", "%0A")
    print(f"::error title={title}, last lines::{tail}")
