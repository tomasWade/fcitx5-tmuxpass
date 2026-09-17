# fcitx5-tmuxpass

A tiny [fcitx5](https://fcitx-im.org) addon that makes prefix-key tools work
smoothly while a CJK input method is active.

**The problem:** with a Chinese/Japanese/Korean input method active, the keys
you press right after a tmux prefix (`C-b` / `C-q` …) get swallowed into the
candidate window instead of reaching tmux. So `prefix c` opens a candidate for
"c" instead of a new window, and you end up toggling the IM back and forth all
day. The same happens with any prefix-driven tool: screen, zellij, emacs
`C-x`, custom tools, etc.

**The fix:** the moment the prefix key is pressed, the addon performs a
configurable action — either switching the input method to English (default)
or passing keys through for a short window. No manual toggling, no racing
against a timer.

## How it works

The addon watches key events at the `PreInputMethod` phase via
`Instance::watchEvent`, before any IM engine sees them.

- **SwitchIM (default):** on prefix key press it calls `Instance::deactivate()`,
  switching the focused input context to the first input method (your plain
  keyboard layout). This happens synchronously inside the fcitx5 process, so
  every subsequent key — the tmux command and anything after — flows through
  the plain keyboard with zero race. Switch back to Chinese with your normal
  toggle key (e.g. `Ctrl+Space`) whenever you actually want to type CJK.
- **PassThrough:** keys pressed within a time window after the prefix (default
  400 ms, refreshed by each key) are filtered so the IM engine never sees
  them; the frontend delivers the original key to the application (wayland
  `sendKeyToVK` / dbus "not handled"). Real pass-through, not simulated input.
  Kept as an optional mode; see below for why it is not the default.

**Why SwitchIM is the default:** a time window is inherently a dilemma — set
it short and fast key sequences leak into the candidate window; set it long
and the first characters you genuinely type in CJK right after a prefix get
passed through as raw ASCII. Switching the input method is deterministic in
both directions: leaving Chinese is automatic and instant, coming back is an
explicit, deliberate toggle.

Works with every frontend (wayland_v2 / dbus / xim) and every input method
engine (rime, pinyin, …).

## Comparison with existing approaches

| Approach | Layer | Spawns a process per prefix | Needs tool config changes | Works beyond tmux | GUI-configurable |
| --- | --- | --- | --- | --- | --- |
| This addon (SwitchIM) | fcitx5, in-process | no | no | yes | yes |
| tmux `run-shell` + `fcitx5-remote` (blog hacks) | tmux | yes | yes (rebind prefix) | no | no |
| macOS `macism` + tmux root binding ([yugood.dev](https://blog.yugood.dev/posts/tmux-ime-auto-switch/)) | tmux + macOS TIS | yes | yes (disable default prefix) | no | no |
| vim autocmd + `fcitx5-remote` | editor | yes | yes (per editor) | no | no |

The tmux-level hacks also require disabling the default prefix and re-entering
the prefix table with `switch-client -T`, which has its own edge cases. An
addon inside fcitx5 sees every key before any IM engine regardless of which
tool consumes it.

## Install

Dependencies: `fcitx5` (ships all required headers on Arch), a C++20 compiler.

```bash
make
sudo make install    # libtmuxpass.so -> /usr/lib/fcitx5, conf -> /usr/share/fcitx5/addon
fcitx5 -r            # restart fcitx5
```

Arch users: see the AUR package `fcitx5-tmuxpass-git`.

Uninstall: `sudo make uninstall && fcitx5 -r`.

## Configuration

Edit `~/.config/fcitx5/conf/tmuxpass.conf` (then `fcitx5 -r`), or use
fcitx5-configtool's GUI page for this addon (changes apply immediately).

```ini
[PrefixKeys]
0=Control+q
# multiple prefix keys are supported, e.g. 1=Control+b

Action=SwitchIM

[Programs]
0=foot
# e.g. 1=kitty, 2=footclient; remove the section entirely = all programs

WindowMs=400
```

| Option | Default | Description |
| --- | --- | --- |
| `PrefixKeys` | `Control+q` | Prefix keys, standard fcitx key names, multiple allowed |
| `Action` | `SwitchIM` | `SwitchIM`: switch to first IM (English); `PassThrough`: time-window key pass-through |
| `Programs` | `foot` | React only in these programs (wayland appId); empty list = all programs |
| `WindowMs` | `400` | Pass-through window in ms (100–5000), `PassThrough` mode only |

## Verify

SwitchIM mode:

1. With rime (Chinese) active, press `Ctrl+Q c` in your terminal: tmux opens
   a new window, no candidate popup; `fcitx5-remote` now prints `1`.
2. `Ctrl+Q [` enters copy-mode; `j/k` scroll works. Press `Ctrl+Space` to get
   Chinese back afterwards.
3. Pressing `Ctrl+Q` outside the listed programs (e.g. in Firefox, where it
   means quit) does not touch the input method.
4. Normal Chinese typing is unaffected.

PassThrough mode:

5. `Ctrl+Q [` followed by `j/k` within the window scrolls; keys after the
   window expire go to the IM as usual.
6. The `Programs` filter applies to both modes; clearing the list restores
   global behaviour.

## FAQ

**How do I find my terminal's appId for `Programs`?**
Look at the `Program` column in the input context list of `fcitx5-diagnose`,
or check the wayland appId your terminal sets (foot → `foot`, kitty →
`kitty`, footclient windows still report `foot`).

**Why does SwitchIM switch to the first input method?**
fcitx5's "deactivate" goes to the first entry of your input method group.
Make sure a plain keyboard layout (e.g. `keyboard-us`) is the first item in
fcitx5-configtool's input method list — that is the default setup.

**Does this affect other windows?**
fcitx5 keeps per-program input state by default, so switching affects the
current application only. Other windows keep their state.

**ABI / upgrades:** after a fcitx5 major upgrade, re-run
`make && sudo make install`.

## License

LGPL-2.1-or-later, same as fcitx5 itself. See [LICENSE](LICENSE).
