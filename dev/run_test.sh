#!/bin/bash
# usage: dev/run_test.sh <tag> <script.py> [NAME=value ...]
# Runs one of dev/tests headless against the library in dist\FielDes (the one copy); the log goes to dev/logs/<tag>.log
R=/c/Users/tinmi/OneDrive/FielDes/FielDes
PY=/c/dev/vcpkg/installed/x64-windows/tools/python3/python.exe
TAG=$1; SCRIPT=$2; shift 2
export FIELDES_DIR='C:\Users\tinmi\OneDrive\FielDes\FielDes\dist\FielDes' FIELDES_FLOW_DEBUG=1 PYTHONUNBUFFERED=1
for kv in "$@"; do export "$kv"; done
cd $R
echo "== $TAG $(date +%H:%M:%S) $*"
case "$SCRIPT" in /*|[A-Za-z]:*) P="$SCRIPT";; *) P="$(cygpath -w $R/dev/tests/$SCRIPT)";; esac
timeout 1500 $PY scripts/run_example.py "$P" > $R/dev/logs/$TAG.log 2>&1
echo "rc=$?  $(date +%H:%M:%S)"
grep -v "^\[bar" $R/dev/logs/$TAG.log | cut -c1-250
