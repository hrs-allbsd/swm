/*
 * Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(expr)                                                     \
	do {                                                            \
		if (!(expr)) {                                          \
			fprintf(stderr, "FAIL line %d: %s\n", __LINE__, \
			    #expr);                                     \
			exit(1);                                        \
		}                                                       \
	} while (0)

static Display *d;
static Window root;
static Atom del;

static void
pause_events(void)
{
	struct timespec t = { 0, 100000000 };

	nanosleep(&t, NULL);
	XSync(d, False);
}


static Window
create(const char *name, Window parent, int typed)
{
	Atom a;
	Window w;

	w = XCreateSimpleWindow(d, root, 10, 10, 300, 120, 0, 0, 0);
	XStoreName(d, w, name);
	XSetWMProtocols(d, w, &del, 1);
	if (parent)
		XSetTransientForHint(d, w, parent);
	if (typed) {
		a = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
		XChangeProperty(d, w,
		    XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)&a, 1);
	}
	XMapWindow(d, w);
	XFlush(d);
	return (w);
}

static void
geometry(Window w, int x, int y, int width, int height)
{
	XWindowAttributes a;
	int i;

	for (i = 0; i < 30; i++) {
		pause_events();
		XGetWindowAttributes(d, w, &a);
		if (a.x == x && a.y == y && a.width == width &&
		    a.height == height && a.map_state == IsViewable)
			return;
	}
	fprintf(stderr, "geometry: %d,%d %dx%d\n", a.x, a.y, a.width, a.height);
	CHECK(0);
}

static void
focused(Window expected)
{
	Window w;
	int i, revert;

	for (i = 0; i < 30; i++) {
		pause_events();
		XGetInputFocus(d, &w, &revert);
		if (w == expected)
			return;
	}
	CHECK(w == expected);
}

static void
close_key(void)
{

	CHECK(system("xdotool key alt+w") == 0);
}

static void
deleted(Window expected)
{
	XEvent e;
	int i;

	for (i = 0; i < 30; i++) {
		pause_events();
		while (XPending(d)) {
			XNextEvent(d, &e);
			if (e.type == ClientMessage &&
			    (Atom)e.xclient.data.l[0] == del) {
				CHECK(e.xclient.window == expected);
				return;
			}
		}
	}
	CHECK(0);
}

int
main(void)
{
	XEvent click = { 0 };
	XWindowAttributes a;
	Window main, dialog, nested, typed, rr, pp, *children = NULL;
	Window bar = None;
	unsigned int i, n;

	d = XOpenDisplay(NULL);
	CHECK(d);
	root = DefaultRootWindow(d);
	del = XInternAtom(d, "WM_DELETE_WINDOW", False);
	main = create("Main", None, 0);
	geometry(main, 0, 24, 800, 576);
	focused(main);
	XMoveResizeWindow(d, main, 50, 50, 100, 100);
	geometry(main, 0, 24, 800, 576);
	dialog = create("Confirm", main, 0);
	geometry(dialog, 250, 252, 300, 120);
	focused(dialog);
	nested = create("Nested", dialog, 0);
	geometry(nested, 250, 252, 300, 120);
	focused(nested);
	XSetInputFocus(d, main, RevertToPointerRoot, CurrentTime);
	XFlush(d);
	focused(nested);
	close_key();
	deleted(nested);
	XDestroyWindow(d, nested);
	XFlush(d);
	focused(dialog);
	close_key();
	deleted(dialog);
	XUnmapWindow(d, dialog);
	XFlush(d);
	focused(main);
	XMapWindow(d, dialog);
	XFlush(d);
	focused(dialog);
	XDestroyWindow(d, dialog);
	XFlush(d);
	focused(main);
	typed = create("Typed dialog", None, 1);
	geometry(typed, 250, 252, 300, 120);
	focused(typed);
	/*
	 * Find its sibling title bar by geometry, then exercise the close
	 * button.
	 */
	CHECK(XQueryTree(d, root, &rr, &pp, &children, &n));
	for (i = 0; i < n; i++) {
		XGetWindowAttributes(d, children[i], &a);
		if (a.override_redirect && a.x == 250 && a.y == 228 &&
		    a.width == 300)
			bar = children[i];
	}
	XFree(children);
	CHECK(bar);
	click.xbutton.type = ButtonPress;
	click.xbutton.display = d;
	click.xbutton.window = bar;
	click.xbutton.button = Button1;
	click.xbutton.x = 290;
	XSendEvent(d, bar, False, ButtonPressMask, &click);
	XFlush(d);
	deleted(typed);
	XDestroyWindow(d, typed);
	XFlush(d);
	focused(main);
	CHECK(system("xdotool key Caps_Lock") == 0);
	close_key();
	deleted(main);
	CHECK(system("xdotool key Caps_Lock Num_Lock") == 0);
	close_key();
	deleted(main);
	CHECK(system("xdotool key Caps_Lock") == 0);
	close_key();
	deleted(main);
	CHECK(system("xdotool key Caps_Lock Num_Lock") == 0);
	close_key();
	deleted(main);
	XDestroyWindow(d, main);
	XFlush(d);
	focused(root);
	XCloseDisplay(d);
	puts(
	    "PASS: maximize, resize rejection, transient/nested/type dialogs, focus restoration, remap, Alt-w, title close");
	return (0);
}
