#!/bin/sh
# python3 wrapper for the Emscripten docker image: its python lacks PyYAML
# (needed by tools/gen_text.py). Install it on first use, then exec.
if ! python3 -c 'import yaml' 2>/dev/null; then
	pip install --quiet --disable-pip-version-check pyyaml >&2
fi
exec python3 "$@"
