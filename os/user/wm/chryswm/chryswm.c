/*
 * chryswm.c - A minimalistic tiling window manager for ChrysalisOS
 * Based on tinywm and dwm logic.
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static Display *dpy;
static Window root;
static int screen;
static int sw, sh;

void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "chryswm: cannot open display\n");
        exit(1);
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    
    /* Grab keys: Alt + Shift + Enter (Terminal), Alt + Shift + C (Close), Alt + Q (Quit) */
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), Mod1Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_c), Mod1Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
}

void tile() {
    Window root_return, parent_return, *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren)) {
        int count = 0;
        for (unsigned int i = 0; i < nchildren; i++) {
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, children[i], &wa);
            if (!wa.override_redirect && wa.map_state == IsViewable) count++;
        }

        if (count > 0) {
            int w = sw / count;
            int x = 0;
            for (unsigned int i = 0; i < nchildren; i++) {
                XWindowAttributes wa;
                XGetWindowAttributes(dpy, children[i], &wa);
                if (!wa.override_redirect && wa.map_state == IsViewable) {
                    XMoveResizeWindow(dpy, children[i], x, 0, w - 2, sh - 2);
                    x += w;
                }
            }
        }
        if (children) XFree(children);
    }
}

void run() {
    XEvent ev;
    while (!XNextEvent(dpy, &ev)) {
        if (ev.type == MapRequest) {
            XMapWindow(dpy, ev.xmaprequest.window);
            tile();
        } else if (ev.type == DestroyNotify || ev.type == UnmapNotify) {
            tile();
        } else if (ev.type == KeyPress) {
            KeySym keysym = XLookupKeysym(&ev.xkey, 0);
            if (keysym == XK_Return && (ev.xkey.state & (Mod1Mask | ShiftMask))) {
                if (fork() == 0) {
                    execlp("xterm", "xterm", NULL);
                    exit(0);
                }
            } else if (keysym == XK_c && (ev.xkey.state & (Mod1Mask | ShiftMask))) {
                XKillClient(dpy, ev.xkey.subwindow);
            } else if (keysym == XK_q && (ev.xkey.state & Mod1Mask)) {
                break;
            }
        }
    }
}

int main() {
    setup();
    printf("chryswm: starting tiling engine...\n");
    tile();
    run();
    XCloseDisplay(dpy);
    return 0;
}
