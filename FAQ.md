# FAQ

## VST3 plugins

### Why does a plugin show only a few parameters, or "Param 1", "Param 2"…?

RigRoom shows the parameters the plugin itself exposes. Many amp plugins
expose a few fixed controls plus generic slots (AmpliTube's "Param 1–16", Amp
Locker's "Automation 1–18") instead of their amp knobs. A slot does nothing
until you assign a control to it in the plugin itself; plugins that rename the
slot show the new name in RigRoom.

### Does a preset remember the amp I picked inside the plugin?

Yes. Presets keep a VST3 plugin's full settings, including what isn't a
parameter, such as the amp chosen inside AmpliTube.

### I picked another amp inside the plugin, but the preset isn't marked as changed

Known limitation. RigRoom notices changes a plugin reports and changes to its
parameters, but many plugins don't report picking another amp or cabinet.
Press Save to keep such a change; the preset stores the plugin's full settings.

### Can each scene use a different amp?

Scenes switch only parameters and blocks on/off, not a plugin's own settings.
For a different amp per scene, add two instances of the plugin with different
amps and switch them on and off in the scenes.

### Are VST2 plugins supported?

No.

## Windows plugins (Wine)

Windows VST3 plugins run through Wine with
[yabridge](https://github.com/robbert-vdh/yabridge) or its fork vstbridge.

### Why does a Windows plugin take so long to load?

Wine starts first (about 5 s), then the plugin initialises itself; AmpliTube 5
takes about 13 s in total. Switching to a preset loads its plugins again, so a
preset with such a plugin takes a few seconds to open.

To skip the Wine start for every plugin after the first, put them in one group
in `~/.vst3/yabridge/yabridge.toml`:

```toml
["*"]
group = "all"
```

With another Windows plugin already loaded, AmpliTube 5 then loads in about
5 s instead of 13 s. If one plugin in the group crashes, it takes the others
with it.

### The plugin window doesn't react to the mouse

In `winecfg`, Graphics tab, untick "Allow the window manager to decorate the
windows" and "Allow the window manager to control the windows".

### A plugin window opens at the wrong size

Some Windows plugins (for example AmpliTube 5 through vstbridge on some
systems) draw at the wrong size until the window is resized. Drag a corner of
the window a little.

### RigRoom closed when I loaded a Windows plugin

Some plugins crash under Wine (BIAS FX 2, for example), and RigRoom may close
with them. Save your preset before trying a new Windows plugin.

## AppImage

### The AppImage doesn't start and mentions FUSE

The AppImage mounts itself with FUSE, which desktop distributions include.
Without it (for example in a container), start it with
`./RigRoom-<version>-x86_64.AppImage --appimage-extract-and-run`.
