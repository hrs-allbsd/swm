#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
# All rights reserved.

set -eu
work=$(mktemp -d)
xvfb_pid=
wm_pid=
cleanup() {
    test -z "$wm_pid" || kill "$wm_pid" 2>/dev/null || :
    test -z "$xvfb_pid" || kill "$xvfb_pid" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp 3>"$work/display" >"$work/xvfb.log" 2>&1 &
xvfb_pid=$!
i=0
while test ! -s "$work/display"; do
    i=$((i + 1))
    if test "$i" -gt 50; then cat "$work/xvfb.log"; exit 1; fi
    sleep 0.1
done
DISPLAY=:$(cat "$work/display")
export DISPLAY
./swm >"$work/wm.log" 2>&1 &
wm_pid=$!
sleep 0.2
if ./swm >"$work/second.log" 2>&1; then
    echo 'FAIL: second WM succeeded'; exit 1
fi
./tests/client
kill -0 "$wm_pid"
test ! -s "$work/wm.log" || { cat "$work/wm.log"; exit 1; }
echo 'PASS: second WM rejected'
