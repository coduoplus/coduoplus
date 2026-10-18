#!/usr/bin/env bash
# Disassemble a VA range of a PE with MinGW objdump (Intel syntax).
# Usage: tools/re/dis.sh <binary> <start-va> [<length>=0x60]
set -euo pipefail
bin="$1"; start=$(( $2 )); len=$(( ${3:-0x60} ))
objdump="${OBJDUMP:-/c/tools/mingw32/bin/objdump}"
"$objdump" -d -M intel --start-address=$start --stop-address=$(( start + len + 16 )) "$bin" |
  grep -E '^ *[0-9a-f]+:' | awk -v stop=$(( start + len )) '{ a = strtonum("0x" substr($1, 1, length($1)-1)); if (a < stop) print }'
