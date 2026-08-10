#!/bin/sh
# NORMAL MODE — this is the one to use.
#
# Dev mode is OFF, so the Library menu is fully functional and you can log in and
# install third-party plugins. Settings, plugins and patches live in
# ~/Documents/RackFork, keeping this fork isolated from any stock Rack install.
#
# The cd is required, not cosmetic: the Rack executable links libRack.dylib by
# bare relative name, so it only runs with the repo root as the working directory.
# It also makes Rack resolve its system directory (res/, Core.json, cacert.pem,
# translations) to this repo.
#
# Do not use `make run` — that passes -d and leaves you without a Library.

cd "$(dirname "$0")" || exit 1
exec ./Rack -u "$HOME/Documents/RackFork" "$@"
