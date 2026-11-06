# swm

[日本語](README-ja.md)

swm is a small X11 window manager for running applications in Xsixel.
It is written in C and depends only on Xlib. It has no window-switching
commands, workspaces, or configuration file; use screen or tmux
in the terminal for session management.

## Behavior

- Normal windows fill the screen below a 24-pixel title bar.
- Left-click the `X` at the right of the title bar, or press **Alt-w**, to
  send `WM_DELETE_WINDOW` to the focused application. The application can
  display its usual save or exit confirmation.
- Applications without this protocol are terminated with `XKillClient`.
  This may also close other windows sharing the same X connection.
- Newly opened windows appear in front. Closing one returns focus to a
  remaining window.
- Alt-w works with Caps Lock and Num Lock enabled.
- Windows already visible at startup are managed as well.
- Windows are laid out again when the X server reports a screen-size change.

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

You need a C compiler, make, pkg-config, and the Xlib development headers
and libraries. On Debian or Ubuntu, install `build-essential pkg-config
libx11-dev`.

```sh
make
```

## Run with Xsixel

Start [Xsixel](https://github.com/csdvrx/xsixel), then run swm on the same
X server in place of ratpoison. For example, if Xsixel uses DISPLAY `:1`:

```sh
DISPLAY=:1 ./swm -exec xterm
```

The `-exec` option and a command are required. All following arguments
are passed directly to the application; commands are looked up in `PATH`.
For example, `swm -exec xterm -e top` starts xterm with its own arguments.
For shell syntax, invoke a shell explicitly: `swm -exec sh -c 'exec xterm'`.
The application inherits the environment, including `DISPLAY`, and starts
after the window manager is ready. If it cannot be executed, swm reports
an error and exits with a nonzero status. swm continues running after the
application exits. Use `swm --help` to show usage without starting X11.

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
required `-exec` option, application startup, argument forwarding, and
execution failures.

Operation with Xsixel itself and real browsers still needs verification.
Full ICCCM or EWMH support is outside the scope of this project. swm does
not implement application minimum-size or aspect-ratio hints, minimization,
application-requested fullscreen toggling, or per-monitor layouts.
UTF-8 titles are read from `_NET_WM_NAME` and drawn with an Xlib font set;
character coverage depends on the locale and available X fonts.

## Author and license

Author: Hiroki Sato <hrs@allbsd.org>

Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>.
Distributed under the BSD 2-Clause license. See [LICENSE](LICENSE).
