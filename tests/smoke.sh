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
# Invalid invocations must fail before attempting to connect to X11.
reject_usage() {
    if DISPLAY= ./swm "$@" >"$work/usage.log" 2>&1; then
        echo 'FAIL: invalid invocation succeeded'; exit 1
    fi
    case $(cat "$work/usage.log") in
        'Usage: swm command [args ...]') ;;
        *) cat "$work/usage.log"; exit 1 ;;
    esac
}
reject_usage
reject_usage ''
DISPLAY= ./swm --help >"$work/help.log"
echo 'PASS: required command and help without DISPLAY'

if test "${SWM_TEST_SERVER:-Xvfb}" = Xvnc; then
    Xvnc -displayfd 3 -geometry 800x600 -SecurityTypes None \
        -rfbport -1 -rfbunixpath "$work/vnc" -nolisten tcp \
        3>"$work/display" >"$work/xvfb.log" 2>&1 &
else
    Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp \
        3>"$work/display" >"$work/xvfb.log" 2>&1 &
fi
xvfb_pid=$!
i=0
while test ! -s "$work/display"; do
    i=$((i + 1))
    if test "$i" -gt 50; then cat "$work/xvfb.log"; exit 1; fi
    sleep 0.1
done
DISPLAY=:$(cat "$work/display")
export DISPLAY
if ./swm /swm-test-nonexistent-command >"$work/exec.log" 2>&1; then
    echo 'FAIL: nonexistent command succeeded'; exit 1
fi
case $(cat "$work/exec.log") in
    'swm: cannot execute /swm-test-nonexistent-command:'*) ;;
    *) cat "$work/exec.log"; exit 1 ;;
esac
./swm sh tests/launch.sh 'argument with spaces' '$literal' \
    "$work/result" >"$work/wm.log" 2>&1 &
wm_pid=$!
sleep 0.2
if ./swm sh -c 'touch "$1"' sh "$work/unexpected" \
    >"$work/second.log" 2>&1; then
    echo 'FAIL: second WM succeeded'; exit 1
fi
case $(cat "$work/second.log") in
    'swm: another window manager is running') ;;
    *) cat "$work/second.log"; exit 1 ;;
esac
test ! -e "$work/unexpected"
i=0
while test ! -s "$work/result"; do
    i=$((i + 1))
    if test "$i" -gt 100 || ! kill -0 "$wm_pid" 2>/dev/null; then
        cat "$work/wm.log"; echo 'FAIL: launched tests did not complete'; exit 1
    fi
    sleep 0.1
done
i=0
while kill -0 "$wm_pid" 2>/dev/null; do
    i=$((i + 1))
    if test "$i" -gt 50; then
        echo 'FAIL: WM did not exit with its application'; exit 1
    fi
    sleep 0.1
done
wait "$wm_pid"
wm_pid=
# Also check immediate exits when there are no X events to wake the WM.
status=0
timeout 5 ./swm sh -c 'exit 37' || status=$?
test "$status" -eq 37 || { echo "FAIL: exit status $status"; exit 1; }
status=0
timeout 5 ./swm sh -c 'kill -TERM $$' || status=$?
test "$status" -eq 143 || { echo "FAIL: signal exit status $status"; exit 1; }
timeout 5 ./swm true
cat "$work/wm.log"
echo 'PASS: application launch, literal arguments, exec failure, second WM rejected, application exit/status'
