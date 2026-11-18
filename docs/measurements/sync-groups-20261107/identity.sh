#!/bin/bash
code_identity() {
  local tree=$1 ref=${2:-HEAD}
  git -C "$tree" diff --quiet HEAD -- CMakeLists.txt cmake include src tests || return 1
  git -C "$tree" ls-tree -r "$ref" CMakeLists.txt cmake include src tests | shasum -a256 | cut -d' ' -f1
}
qualification_identity() {
  local after before harness
  after=$(code_identity /Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58) || return 1
  before=$(code_identity /Users/kronuz/code/Kronuz/Detent-wt/s2-before) || return 1
  harness=$(shasum -a256 /Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58/.scratch/cold-bootstrap-5m/coldboot.py)
  printf '%s\n%s\n%s\n' "$after" "$before" "$harness" | shasum -a256 | cut -d' ' -f1
}
