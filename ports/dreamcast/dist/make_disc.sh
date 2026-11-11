#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Linux/WSL: ./make_disc.sh /path/to/orunners.zip [output folder]   (python3, cc and mkdcdisc needed)
exec python3 "$(dirname "${BASH_SOURCE[0]}")/make_disc.py" "$@"
