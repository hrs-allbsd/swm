#!/usr/bin/env python3
# Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
#
# SPDX-License-Identifier: BSD-2-Clause

"""Test swm against Xsixel using a simulated terminal on an isolated PTY.

Run with XSIXEL=/path/to/Xsixel python3 tests/xsixel.py after make tests/client.
Requires Xsixel, xmessage and xdotool. No real terminal is modified.
"""

import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import threading
import time


def main():
    root = Path(__file__).resolve().parent.parent
    env = {key: value for key, value in os.environ.items()
           if key not in ('TMUX', 'TMUX_PANE', 'STY', 'TERM_PROGRAM', 'LC_TERMINAL')}
    env['TERM'] = 'xterm-256color'
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 80, 800, 600))
    output = bytearray()
    stopped = threading.Event()

    def drain():
        while not stopped.is_set():
            if select.select([master], [], [], .1)[0]:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    return
                if not data:
                    return
                output.extend(data)
                del output[:-1024 * 1024]

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    server = wm = None
    rfd, wfd = os.pipe()
    with tempfile.TemporaryFile() as log:
        try:
            server = subprocess.Popen(
                [os.environ.get('XSIXEL', 'Xsixel'), '-displayfd', str(wfd),
                 '-nolisten', 'tcp', '-ac'], stdin=slave, stdout=slave,
                stderr=log, env=env, pass_fds=(wfd,), start_new_session=True)
            os.close(wfd)
            wfd = -1
            assert select.select([rfd], [], [], 10)[0], 'Xsixel readiness timeout'
            display = os.read(rfd, 64).decode().strip()
            assert display, 'Xsixel exited before readiness'
            env['DISPLAY'] = ':' + display
            wm = subprocess.Popen(
                [str(root / 'swm'), 'xmessage', '-name', 'swm-xsixel-test',
                 'Close this application to end swm'], env=env, cwd=root,
                stdout=log, stderr=log)
            window = None
            for _ in range(100):
                result = subprocess.run(
                    ['xdotool', 'search', '--name', 'swm-xsixel-test'], env=env,
                    capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    window = result.stdout.splitlines()[0]
                    break
                assert wm.poll() is None, 'swm exited before mapping the app'
                time.sleep(.05)
            assert window, 'application window not found'
            for width, height in [(800, 600), (640, 480), (1000, 700), (800, 600)]:
                fcntl.ioctl(slave, termios.TIOCSWINSZ,
                            struct.pack('HHHH', 30, 80, width, height))
                os.kill(server.pid, signal.SIGWINCH)
                os.write(master, f'\x1b[4;{height};{width}t\x1b[6;20;10t'.encode())
                subprocess.run([str(root / 'tests/client'), '--check', window,
                                str(width), str(height)], env=env, check=True,
                               timeout=5, stdout=log, stderr=log)
                print(f'PASS: Xsixel {width}x{height}, maximization and title pixels')
            assert b'\x1bP' in output, 'Xsixel produced no SIXEL output'
            subprocess.run(['xdotool', 'key', 'alt+w'], env=env, check=True, timeout=5)
            wm.wait(timeout=5)
            print('PASS: closing the application also exits swm')
        except BaseException:
            log.seek(0)
            print(log.read().decode(errors='replace'))
            raise
        finally:
            for process in (wm, server):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            stopped.set()
            reader.join(timeout=2)
            for fd in (master, slave, rfd, wfd):
                if fd != -1:
                    os.close(fd)


if __name__ == '__main__':
    main()
