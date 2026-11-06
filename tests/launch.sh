#!/bin/sh
# Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
#
# SPDX-License-Identifier: BSD-2-Clause

set -eu
test "$#" -eq 3
test "$1" = 'argument with spaces'
test "$2" = '$literal'
./tests/client
printf 'success\n' >"$3"
