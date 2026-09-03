# mintMenu segfaults on every application launch when any icon-less `.desktop` file is in the Recent list

**Component:** `linuxmint/mintmenu` — `plugins/easybuttons.py` (`ApplicationLauncher`)
**Severity:** crash (SIGSEGV), user-visible on every app launch
**Status:** root-caused, patch written and verified locally

## Summary

`ApplicationLauncher.__init__` calls `drag_source_set_icon_name(self.iconName)`
unconditionally. For a `.desktop` file with no `Icon=` key, `iconName` is the
empty string. GTK 3 segfaults when it frees the drag-source site data of a widget
whose drag icon name is `""`, so the button crashes the process at finalisation.

mintMenu finalises those buttons on **every** application launch: clicking any app
rebuilds the Recent Applications list, which destroys and drops the last reference
to every recent button. If even one icon-less app is in that list, launching
*anything* kills the applet, and mate-panel shows
*"mintMenu has quit unexpectedly"*.

## Environment

| | |
|---|---|
| Distro | Linux Mint 21.3 (Ubuntu 22.04 base), MATE |
| Kernel | 6.8.0-138-generic |
| mintmenu | 6.1.7 |
| libgtk-3-0 | 3.24.33-1ubuntu2.2 |
| libglib2.0-0 | 2.72.4-0ubuntu2.9 |
| python3-gi | 3.42.1-0ubuntu1 |
| mate-panel | 1.26.2-1 |

## Minimal reproducer (no mintMenu involved)

This is the underlying GTK 3 bug, in ten lines:

```python
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk

b = Gtk.Button()
targets = (Gtk.TargetEntry.new("text/plain", 0, 100),)
b.drag_source_set(Gdk.ModifierType.BUTTON1_MASK, targets, Gdk.DragAction.COPY)
b.drag_source_set_icon_name("")      # empty icon name
b.destroy()
del b                                # -> SIGSEGV in libgtk-3
```

Results:

| icon name passed | outcome |
|---|---|
| `"accessories-calculator"` | survives |
| `"no-such-icon-xyz"` (nonexistent) | survives |
| `""` (empty) | **SIGSEGV** |

So it is specifically the empty string, not a missing icon.

## Reproducer via mintMenu

```python
import sys, gc
sys.path.insert(0, '/usr/lib/linuxmint/mintMenu')
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
import plugins.easybuttons as EB

# any .desktop file with no Icon= key
b = EB.ApplicationLauncher('/usr/share/applications/vintageword.desktop', 16)
box = Gtk.Box(); box.pack_start(b, False, True, 0)
for c in box.get_children():
    c.destroy()                      # what doRecentApps() does
del b, c
gc.collect()                         # -> SIGSEGV
print("SURVIVED")
```

Adding an `Icon=` line to that `.desktop` file makes it survive; removing
`Comment=`/`MimeType=` changes nothing. A stripped 5-line `.desktop` with no
`Icon=` also crashes.

## End-to-end reproduction

1. Ensure some app with no `Icon=` in its `.desktop` file is in
   *Recent Applications* (`gsettings get com.linuxmint.mintmenu.plugins.recent recent-apps-list`).
2. Open the Mint menu and launch any application.
3. mate-panel shows *"mintMenu has quit unexpectedly"* with Delete / Don't Reload / Reload.
4. `journalctl -b | grep 'mintmenu.*segfault'` shows a SIGSEGV in `libgtk-3.so.0`
   at the same instruction each time.

On the affected machine this fired 12 times in one session.

## Diagnosis trail

Kernel log — always the same faulting instruction:

```
mintmenu[100669]: segfault at 4 ip ...12d2fa sp ... error 4
  in libgtk-3.so.0.2404.29[...+383000]
```

C backtrace from the core dump:

```
#0  0x00007321b17b12fa in ?? ()          from libgtk-3.so.0
#1  g_datalist_clear ()                  from libglib-2.0.so.0
#2  g_object_unref ()                    from libgobject-2.0.so.0
#3  ?? ()                                from gi/_gi.cpython-310-x86_64-linux-gnu.so
...
#25 ?? ()                                from libmate-panel-applet-4.so.1
```

i.e. the crash is a qdata destroy-notify running during GObject finalisation —
the drag-source site data.

Python stack, obtained by running the applet with `PYTHONFAULTHANDLER=1`:

```
Fatal Python error: Segmentation fault
Current thread (most recent call first):
  File ".../plugins/recentHelper.py", line 79 in buildRecentApps      # del recentApps[:]
  File ".../plugins/recentHelper.py", line 108 in doRecentApps
  File ".../plugins/recent.py", line 147 in DoRecent
  File ".../plugins/recent.py", line 125 in RebuildPlugin
  File ".../plugins/recent.py", line 121 in GetGSettingsEntries
  File ".../plugins/recent.py", line 93 in RegenPlugin
  File ".../plugins/recentHelper.py", line 38 in recentAppsSave
  File ".../plugins/recentHelper.py", line 123 in applicationButtonClicked
```

`del recentApps[:]` drops the last reference to the recent buttons, finalising
them — which is where the empty drag icon name kills the process.

### Dead end worth recording

The stack initially looks like re-entrancy: `applicationButtonClicked` is the
`clicked` handler, and it ends up destroying the very button still emitting the
signal. Deferring the whole body via `GLib.idle_add` **does not help** — the
crash simply moves into the idle callback. The emission context is irrelevant;
finalising that button crashes wherever it happens. Isolating the button
construction outside the panel entirely (the reproducer above) is what made the
real cause visible.

## Fix

Guard both `drag_source_set_icon_name` call sites in
`plugins/easybuttons.py` on a non-empty name:

```diff
--- a/usr/lib/linuxmint/mintMenu/plugins/easybuttons.py
+++ b/usr/lib/linuxmint/mintMenu/plugins/easybuttons.py
@@ -251,7 +251,12 @@
 
         targets = (Gtk.TargetEntry.new("text/plain", 0, 100), Gtk.TargetEntry.new("text/uri-list", 0, 101))
         self.drag_source_set(Gdk.ModifierType.BUTTON1_MASK, targets, Gdk.DragAction.COPY)
-        self.drag_source_set_icon_name(self.iconName)
+        # A .desktop file with no Icon= key gives iconName == "", and GTK 3
+        # segfaults when it frees the drag-source site data of a widget whose
+        # drag icon name is the empty string (g_datalist_clear at finalisation).
+        # Only set a drag icon when there actually is one.
+        if self.iconName:
+            self.drag_source_set_icon_name(self.iconName)
 
         self.connectSelf("focus-in-event", self.onFocusIn)
         self.connectSelf("focus-out-event", self.onFocusOut)
@@ -371,7 +376,8 @@
         icon = self.getIcon(Gtk.IconSize.DND)
         if icon:
             iconName, size = icon.get_icon_name()
-            self.drag_source_set_icon_name(iconName)
+            if iconName:
+                self.drag_source_set_icon_name(iconName)
 
     def startupFileChanged(self, *args):
         self.inStartup = os.path.exists(self.startupFilePath)
```

The second hunk (`iconChanged`) is the same bug on the icon-theme-change path:
`Gtk.Image.get_icon_name()` can also hand back an empty name.

## Verification

Every `.desktop` file on the test machine with no `Icon=` key, built as an
`ApplicationLauncher`, destroyed and finalised — before and after the patch:

| `.desktop` (no `Icon=`) | unpatched | patched |
|---|---|---|
| `apturl` | CRASH | ok |
| `marco` | CRASH | ok |
| `mint-window-manager` | CRASH | ok |
| `gcr-viewer` | CRASH | ok |
| `compiz` | CRASH | ok |
| `libreoffice-xsltfilter` | CRASH | ok |
| `remmina-gnome` | CRASH | ok |
| `vintageword` | CRASH | ok |
| `jetbrainsd` | CRASH | ok |
| `claude-code-url-handler` | CRASH | ok |
| `userapp-idea-J9XHM1` | CRASH | ok |
| `userapp-ultima-96ZUP1` | CRASH | ok |

Apps that do have an icon (`mate-terminal`, `xed`, `thunderbird`) are unaffected
and still survive with the patch applied.

## Upstream notes

Two separate reports are arguably warranted:

1. **linuxmint/mintmenu** — the patch above. Passing `""` to
   `drag_source_set_icon_name` is a mintMenu bug regardless of GTK's behaviour.
2. **GTK 3** — `gtk_drag_source_set_icon_name(widget, "")` should either be
   rejected or handled, not crash when the site data is freed. The pure-GTK
   reproducer in this document is self-contained and needs no mintMenu.

## Workarounds (no code change)

- Add an `Icon=` line to the offending `.desktop` file(s), or
- Clear Recent Applications:
  `gsettings set com.linuxmint.mintmenu.plugins.recent recent-apps-list "[]"`

Both are temporary: any icon-less app entering the Recent list re-triggers it.

## Note for whoever picks this up

`plugins/pointerMonitor.py` on the affected machine carries a separate local
patch (GDK calls from a background thread). It is unrelated to this crash and
should not be confused with it.
