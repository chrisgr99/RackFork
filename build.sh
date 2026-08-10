#!/bin/sh
# Build Rack with the version PINNED.
#
# Why this script exists rather than plain `make`:
#
# The Makefile derives the version from git, using
#   git describe --tags --match "v2.*"
# HEAD currently sits exactly on v2.6.6, so plain make computes 2.6.6. But the
# moment there are commits on the `mods` branch, git describe returns something
# like v2.6.6-3-g1a2b3c4d, and that string gets compiled in as APP_VERSION.
#
# That would break the "never change APP_VERSION" rule by accident — you would
# not have edited a version anywhere, just committed. Library sync sends this
# string to the VCV API and it is written into settings.json.
#
# Passing RACK_VERSION on the make command line overrides the Makefile's soft
# assignment and any environment value, so the version stays put no matter what
# the git history looks like.
#
# Extra arguments are forwarded, e.g.  ./build.sh clean

cd "$(dirname "$0")" || exit 1
exec make RACK_VERSION=2.6.6 -j10 "$@"
