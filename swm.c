/*
 * Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/wait.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrandr.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

#define	BAR	24

typedef struct Client Client;
struct Client {
	Window	win, bar, parent;
	int	dialog, x, y, width, height;
	Client	*next;
};
static Display *dpy;
static Window root;
static GC gc;
static XFontSet fontset;
static Client *clients, *active;
static Atom protocols, delete_window, take_focus, wm_state, type, dialog_type,
    net_name, utf8;
static int sw, sh, startup_error;
static unsigned int numlock;
static unsigned long bar_pixel;
static int randr_event, randr_available;
static int child_pipe[2] = { -1, -1 };
static pid_t application_pid;

static int
error(Display *d, XErrorEvent *e)
{

	(void)d;
	if (e->error_code == BadAccess)
		startup_error = 1;
	/* A client may disappear while its events are queued. */
	return (0);
}

static Client *
find(Window w)
{
	Client *c;

	for (c = clients; c; c = c->next)
		if (c->win == w || c->bar == w)
			return (c);
	return (NULL);
}

static int
supports(Client *c, Atom a)
{
	Atom *list;
	int count, found, i;

	list = NULL;
	count = found = 0;
	if (XGetWMProtocols(dpy, c->win, &list, &count)) {
		for (i = 0; i < count; i++)
			if (list[i] == a)
				found = 1;
		XFree(list);
	}
	return (found);
}

static void
message(Client *c, Atom a, Time time)
{
	XEvent e = { 0 };

	e.xclient.type = ClientMessage;
	e.xclient.window = c->win;
	e.xclient.message_type = protocols;
	e.xclient.format = 32;
	e.xclient.data.l[0] = (long)a;
	e.xclient.data.l[1] = (long)time;
	XSendEvent(dpy, c->win, False, NoEventMask, &e);
}

static void
draw(Client *c)
{
	char *name = NULL;
	unsigned char *text = NULL;
	Atom actual;
	int format, status, unicode;
	unsigned long n, rest;
	XRectangle clip;

	status = XGetWindowProperty(dpy, c->win, net_name, 0, 1024, False,
	    utf8, &actual, &format, &n, &rest, &text);
	unicode = status == Success && actual == utf8 && format == 8 && text;
	if (!unicode)
		XFetchName(dpy, c->win, &name);
	XClearWindow(dpy, c->bar);
	if (c->width > 36) {
		clip.x = clip.y = 0;
		clip.width = c->width - 30;
		clip.height = BAR;
		XSetClipRectangles(dpy, gc, 0, 0, &clip, 1, Unsorted);
		if (unicode && fontset)
			Xutf8DrawString(dpy, c->bar, fontset, gc, 6, 16,
			    (char *)text, (int)n);
		else if (name)
			XDrawString(dpy, c->bar, gc, 6, 16, name,
			    (int)strlen(name));
		XSetClipMask(dpy, gc, None);
	}
	if (text)
		XFree(text);
	if (name)
		XFree(name);
	XDrawLine(dpy, c->bar, gc, 0, BAR - 1, c->width - 1, BAR - 1);
	if (c->width >= BAR) {
		XDrawLine(dpy, c->bar, gc, c->width - BAR, 0,
		    c->width - BAR, BAR - 1);
		XDrawLine(dpy, c->bar, gc, c->width - 16, 8,
		    c->width - 8, 16);
		XDrawLine(dpy, c->bar, gc, c->width - 16, 16,
		    c->width - 8, 8);
	}
}

static void
layout(Client *c)
{
	XEvent e = { 0 };

	if (!c->dialog) {
		c->x = 0;
		c->y = BAR;
		c->width = sw;
		c->height = sh > BAR ? sh - BAR : 1;
	} else {
		if (c->width > sw)
			c->width = sw;
		if (c->height > sh - BAR)
			c->height = sh > BAR ? sh - BAR : 1;
		c->x = (sw - c->width) / 2;
		c->y = BAR + (sh - BAR - c->height) / 2;
	}
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->width, c->height);
	XMoveResizeWindow(dpy, c->bar, c->x, c->y - BAR, c->width, BAR);
	/*
	 * Notify even when a rejected ConfigureRequest leaves geometry
	 * unchanged.
	 */
	e.xconfigure.type = ConfigureNotify;
	e.xconfigure.display = dpy;
	e.xconfigure.event = e.xconfigure.window = c->win;
	e.xconfigure.x = c->x;
	e.xconfigure.y = c->y;
	e.xconfigure.width = c->width;
	e.xconfigure.height = c->height;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, &e);
}

static void
focus(Client *c, Time time)
{
	Client *child, *p;
	XWMHints *h;
	int depth;

	/*
	 * Keep transient descendants above their parent, including nested
	 * dialogs.
	 */
	for (depth = 0; c && depth < 32; depth++) {
		child = NULL;
		for (p = clients; p; p = p->next)
			if (p != c && p->parent == c->win) {
				child = p;
				break;
			}
		if (!child)
			break;
		c = child;
	}
	active = c;
	if (!c) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, time);
		return;
	}
	XRaiseWindow(dpy, c->win);
	XRaiseWindow(dpy, c->bar);
	h = XGetWMHints(dpy, c->win);
	if (!h || !(h->flags & InputHint) || h->input)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, time);
	if (h)
		XFree(h);
	if (supports(c, take_focus))
		message(c, take_focus, time);
}

static int
is_dialog(Window w)
{
	Atom actual;
	int format;
	unsigned long i, n, rest;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(dpy, w, type, 0, 32, False, XA_ATOM, &actual,
		&format, &n, &rest, &data) == Success &&
	    data) {
		if (actual == XA_ATOM && format == 32)
			for (i = 0; i < n; i++)
				if (((Atom *)data)[i] == dialog_type)
					result = 1;
		XFree(data);
	}
	return (result);
}

static void
manage(Window w)
{
	Client *c;
	XWindowAttributes a;
	XSetWindowAttributes attrs = { 0 };
	long state[] = { NormalState, None };

	if (find(w) || !XGetWindowAttributes(dpy, w, &a) || a.override_redirect)
		return;
	c = calloc(1, sizeof(*c));
	if (!c) {
		fputs("swm: out of memory\n", stderr);
		exit(1);
	}
	c->win = w;
	XGetTransientForHint(dpy, w, &c->parent);
	c->dialog = c->parent != None || is_dialog(w);
	c->width = a.width;
	c->height = a.height;
	attrs.override_redirect = True;
	attrs.background_pixel = bar_pixel;
	attrs.event_mask = ExposureMask | ButtonPressMask;
	c->bar = XCreateWindow(dpy, root, 0, 0, 1, BAR, 0, CopyFromParent,
	    InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
	c->next = clients;
	clients = c;
	XSelectInput(dpy, w, PropertyChangeMask | FocusChangeMask);
	XSetWindowBorderWidth(dpy, w, 0);
	XChangeProperty(dpy, w, wm_state, wm_state, 32, PropModeReplace,
	    (unsigned char *)state, 2);
	layout(c);
	XMapWindow(dpy, w);
	XMapWindow(dpy, c->bar);
	focus(c, CurrentTime);
	draw(c);
}

static void
remove_client(Client *c)
{
	Window parent = c->parent;
	int was_active = active == c;
	Client **p = &clients;

	while (*p && *p != c)
		p = &(*p)->next;
	if (*p)
		*p = c->next;
	XDestroyWindow(dpy, c->bar);
	XDeleteProperty(dpy, c->win, wm_state);
	free(c);
	if (was_active)
		focus(find(parent) ? find(parent) : clients, CurrentTime);
}

static void
close_client(Client *c, Time time)
{

	if (!c)
		return;
	if (supports(c, delete_window))
		message(c, delete_window, time);
	else
		XKillClient(dpy, c->win);
}

static void
grab_keys(void)
{
	unsigned int locks[4];
	int i, j;
	XModifierKeymap *mods = XGetModifierMapping(dpy);
	KeyCode num = XKeysymToKeycode(dpy, XK_Num_Lock);

	numlock = 0;
	for (i = 0; i < 8; i++)
		for (j = 0; j < mods->max_keypermod; j++)
			if (num != 0 && mods->modifiermap[
			    i * mods->max_keypermod + j] == num)
				numlock |= 1U << i;
	XFreeModifiermap(mods);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	locks[0] = 0;
	locks[1] = LockMask;
	locks[2] = numlock;
	locks[3] = numlock | LockMask;
	for (i = 0; i < 4; i++)
		XGrabKey(dpy, XKeysymToKeycode(dpy, XK_w), Mod1Mask | locks[i],
		    root, False, GrabModeAsync, GrabModeAsync);
}

static void
child_changed(int signo)
{
	int saved_errno;
	char byte = 0;

	(void)signo;
	saved_errno = errno;
	(void)write(child_pipe[1], &byte, 1);
	errno = saved_errno;
}

static int
watch_child(void)
{
	struct sigaction sa;
	int i, flags;

	if (pipe(child_pipe) == -1)
		return (-1);
	for (i = 0; i < 2; i++) {
		flags = fcntl(child_pipe[i], F_GETFL);
		if (flags == -1 || fcntl(child_pipe[i], F_SETFL,
		    flags | O_NONBLOCK) == -1 ||
		    fcntl(child_pipe[i], F_SETFD, FD_CLOEXEC) == -1)
			return (-1);
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = child_changed;
	sa.sa_flags = SA_NOCLDSTOP;
	sigemptyset(&sa.sa_mask);
	return (sigaction(SIGCHLD, &sa, NULL));
}

static void
resize_screen(int width, int height)
{
	Client *c;

	if (width < 1 || height < 1 || (width == sw && height == sh))
		return;
	sw = width;
	sh = height;
	for (c = clients; c; c = c->next) {
		layout(c);
		draw(c);
	}
	XFlush(dpy);
}

static int
start_application(char **argv)
{
	posix_spawn_file_actions_t actions;
	int error;

	error = posix_spawn_file_actions_init(&actions);
	if (error == 0) {
		error = posix_spawn_file_actions_addclose(&actions,
		    ConnectionNumber(dpy));
		if (error == 0)
			error = posix_spawnp(&application_pid, argv[0], &actions,
			    NULL, argv, environ);
		posix_spawn_file_actions_destroy(&actions);
	}
	if (error != 0) {
		fprintf(stderr, "swm: cannot execute %s: %s\n", argv[0],
		    strerror(error));
		return (-1);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	Client *c;
	XEvent e;
	XColor color, exact;
	struct pollfd fds[2];
	pid_t child;
	XFontStruct *font;
	XWindowAttributes a;
	Window r, parent, *children = NULL;
	char **missing, *fallback;
	unsigned int count, i;
	int nmissing, randr_error, status, exit_status;
	char bytes[64];

	if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
	    strcmp(argv[1], "-h") == 0)) {
		puts("Usage: swm command [args ...]\n"
		    "Alt-w: close focused window");
		return (0);
	}
	if (argc < 2 || argv[1][0] == '\0') {
		fputs("Usage: swm command [args ...]\n", stderr);
		return (1);
	}
	setlocale(LC_CTYPE, "");
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fputs("swm: cannot open DISPLAY\n", stderr);
		return (1);
	}
	root = DefaultRootWindow(dpy);
	XSetErrorHandler(error);
	XSelectInput(dpy, root, SubstructureRedirectMask |
	    SubstructureNotifyMask | StructureNotifyMask);
	XSync(dpy, False);
	if (startup_error) {
		fputs("swm: another window manager is running\n", stderr);
		return (1);
	}
	sw = DisplayWidth(dpy, DefaultScreen(dpy));
	sh = DisplayHeight(dpy, DefaultScreen(dpy));
	randr_available = XRRQueryExtension(dpy, &randr_event, &randr_error);
	if (randr_available)
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	bar_pixel = BlackPixel(dpy, DefaultScreen(dpy));
	if (XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
	    "#303030", &color, &exact))
		bar_pixel = color.pixel;
	protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wm_state = XInternAtom(dpy, "WM_STATE", False);
	type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	dialog_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	fontset = XCreateFontSet(dpy, "fixed", &missing, &nmissing, &fallback);
	if (missing)
		XFreeStringList(missing);
	gc = XCreateGC(dpy, root, 0, NULL);
	XSetForeground(dpy, gc, WhitePixel(dpy, DefaultScreen(dpy)));
	font = XLoadQueryFont(dpy, "fixed");
	if (font)
		XSetFont(dpy, gc, font->fid);
	grab_keys();
	if (XQueryTree(dpy, root, &r, &parent, &children, &count)) {
		for (i = 0; i < count; i++) {
			if (XGetWindowAttributes(dpy, children[i], &a) &&
			    a.map_state == IsViewable)
				manage(children[i]);
		}
		if (children)
			XFree(children);
	}
	if (watch_child() == -1) {
		perror("swm: cannot monitor application");
		XCloseDisplay(dpy);
		return (1);
	}
	/* Finish setting up the window manager before starting the client. */
	XSync(dpy, False);
	if (start_application(&argv[1]) == -1) {
		XCloseDisplay(dpy);
		return (1);
	}
	fds[0] = (struct pollfd){ .fd = ConnectionNumber(dpy),
	    .events = POLLIN };
	fds[1] = (struct pollfd){ .fd = child_pipe[0], .events = POLLIN };
	exit_status = 1;
	for (;;) {
		child = waitpid(application_pid, &status, WNOHANG);
		if (child == application_pid) {
			exit_status = WIFEXITED(status) ? WEXITSTATUS(status) :
			    128 + WTERMSIG(status);
			break;
		}
		if (child == -1 && errno != EINTR) {
			perror("swm: waitpid");
			break;
		}
		if (!XPending(dpy)) {
			if (poll(fds, 2, -1) == -1 && errno != EINTR) {
				perror("swm: poll");
				break;
			}
			while (read(child_pipe[0], bytes, sizeof(bytes)) > 0)
				;
			continue;
		}
		XNextEvent(dpy, &e);
		if (randr_available && e.type == randr_event +
		    RRScreenChangeNotify) {
			XRRUpdateConfiguration(&e);
			resize_screen(DisplayWidth(dpy, DefaultScreen(dpy)),
			    DisplayHeight(dpy, DefaultScreen(dpy)));
			continue;
		}
		c = find(e.xany.window);
		switch (e.type) {
		case MapRequest:
			manage(e.xmaprequest.window);
			break;
		case DestroyNotify:
			c = find(e.xdestroywindow.window);
			if (c && e.xdestroywindow.window == c->win)
				remove_client(c);
			break;
		case UnmapNotify:
			c = find(e.xunmap.window);
			if (c && e.xunmap.window == c->win)
				remove_client(c);
			break;
		case ConfigureRequest: {
			XConfigureRequestEvent *q = &e.xconfigurerequest;

			c = find(q->window);
			if (c) {
				if (c->dialog) {
					if (q->value_mask & CWWidth)
						c->width = q->width;
					if (q->value_mask & CWHeight)
						c->height = q->height;
				}
				layout(c);
			} else {
				XWindowChanges wc = { q->x, q->y, q->width,
					q->height, q->border_width, q->above,
					q->detail };

				XConfigureWindow(dpy, q->window, q->value_mask,
				    &wc);
			}
			break;
		}
		case ConfigureNotify:
			if (e.xconfigure.window == root) {
				if (!randr_available)
					resize_screen(e.xconfigure.width,
					    e.xconfigure.height);
			} else {
				c = find(e.xconfigure.window);
				if (c && e.xconfigure.window == c->win) {
					XWindowChanges wc = { .sibling = c->win,
					    .stack_mode = Above };

					XConfigureWindow(dpy, c->bar,
					    CWSibling | CWStackMode, &wc);
				}
			}
			break;
		case Expose:
			if (c && !e.xexpose.count)
				draw(c);
			break;
		case PropertyNotify:
			if (c &&
			    (e.xproperty.atom == XA_WM_NAME ||
				e.xproperty.atom == net_name))
				draw(c);
			break;
		case ButtonPress:
			if (c) {
				focus(c, e.xbutton.time);
				if (active == c &&
				    e.xbutton.button == Button1 &&
				    e.xbutton.x >= c->width - BAR)
					close_client(c, e.xbutton.time);
			}
			break;
		case KeyPress:
			close_client(active, e.xkey.time);
			break;
		case FocusIn:
			if (c && c != active && e.xfocus.mode == NotifyNormal &&
			    e.xfocus.detail != NotifyInferior)
				focus(c, CurrentTime);
			break;
		case MappingNotify:
			XRefreshKeyboardMapping(&e.xmapping);
			grab_keys();
			break;
		}
	}
	XCloseDisplay(dpy);
	close(child_pipe[0]);
	close(child_pipe[1]);
	return (exit_status);
}
