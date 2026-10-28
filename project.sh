#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "${RC_PYTHON:-python3}" "$root/tools/project.py" "$@"

