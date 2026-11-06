# swm

[日本語](README-ja.md)

swm is a small X11 window manager for running applications in Xsixel.
It is written in C and uses Xlib and libXrandr. It has no window-switching
commands, workspaces, or configuration file; use screen or tmux
in the terminal for session management.

## Behavior

- Normal windows fill the screen below a contrasting 24-pixel title bar.
  The close mark and bottom border are drawn without relying on fonts.
- Left-click the `X` at the right of the title bar, or press **Alt-w**, to
  send `WM_DELETE_WINDOW` to the focused application. The application can
  display its usual save or exit confirmation.
- Applications without this protocol are terminated with `XKillClient`.
  This may also close other windows sharing the same X connection.
- Newly opened windows appear in front. Closing one returns focus to a
  remaining window.
- Alt-w works with Caps Lock and Num Lock enabled.
- Windows already visible at startup are managed as well.
- RandR screen-size notifications keep normal windows maximized as the
  Xsixel terminal resizes. Dialogs remain centered.

### Confirmation dialogs

Windows with a parent specified through `WM_TRANSIENT_FOR`, or with the
`_NET_WM_WINDOW_TYPE_DIALOG` type, keep their requested size and appear in
the center of the screen. Oversized dialogs are reduced to fit the screen.
Dialogs also have a title bar.

A transient dialog stays above its parent. If the parent receives focus,
swm redirects focus to its open dialog, including nested dialogs. Alt-w
acts on the front dialog; closing it returns focus to its parent. For
simplicity, other transient utility windows receive the same treatment.

Confirmation UI drawn inside a browser page is managed by the browser.
Applications also manage their own `override_redirect` windows, such as
menus and tooltips.

## Build

You need a C compiler, make, pkg-config, and the Xlib/libXrandr development
headers and libraries. On Debian or Ubuntu, install `build-essential pkg-config
libx11-dev libxrandr-dev`.

```sh
make
```

## Run with Xsixel

Start [Xsixel](https://github.com/csdvrx/xsixel), then run swm on the same
X server in place of ratpoison. For example, if Xsixel uses DISPLAY `:1`:

```sh
DISPLAY=:1 ./swm xterm
```

A command is required; `-exec` is no longer used. The first argument is
the program and the rest are passed directly as its arguments. Programs
are looked up in `PATH`. For example, `swm xterm -e top` starts xterm
with its own arguments. For shell syntax, invoke a shell explicitly:
`swm sh -c 'exec xterm'`.

The application inherits the environment, including `DISPLAY`, and starts
after the window manager is ready. swm exits when this process exits and
returns its exit status (or 128 plus the signal number). Keep the launched
application in the foreground; a launcher that forks and exits also ends
swm. If the command cannot be executed, swm reports an error and exits.
Use `swm --help` to show usage without starting X11.

Xsixel must report the terminal size to X11 for maximization to follow it.
Use Xsixel's automatic sizing; a fixed `-screen` size does not follow the
terminal. swm does not change terminal or multiplexer settings.

swm refuses to start if another window manager is already running.
To stop swm itself, use the launching shell's job control or
`kill <swm-pid>`. Alt-w closes application windows. If the terminal
intercepts Alt-w, adjust its key bindings or use the title-bar button.

Optional installation:

```sh
make install PREFIX="$HOME/.local"
```

## Tests and limitations

```sh
make test
```

The tests also require Xvfb and xdotool. They use a separate virtual X
server to check maximization, resize requests, transient, nested, and
typed dialogs, focus restoration, remapping, Alt-w, lock keys, the close
button, and rejection of a second window manager. They also check the
required command, application startup, argument forwarding, execution
failures, title-bar pixels, and automatic exit with the application.

`make test-resize` additionally requires TigerVNC's Xvnc and xrandr. It
checks real RandR shrink/grow/restore operations and dialog positioning.
Xvnc listens only on an isolated local Unix socket.

For a PTY integration test against Xsixel, also install Python 3 and xmessage:

```sh
XSIXEL=/path/to/Xsixel python3 tests/xsixel.py
```

This test simulates terminal size replies, verifies maximization and
visible title-bar pixels, and checks exit after closing the application.

Actual GUI terminal/multiplexer combinations and real browsers still need
verification; the PTY test does not replace those checks.
Full ICCCM or EWMH support is outside the scope of this project. swm does
not implement application minimum-size or aspect-ratio hints, minimization,
application-requested fullscreen toggling, or per-monitor layouts.
UTF-8 titles are read from `_NET_WM_NAME` and drawn with an Xlib font set;
character coverage depends on the locale and available X fonts.

## Author and license

Author: Hiroki Sato <hrs@allbsd.org>

Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>.
Distributed under the BSD 2-Clause license. See [LICENSE](LICENSE).
