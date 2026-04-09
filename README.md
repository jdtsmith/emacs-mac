# emacs-mac

This is an experimental build of the [emacs-mac](https://bitbucket.org/mituharu/emacs-mac) (aka Carbon[^1] Emacs) port of emacs, updated for Emacs v30.2, and Emacs master.

> [!WARNING]
> This is an experimental build of `emacs-mac`; there will certainly be bugs. We are looking for feedback and testing from experienced users.  If you are familiar with or willing to learn about running new builds of Emacs, including under a debugger, perfect.  If you are a Mac developer familiar with ObjC or Mac Window frameworks, even better (get in touch)!  Other users should stick to the official NS build or recent v29.4 emacs-mac release for now.

> [!NOTE]
> Branch organization and naming are subject to change.

## Status

Known working systems:

- MacOS 26
- MacOS 15 (Sequoia) on ARM64 (M1, M2, M3, M4), X86_64 (Intel)
- MacOS 14 (Sonoma) on ARM64 (M1, M3)
- MacOS 12 (Monterey) on X86_64 (Intel)

Please see the [discussion](../../discussions/categories/show-and-tell) for advice on build configurations for your system.

>[!NOTE]
> Please start a [discussion](../../discussions/categories/show-and-tell) to report your build experiences, even if you encounter no problems.  Mention your OS version, CPU, which branch you built, and any other relevant details, including the build/configure flags you used.

This build is kept current with the [`emacs-30`](https://github.com/emacs-mirror/emacs/tree/emacs-30) release branch.

For an experimental build synced with Emacs master, see [this branch](https://github.com/jdtsmith/emacs-mac/tree/emacs-mac-gnu_master_exp).

## Reporting problems

> [!IMPORTANT]
> If you encounter a problem with this build, **first** please [read this wiki note on debugging](../../wiki/How-to-help-improve-emacs%E2%80%90mac-with-actionable-issues-and-debug-info), then open [an issue](../../issues).


## Install & Config

See the `emacs-mac-30_1_exp` or `emacs-mac-gnu_master_exp` branch and the file `README-mac` for additional compile instructions.  

> [!NOTE]
> On MacOS, `gcc` is aliased to the `clang` compiler, which is required to build `emacs-mac`.  Recent `gcc` versions either cannot build for the architecture (e.g. Apple Silicon) and/or do not support [blocks](https://en.wikipedia.org/wiki/Blocks_(C_language_extension)), which this build uses heavily.

Briefly:

### Install build tools

```bash
brew install pkgconf texinfo
```

### Install (optional) libraries

If you'd like to build with tree-sitter support, native-compilation, and RSVG (all recommended), first install the necessary libraries, here demonstrated using HomeBrew:

```bash
brew install tree-sitter libgccjit librsvg
```

#### Tree Sitter in Emacs 30

If you'd like to build Emacs 30 (build on [this branch](https://github.com/jdtsmith/emacs-mac/tree/emacs-mac-30_1_exp)) with tree-sitter support, you'll need to install the older version `tree-sitter-0.25` to maintain compatibility.  For example:

```bash
brew install tree-sitter@0.25
```

With this version in place, inform configure of its location using `CFLAGS` and `LDDFLAGS`, for example:

```bash
CFLAGS="-I/opt/homebrew/opt/tree-sitter@0.25/include ..." LDDFLAGS="-L/opt/homebrew/opt/tree-sitter@0.25/lib" ./configure --with-tree-sitter ...
```

Note: use `CFLAGS` instead of `CPPFLAGS`. You can consult some Caveats with:

```bash
brew info tree-sitter@0.25
```

This is not required for Emacs 31, as it supports the latest tree-sitter ABI.

### Configure

You can configure the app either as self-contained (all resources live within the app), or non-self-contained (the default).  A self-contained app is recommended.  The recommended configuration options are given below; see the file `README-mac` for others.

#### Self-contained

A _self-contained_ app by default will go into `/Applications/Emacs.app`.

```bash
./autogen.sh
```

```bash
CFLAGS="-O2 -mcpu=native" ./configure --with-native-compilation --with-tree-sitter --enable-mac-app=yes --enable-mac-self-contained
```

Optionally, you can add `-DFD_SETSIZE=10000 -D_DARWIN_UNLIMITED_SELECT` to `CFLAGS` to increase the file descriptor limit, to help with packages that open many connections (like LSP).
Note that this may degrade performance in some cases.

You can specify another build directory for the self-contained app using `--enable-mac-app=/path/to/dir`.

>[!NOTE]
> Please note the `yes` argument to `--enable-mac-app=yes`, which is required to build a self-contained app under `/Applications`.

#### Non self-contained

```bash
./autogen.sh
CFLAGS="-O2 -mcpu=native" ./configure --with-native-compilation --with-tree-sitter  
```

### Build

```bash
make -j6 # or however many CPU cores you want to use
```

You'll find the staging build of the app under `mac/`.

### Install

This step compresses EL files and fully populates the app.

#### Self-contained

```bash
make install # Installs all resources under /Applications/Emacs.app (or wherever your self-contained build is going)
```

#### Non self-contained

```bash
sudo make install  # installs resources in, e.g., /usr/local/share/emacs/30.2.50
```

#### No install, e.g. for debug

If you choose not to `make install`, but instead want to run the application directly from the `mac/` sub-directory, you may need to:

```bash
% cd mac/Emacs.app/Contents
% ln -s ../../../native-lisp .
```

to associate the native lisp files.  This is useful for debugging, to quickly rebuild and test, for example (saving the install step).  But a [self-contained app](#Self-contained) build is easier, and recommended for most uses.

## Tips

- The new builtin `pixel-scroll-precision-mode` does not work with `emacs-mac`, which has its own flavor of scroll event.  Instead, check out [`ultra-scroll`](https://github.com/jdtsmith/ultra-scroll), which was designed for `emacs-mac` originally.
- Some tools want a proper `emacs` command.  If you build self-contained, you can link `/usr/local/bin/emacs` -> `/Applications/Emacs.app/Contents/MacOS/Emacs`.

## Additions

Several additional features and fixes have been added on top of upstream `emacs-mac` and Emacs proper.

### Features

- A `New Frame` Dock Menu entry.
- Support compiling with non-system versions of CLANG.
- New custom variable `mac-underwave-thickness` to customize the thickness of squiggly underlines (e.g., as drawn by linters or spell-checkers).
- A new [full-featured `Window` menu](../../pull/21) (including tab and tiling support, with default system shortcuts, e.g. `C-Fn-left/right/up/down`).  Thanks to @rymndhng!
- Add a new `mac-raise-all-frames` command, also found in the `Window` menu ("Bring All to Front").
- A new `mac-toggle-frame-full-screen` command for toggling "real" full-screen window display.
- Automatically enable Retina 2x support for known high-DPI images.

### Bug fixes

- Avoid crashes when selecting certain fonts from the system font panel.
- Guard against using native image API when unavailable.
- Prevent zombie "Emacs Web Content" processes [on SVG load](../../issues/9), ~~restoring normal WebView SVG rendering for MacOS v14+~~.  Update: `WebView` is deprecated, so this has been reverted and another workaround installed. It's recommended to build with RSVG (it is enabled by default if the `librsvg2` library is found during build).
- Fix [occasional hangs](../../pull/20) when callbacks are invoked on dying threads.
- Fix [rare occasional hangs](../../pull/86) upon waking from sleep with multiple monitors.
- Normalized the use of `CF|NS_NOESCAPE` to prevent compilation issues and hangs when built with non-system CLANG.  See [this PR](../../pull/76).
- Handle cropped PDF images correctly.
- Correctly handle pixel-doubled images during allocation (fix regression from FSF upstream).
- Fix various compiler warnings related to type casting. 

## Debugging

If you get crashes or just want to help with debugging, it would be very useful to run emacs-mac under `lldb`, the clang debugger.  Here's how:

1. Build emacs-mac with debug flags:
   ```
    CFLAGS="-O0 -g3" ./configure --with-native-compilation --with-tree-sitter --enable-mac-app=yes
    ```
2.  Link in the [native-lisp directory](#no-install-eg-for-debug).   
2.  In an `~/.lldbinit` file, add `settings set target.load-cwd-lldbinit true`, so Emacs can read the custom lldb commands it has defined.
3.  Start the emacs binary from the `src/` directory, like:
    ```bash
    %lldb ../mac/Emacs.app
    ```
    Then `run` (or better, `run -Q`).
1. Now cause your crash to occur, go `up` to the frame of interest, and use `xprint`, `p`, etc. on the potentially problematic variables.
2. You can also try `gui` which is a little curses-based terminal GUI inside lldb (slow for me though), or [`realgud-lldb`](https://github.com/realgud/realgud-lldb) which isn't very complete but can do some things.

## Contributions

We are very happy to accept contributions, especially bug fixes and other improvements.  Note that, to preserve options for upstreaming, any contributor of substantial code must have valid copyright assignment paperwork with the FSF, and be willing to assign copyright, should that option be taken in the future. 

## Notes

You can read about the issues encountered during the merge of Emacs v30 in the [debugging notes](https://github.com/jdtsmith/emacs-mac/blob/emacs-mac-30_1_exp/devel_update_notes.org).

[^1]: Calling this the "Carbon" port is a vestigial nod to its origins back in the pre-OSX days. It is also what `M-x emacs-version` says.  But "Carbon" is a misnomer now.  The ancient Carbon API never supported 64bit applications, and was deprecated and removed by Apple in 2019.  A few convenience functions do remain (e.g. `Carbon.h`), and these are used by the NS build as well.  **Both NS and emacs-mac are Cocoa applications**.
