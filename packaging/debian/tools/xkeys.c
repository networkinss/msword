/* xkeys — used for headless GUI testing of vintageword under Xvfb:
 * build with `gcc -O2 -o xkeys xkeys.c -lX11 -lXtst`, run inside
 * `xvfb-run -a`, screenshot with ImageMagick `import -window root`.
 *
 * Original description: xkeys — minimal XTEST keystroke injector for driving GUI tests.
 * Usage: xkeys ARG...
 *   @SLEEP=N   sleep N milliseconds
 *   @KEY=name  press+release keysym `name` (Return, Escape, F12, Tab...)
 *   @ALT=name  Alt + keysym
 *   @CTRL=name Ctrl + keysym
 *   anything else: typed as literal text (latin1)
 */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static Display *dpy;

static void press(KeySym mod, KeySym ks) {
    KeyCode mc = mod ? XKeysymToKeycode(dpy, mod) : 0;
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (!kc) { fprintf(stderr, "xkeys: no keycode for keysym %#lx\n", ks); return; }
    /* Does this keycode need shift? Check whether the keysym is on level 1. */
    int need_shift = (XkbKeycodeToKeysym(dpy, kc, 0, 0) != ks &&
                      XkbKeycodeToKeysym(dpy, kc, 0, 1) == ks);
    if (mc) XTestFakeKeyEvent(dpy, mc, True, 0);
    if (need_shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, 0);
    XTestFakeKeyEvent(dpy, kc, True, 0);
    XTestFakeKeyEvent(dpy, kc, False, 0);
    if (need_shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, 0);
    if (mc) XTestFakeKeyEvent(dpy, mc, False, 0);
    XFlush(dpy);
    usleep(30000);
}

int main(int argc, char **argv) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "xkeys: cannot open display\n"); return 1; }
    int ev, er, maj, min;
    if (!XTestQueryExtension(dpy, &ev, &er, &maj, &min)) {
        fprintf(stderr, "xkeys: no XTEST\n"); return 1;
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "@SLEEP=", 7)) { usleep(atoi(a + 7) * 1000); continue; }
        if (!strncmp(a, "@KEY=", 5))  { press(0, XStringToKeysym(a + 5)); continue; }
        if (!strncmp(a, "@ALT=", 5))  { press(XK_Alt_L, XStringToKeysym(a + 5)); continue; }
        if (!strncmp(a, "@CTRL=", 6)) { press(XK_Control_L, XStringToKeysym(a + 6)); continue; }
        if (!strncmp(a, "@SHIFT=", 7)) { press(XK_Shift_L, XStringToKeysym(a + 7)); continue; }
        for (const char *p = a; *p; p++) {
            KeySym ks = (*p == ' ') ? XK_space : (KeySym)(unsigned char)*p;
            press(0, ks);
        }
    }
    XCloseDisplay(dpy);
    return 0;
}
