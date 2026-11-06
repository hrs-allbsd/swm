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
#include <string.h>
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
titlebar(Window w)
{
	XWindowAttributes a, b;
	XImage *image;
	Window rr, parent, *children;
	unsigned int count, i;
	int found;

	pause_events();
	CHECK(XGetWindowAttributes(d, w, &a));
	CHECK(XQueryTree(d, root, &rr, &parent, &children, &count));
	found = 0;
	for (i = 0; i < count; i++) {
		CHECK(XGetWindowAttributes(d, children[i], &b));
		if (b.override_redirect && b.map_state == IsViewable &&
		    b.x == a.x && b.y == a.y - 24 && b.width == a.width &&
		    b.height == 24)
			found = 1;
	}
	XFree(children);
	CHECK(found);
	/* Read the composed root image to catch obscured title bars. */
	image = XGetImage(d, root, a.x, a.y - 24, a.width, 24,
	    AllPlanes, ZPixmap);
	CHECK(image != NULL);
	CHECK(XGetPixel(image, 1, 1) != BlackPixel(d, DefaultScreen(d)));
	CHECK(XGetPixel(image, a.width - 16, 8) ==
	    WhitePixel(d, DefaultScreen(d)));
	CHECK(XGetPixel(image, 1, 23) == WhitePixel(d, DefaultScreen(d)));
	XDestroyImage(image);
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
main(int argc, char **argv)
{
	XEvent click = { 0 };
	XWindowAttributes a;
	Window main, dialog, nested, typed, rr, pp, *children = NULL;
	Window bar = None;
	unsigned int i, n;

	d = XOpenDisplay(NULL);
	CHECK(d);
	root = DefaultRootWindow(d);
	if (argc == 5 && strcmp(argv[1], "--check") == 0) {
		main = strtoul(argv[2], NULL, 0);
		geometry(main, 0, 24, atoi(argv[3]), atoi(argv[4]) - 24);
		titlebar(main);
		XCloseDisplay(d);
		return (0);
	}
	CHECK(argc == 1);
	del = XInternAtom(d, "WM_DELETE_WINDOW", False);
	main = create("Main", None, 0);
	geometry(main, 0, 24, 800, 576);
	focused(main);
	titlebar(main);
	XMoveResizeWindow(d, main, 50, 50, 100, 100);
	geometry(main, 0, 24, 800, 576);
	dialog = create("Confirm", main, 0);
	geometry(dialog, 250, 252, 300, 120);
	focused(dialog);
	titlebar(dialog);
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
	if (getenv("SWM_TEST_RESIZE") != NULL) {
		CHECK(system("xrandr --output VNC-0 --mode 640x480") == 0);
		geometry(main, 0, 24, 640, 456);
		titlebar(main);
		dialog = create("Resize dialog", main, 0);
		geometry(dialog, 170, 192, 300, 120);
		CHECK(system("xrandr --output VNC-0 --mode 1024x768") == 0);
		geometry(main, 0, 24, 1024, 744);
		geometry(dialog, 362, 336, 300, 120);
		titlebar(main);
		titlebar(dialog);
		CHECK(system("xrandr --output VNC-0 --mode 800x600") == 0);
		geometry(main, 0, 24, 800, 576);
		geometry(dialog, 250, 252, 300, 120);
		XDestroyWindow(d, dialog);
		XFlush(d);
		focused(main);
		titlebar(main);
		puts("PASS: real RandR shrink/grow/restore and dialog layout");
	}
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
