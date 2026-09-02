#!/usr/bin/env bash
# PreToolUse guard for the Bash tool.
# Reads the tool call as JSON on stdin. Exit 2 blocks the command and sends the
# reason (stderr) back to Claude. Exit 0 lets normal permissions decide.
#
# Why this exists alongside permissions.deny in settings.json: deny rules are
# prefix matches on the whole command and miss compound or multiline commands
# ("git status && git push --force"). This script sees the entire string.
# Parses the JSON with jq, python3, or node, whichever is installed.

set -u

input=$(cat)

if command -v jq >/dev/null 2>&1; then
  cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty')
elif command -v python3 >/dev/null 2>&1; then
  cmd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))')
elif command -v node >/dev/null 2>&1; then
  cmd=$(printf '%s' "$input" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s);process.stdout.write((j.tool_input&&j.tool_input.command)||"")})')
else
  echo "guard.sh: none of jq, python3, or node found; guard is inactive. Install one to enforce git and secret rules." >&2
  exit 0
fi
[ -z "$cmd" ] && exit 0

block() {
  printf 'Blocked by .claude/hooks/guard.sh: %s.\nIf this is really needed, ask the user to run it themselves.\n' "$1" >&2
  exit 2
}

matches() { printf '%s' "$cmd" | grep -Eiq -- "$1"; }

# Git: history and remote safety
matches '\bgit\s+push\b.*(\s--force\b|\s-f\b|\s--force-with-lease\b)' && block "force push"
matches '\bgit\s+push\b.*\b(main|master)\b'                           && block "push to main or master"
matches '\bgit\s+reset\s+--hard\b'                                    && block "git reset --hard"
matches '\bgit\s+checkout\s+--\s+\.(\s|$)'                            && block "git checkout -- . (discards all working changes)"
matches '\bgit\s+restore\s+(\.|--worktree\s+\.)(\s|$)'                && block "git restore . (discards all working changes)"
matches '\bgit\s+clean\s+-[a-zA-Z]*f'                                 && block "git clean -f"
matches '\bgit\s+branch\s+(-D|--delete\s+--force)\b'                  && block "force-deleting a branch"
matches '\bgit\s+stash\s+(drop|clear)\b'                              && block "dropping stashes"
matches '\bgit\s+(commit|push|merge|rebase)\b.*--no-verify\b'         && block "--no-verify (skips hooks)"
matches '\bgit\s+add\s+(-A|--all|\.)(\s|$)'                           && block "git add -A / git add . (stage named paths instead)"
matches '\bgit\s+filter-(branch|repo)\b'                              && block "history rewrite"

# Filesystem and privilege
matches '\brm\s+(-[a-zA-Z]*r[a-zA-Z]*|--recursive)\b.*\s(/|~|\$HOME|\.\.)(\s|$|/)' && block "recursive delete outside the repo"
matches '(^|[;&|[:space:]])sudo\s'                                    && block "sudo"
matches '\b(mkfs|dd\s+if=|shred)\b'                                   && block "disk-level destructive command"

# Secrets: reading or copying credential files through the shell
matches '\b(cat|less|more|head|tail|grep|cp|scp|base64|xxd|strings)\b.*(\.env(\.|\s|$)|id_rsa|id_ed25519|\.pem\b|\.key\b|/secrets/)' && block "reading secret files"

exit 0
