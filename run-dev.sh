#!/bin/sh
# DEV MODE — debugging only.
#
# Verified against src/asset.cpp, src/library.cpp and src/app/MenuBar.cpp:
#   - logging goes to stderr instead of log.txt
#   - the fatal signal handlers are NOT installed, so crashes drop straight out
#   - the user directory becomes this repo, not ~/Documents/RackFork
#   - library sync is short-circuited and the Library menu shows an inert
#     "dev mode" label instead of login and install items
#
# So you cannot install plugins in this mode. Use run-library.sh for normal work.

cd "$(dirname "$0")" || exit 1
exec ./Rack -d "$@"
