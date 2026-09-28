#!/bin/bash
# usage: tools/shot.sh <prefix> <steps.json> [query]
T=/home/user/Frontier/tarantula/tools
cd "$(dirname "$0")"
LD_LIBRARY_PATH=$T/libs/lib timeout 900 node $T/run.mjs "http://localhost:5174/?manual&${3:-}" out/$1 $2
