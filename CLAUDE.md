# RackFork

A personal fork of VCV Rack 2 Free, modified for accessibility and interaction changes.
Chris works mouse-first with screen magnification, so contrast, pointer economy and
zoom behaviour are the point of this project, not incidental polish.

Public fork: `chrisgr99/RackFork` on GitHub. Upstream `VCVRack/Rack` is a fetch-only
remote; its push URL is deliberately set to a dead string so no push can reach VCV.

- Repo: `/Users/chrisgr/ProgrammingProjects/RackFork` — the path must never contain
  spaces, Rack's Makefile build breaks on them.
- Rack user folder for this build: `~/Documents/RackFork`, kept separate so this
  fork's settings never collide with a stock Rack install.
- Pinned to tag **v2.6.6**, working branch `mods`.


## Build and run

```
./build.sh              # build, version pinned (use this, never bare make)
./build.sh clean        # arguments are forwarded to make
./run-library.sh        # NORMAL MODE — this is the one to use
./run-dev.sh            # dev mode, debugging only
./make-app.sh           # regenerate ~/Applications/RackFork.app
```

Normal use is the **RackFork.app** bundle in ~/Applications, which is what makes the
app findable in the Dock and via Cmd-Tab. See "App bundle wrapper" below.

`run-library.sh` runs Rack with dev mode off and the user folder pointed at
`~/Documents/RackFork`. Dev mode off is what makes the Library menu work, and the
Library is how third-party plugins get installed.

Never use `make run`. It passes the dev-mode flag and leaves you without a Library.

Both scripts change to the repo directory first, and that is required rather than
cosmetic: the `Rack` executable links `libRack.dylib` by bare relative name, so it
only runs with the repo root as the working directory.

`./Rack -v` prints the version and exits without opening a window. It is the cheapest
real smoke test there is — it proves the launcher runs, finds and loads the dylib, and
initialises far enough to print. Use it after any build.

**Quit with Cmd-Q, never by killing the process.** Rack treats an unfinished log as a
crash and greets the next launch with a modal dialog offering to clear your patch. If
you do have to kill it, prefer SIGKILL over SIGTERM: in normal mode Rack installs a
handler for the terminate signal that treats it as a fatal crash and writes a crash
report for no reason.


## App bundle wrapper

The build produces a bare command-line executable with no Mac app identity, which is
awkward to reach in the Dock or by Cmd-Tab. `make-app.sh` generates a thin wrapper at
~/Applications/RackFork.app that fixes that.

It runs the binary in place rather than copying it, so a rebuild takes effect with no
reinstall step, and it does not involve `make dist` or copy anything under `res/`.
Regenerate it if the repo moves.

Two things worth knowing about how it behaves:

- The wrapper `exec`s through to Rack, which preserves the process ID, so macOS keeps
  the Dock tile associated with the bundle.
- Rack still logs an empty "Bundle path", because by then the running image is the
  bare `Rack` executable outside the bundle. That is harmless. The Dock identity comes
  from LaunchServices at launch time, and the asset paths are handled by the
  `RACK_SYSTEM_DIR` environment variable the wrapper exports — which `src/asset.cpp`
  checks *before* its macOS bundle branch. Without that variable, a bundled launch
  would look for `res/` inside the bundle and fail.


## Rule 1: the plugin ABI is sacred

Third-party modules are precompiled binaries built against stock Rack. They subclass
and allocate Rack's widget classes. Break the ABI and they crash or corrupt memory,
with no useful error message.

Never change the layout of any class plugins subclass or allocate — `Widget`,
`ModuleWidget`, `PortWidget`, `ParamWidget`, `Knob`, `SvgKnob`, `SvgPort`, and
anything else under `include/app/`, `include/widget/`, `include/ui/`. Specifically:

- **No new member variables.** Not even at the end, in a class a plugin allocates.
- **No new, removed, or reordered virtual methods.** This is the worse trap, and it
  bites even when you add zero data. Adding a virtual function shifts the vtable, and
  plugin subclasses have their own slots after the base ones. Reordering is just as
  fatal as adding.
- **Changing method bodies is fine, but only for non-inline functions defined in a
  `.cpp`.** Anything inline in a header, and anything templated, was already compiled
  into every plugin binary. Change that body and your code gets the new behaviour
  while every third-party module keeps the old one. No crash, no warning, just two
  different behaviours in one process — which is far harder to debug than a crash.

Keep new state in `RackWidget`, `CableWidget`, `Scene`, or `settings` instead. Those
are not subclassed or allocated by plugins. Even there, **append new members at the
end and never reorder existing ones**, because plugins do reach through `APP` to read
fields on those objects and existing offsets must stay put.


## Rule 2: never change APP_VERSION

Rack 2.x only loads 2.x plugins, and library sync sends the version to the VCV API.
Change the window title or app name if you want a visible mark of your own. Not the
version.

**There is a trap here that has nothing to do with editing a version string.** The
Makefile derives the version from git:

```
RACK_VERSION ?= $(patsubst v%,%,$(shell git describe --tags --match "v2.*"))
```

HEAD currently sits exactly on v2.6.6, so plain `make` computes 2.6.6. But as soon as
there are commits on `mods`, that command returns something like `v2.6.6-3-g1a2b3c4d`,
and the whole string gets compiled in as the app version. You would have changed
APP_VERSION by committing, without touching a version anywhere.

`build.sh` exists to prevent exactly this. It passes the version on the make command
line, which overrides both the Makefile's soft assignment and any environment value.
**Always build through `build.sh`.** After building, confirm with `./Rack -v` that it
still says 2.6.6.


## Rule 3: nothing under res/ may ever change

Rack's panel graphics are licensed CC BY-NC-ND. Verbatim redistribution is permitted;
derivative works are not. This fork is public, and a public push cannot be undone —
GitHub forks share object storage with the parent network, so a pushed file stays
reachable even after a history rewrite. Prevention is the only real control.

So: **no recolouring on disk, no edited copies, no pre-baked variants, ever.** All
recolouring happens at runtime, in memory, after nanosvg has parsed the file.

Two guards are installed and tested, in `.githooks` with `core.hooksPath` pointing at
them:

- `pre-commit` refuses any staged change under `res`.
- `pre-push` compares the entire `res` tree of whatever you are about to push against
  the baseline tag. This is the one that actually protects you, because it catches
  changes arriving by merge or by a commit made with the no-verify flag, however many
  commits back they happened.

The baseline is read from git config key `rackfork.resBaseline`, defaulting to v2.6.6.
After a deliberate upstream merge that legitimately updates `res`, move it forward:

```
git config rackfork.resBaseline v2.6.7
```

Standing audit, which should always come back empty:

```
git diff --stat v2.6.6 -- res
```

**Your own original artwork goes in `resmod/`, never in `res/`.** A panel you draw
from scratch is your own work rather than a derivative, even if it carries the same
controls with a different layout, different colours and different knobs — copyright
protects expressive choices, not the functional fact that a filter has a cutoff knob.
What makes it a derivative is adapting their file. So keep the two activities
physically separate: never open a file under `res` in an editor. Keeping `resmod/`
separate also means the rule above has zero legitimate exceptions, and a rule with no
exceptions is the only kind that survives a year of tinkering.

If you want others to be able to use your designs, put a licence file in `resmod/`
saying so. With nothing stated they default to all rights reserved, which is the only
option that creates confusion.

Never delete or alter the `LICENSE-*` files. Verbatim redistribution of the panels is
only permitted with the licence and attribution intact.


## Planned modifications

1. **Global panel and graphic recolour for low-vision contrast** — hook the SVG load
   path in `src/window/Svg.cpp`, walk the parsed `NSVGimage` shape list, and remap
   fill and stroke colours including gradient stops. One chokepoint covers every
   plugin's panels plus the Component Library knobs and ports.

   Known limits, so they are not mistaken for bugs later: this misses raster panels
   and textures, which graphically elaborate plugins often ship instead of SVG; it
   misses anything drawn in code with direct nanovg calls in `draw()`, meaning lights,
   scope traces, meters and custom text; and both `Svg` objects and rendered widget
   framebuffers are cached, so a live toggle would need cache invalidation plus
   forcing every framebuffer dirty. Recolour at load and require a restart first;
   fight the caching later, if at all.

2. **Modifier-held modal pan and zoom** — `src/app/RackScrollWidget.cpp`.

3. **Click-to-connect cables, no held drag** — `src/app/PortWidget.cpp`, plus the
   incomplete-cable state in `RackWidget`, plus the dragged-widget handling in
   `src/widget/event.cpp`.

4. **Animated directional signal flow on cables** — `src/app/CableWidget.cpp`, in
   `draw()`.

5. **Panel override directory** — same chokepoint as modification 1. Before loading a
   file under `res`, check `resmod/` for an override keyed by the original's path, and
   fall back to the shipped file. Then apply the recolour to whichever was loaded.
   This is what lets Chris redesign any panel, including third-party ones, without
   touching `res` and without the derivative-work question ever arising.


## Verified facts about this build

Established by inspection and testing, not assumed:

- macOS 26.4 on arm64, Xcode 26.4, Apple clang 21. All dependencies built cleanly
  with no patches needed under `dep`.
- Entry point is `adapters/standalone.cpp`, not `src/main.cpp`. Rack builds a thin
  launcher against `libRack.dylib`.
- **This build is arm64-only, while stock Rack is universal.** Older or abandoned
  library plugins that ship only an x64 Mac binary will silently fail to load here
  even though they work in a stock install. Expect it rather than debugging it.
- Dev mode, from `src/asset.cpp`, `src/library.cpp` and `src/app/MenuBar.cpp`: logging
  goes to stderr rather than log.txt, the fatal signal handlers are not installed so
  crashes drop straight out, the user directory becomes the repo, library sync is
  short-circuited, and the Library menu shows an inert label instead of login and
  install items.
- Rack's own `.gitignore` already covers the build outputs and every user-folder path
  dev mode writes into the repo root, so dev mode does not dirty the tree. `log.txt`
  and `cache/` were the two gaps and are listed in `.git/info/exclude`.
- **The build ships Core only, and Fundamental cannot be installed from the Library.**
  The library presents those modules as part of the "VCV Rack 2 Free" product rather
  than as a subscribable plugin, so they arrive with the official download and a source
  build cannot sync them. Get it with the Makefile's own download target instead, then
  drop the package in the user plugins folder — Rack unpacks any package it finds there
  at startup:

  ```
  ./build.sh Fundamental-2.6.4-mac-arm64.vcvplugin
  cp Fundamental-*.vcvplugin ~/Documents/RackFork/plugins-mac-arm64/
  ```

  Third-party plugins do work through the Library normally. The flow is: log in from
  Rack's Library menu, subscribe to the plugin on library.vcvrack.com, then sync from
  the Library menu, then restart.

- In non-dev mode on macOS, Rack resolves its system directory through the Core
  Foundation main bundle. For this bare executable that gives the repo root, which is
  correct — verified from a real launch, no `-s` flag needed.


## Known-good plugin set

These loaded cleanly and made sound on 2026-08-10, with zero warnings or errors in the
log. When a recolour or interaction change later makes something look or behave wrong,
this is the reference set — the first question is always whether that plugin ever
worked here.

| Plugin | Version |
| --- | --- |
| Core | 2.6.6 |
| Fundamental | 2.6.4 |
| FrozenWasteland | 2.1.2 |
| FrequencyDomain | 2.0.3 |
| Hora-Mixers | 2.1.4 |

FrozenWasteland is the graphically elaborate one, so it is the useful test subject for
modification 1.


## Open questions, deliberately not guessed at

- Whether Rack's `dist` target copying `res` into an app bundle counts as
  distribution. Copying verbatim is permitted either way, so this only matters if a
  built app is ever shared, which is not the plan. Do not run `make dist` and assume
  it is fine. The `make-app.sh` wrapper sidesteps this entirely by never copying `res`.
