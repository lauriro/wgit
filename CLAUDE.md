
# CLAUDE.md

Guidance for a Claude session that takes over this repo.
It holds what the code and the git history do not say; read the source for the rest.

## What wgit is

wgit is a Wayland-native git UI for the user's TarkOS laptop:
a Debian testing (glibc) desktop with the dwl compositor.
GTK 3.24 is already in the desktop's stack image, so wgit uses GTK 3 only:
no GTK 4, no libadwaita, no other libraries.

- One window with three views; a `[ Log | Commit | Stat ]` switcher at the top right switches them.
  `wgit` alone opens the commit view if `git status` lists anything, else the log view.
- `wgit c[ommit] [path]` opens the commit view, like gitg's.
  `wgit l[og] [path]` opens the log view, gitk-like.
  `wgit s[tat] [path]` opens the stat view: totals and authors of a path and a period.
  Any unique prefix of a subcommand works.

The desktop has no xdg-desktop-portal and no gsettings or dconf tools.
The bar, tark-shell, owns `org.freedesktop.portal.Desktop` on the session bus
and implements only `org.freedesktop.portal.Settings` for `org.freedesktop.appearance` `color-scheme`:
`ReadOne`, the older `Read` that wraps the value in one more variant (1 dark, 2 light),
and `SettingChanged` on `/org/freedesktop/portal/desktop`.

## Files

- `git.c`, `git.h`: the plumbing, with no GTK, so it is tested without a screen.
- `diff-view.c`, `diff-view.h`: the coloured diff view, shared by both views.
  `diff_view()` returns the text view; its parent is the scrolled window to pack.
- `commit.c`, `log.c`, `stat.c`: the three views. `ui.c`, `ui.h`: the window, the theme and the saved sizes.
- `wgit.c`: the subcommands. `test.c`, `test.sh`: the test binary `wgit-test` and its script.
- `Containerfile`: the local build image `wgit-build`.

Why some things are as they are:

- Every git run gets `GIT_LITERAL_PATHSPECS=1`, so names with `:` or `*` are plain paths,
  and `core.quotePath=false`, so a diff names a file as `git status -z` does.
- `status()` fills the shared `ent` and `nent`. `wgit` alone runs it once to pick the view;
  the commit view takes that result when it is built and runs `status()` itself only when `nent` is 0.
- Untracked files go as intent-to-add into a temporary index (`GIT_INDEX_FILE`),
  so two git runs give the diff of any number of them.
- For amend, `base` is HEAD^ or the empty tree, and the staged side compares the index with `base`.
- `patch()` keeps the lines in the chosen range.
  For staging, an unchosen `+` line is dropped and an unchosen `-` line becomes context;
  for unstaging and revert (applied with `-R`) the two rules swap.
  The hunk header counts are recomputed, the start numbers are kept.
  `to < 0` means the whole hunk whose header is at line `from`.
- The diff view replaces the header lines of each file with a title line,
  so a map from view lines to diff lines keeps clicks and selections on diff lines.
- The log checks the signature of the selected commit only, as `%G?` for all rows is slow.
  One `git show` gives the details and the diff: the format ends with a NUL and the diff comes after it.
- The log loads 10,000 commits at a time and adds only the new rows, so selection and scroll stay.
  It reloads on a switch to the log view only when the refs changed (`stale()`, needs git 2.45).
- The graph column is empty; `paint()` draws the graph with cairo after GTK draws the rows,
  mirrored so the first column is next to the hash.
  The column is at least four cells wide, the width of two branches, so a new branch does not move the rows.
- `stats()` in `git.c` makes the whole text of the stat view from one `git log --numstat`, so `test.sh` tests it.
  The activity bars and the tables of files, folders and file types were removed on 2026-11-14,
  until the user knows what the view must show; commit 2caae6a still has them.
  The three range pages are text views on one buffer, so the range switcher is a plain stack switcher.
  The file tree at the left is read once from HEAD with `git ls-tree`, when the view is built.
- One CSS rule removes the 100 px minimum width of the switcher buttons.
  Without it the three view buttons do not fit over the diff of the log view.
- A second CSS rule gives the text views named `plain` the background of the window:
  the commit details of the log view and the message box of the commit view.
  With the background of a text field, the padding around them looked like a frame.

## Build and test

The build machine has no compiler: it is a musl TarkOS host, so everything builds in a container.
On this host `git` in the shell is a function that may fail; use `command git`.

Plain image, slow, the command to give the user:

```sh
cd /home/lauri/code/litejs/wgit && podman run --rm -v "$PWD:$PWD" -w "$PWD" docker.io/library/debian:testing-slim sh -c 'apt-get update -qq && apt-get install -y -qq gcc make pkg-config libgtk-3-dev git >/dev/null && make && make test'
```

Local image, fast:

```sh
podman build -t wgit-build .
podman run --rm -v "$PWD:$PWD" -w "$PWD" wgit-build make wgit test
```

`make test` builds `wgit-test` and runs `test.sh`; the last line is `48 passed, 0 failed`.
The test names are in `test.sh`, the commands of `wgit-test` in `test.c`.

Size:

- Packed relocations (`-z,pack-relative-relocs`) need glibc 2.36 or later at run time.
- Each imported function costs about 90 bytes in the symbol tables,
  so a property set with `g_object_set` or `g_object_new` is smaller than a setter that is used once.
- The RELRO part ends on a page boundary, so the file size changes in steps of 4096 bytes.

## Checking the window

The GUI cannot be seen here. Check it under Xvfb in a throwaway container, driven with xdotool.
Build the image in the scratchpad, not in the repo, and delete it after with `podman rmi wgit-xv`:

```sh
printf 'FROM wgit-build\nRUN apt-get update -qq && apt-get install -y -qq --no-install-recommends xvfb xauth xdotool imagemagick x11-apps fonts-dejavu-core adwaita-icon-theme >/dev/null\n' > Containerfile
podman build -q -t wgit-xv .
```

A script `smoke.sh`, run as `podman run --rm -v "$PWD:$PWD" -w "$PWD" -v "$S:$S" wgit-xv sh -c "make -s && sh $S/smoke.sh $PWD/wgit $S"`:

```sh
set -e
W=$1; O=$2
cd /tmp && mkdir r && cd r && git init -q && git config user.name t && git config user.email t@t
seq 20 > a.txt; git add a.txt; git commit -qm init
sed -i 's/^2$/two/; s/^18$/eighteen/' a.txt; seq 3 > 'new file.txt'
Xvfb :1 -screen 0 1100x750x24 >/dev/null 2>&1 &
sleep 1; export DISPLAY=:1
$W c > $O/log.txt 2>&1 &
sleep 2
shot() { import -window root $O/$1.png; }
xdotool mousemove 60 103 click 1; sleep 1
xdotool mousemove 400 190 click 1; sleep 1; shot 3
git diff --cached -U0 > $O/cached.txt
xdotool mousemove 370 105 mousedown 1 mousemove 380 113 mousemove 385 122 mouseup 1; sleep 1
xdotool mousemove 385 122 click 3; sleep 1; xdotool mousemove 455 164 click 1; sleep 1; shot 5
xdotool mousemove 600 600 click 1 type 'Hello commit'; xdotool key ctrl+Return; sleep 1
git log --stat -1 >> $O/cached.txt
xdotool mousemove 360 676 click 1; sleep 1; shot 8
```

The window opens at 0,0 without decorations, 1000x700, when `~/.local/state/wgit` does not exist.
Take a screenshot first and read the PNG with the Read tool to find the positions.
`log.txt` must stay empty: a wrong property name gives a GTK warning there, not a build error.
In the commit view, list rows are 23 px apart from y 57, the bar buttons are at y 22
(Log, Commit and Stat at 821, 892 and 964), and Rescan and Commit are at 861 and 950, y 676.
To check the theme, add `dbus-daemon dbus-bin` to the image, run the script under `dbus-run-session --`,
and start a small GDBus mock that owns `org.freedesktop.portal.Desktop` like tark-shell does.

## Known limits

- GTK 3 drops the dark variant when `gtk-theme-name` names a theme that is not installed:
  it falls back to plain Adwaita, light, whatever the colour scheme says.
- A lone `\r` or a U+2029 in a diff line splits it into two buffer lines,
  so the line selection after it points at the wrong diff lines.
- Unstaging part of a newly added file fails: git will not remove only part of a new file.
  Unstaging the whole file works.

## The user's rules

The global rules for style, commits and language apply. For this repo:

- The shortest general code that does the job, no frameworks.
  Measure the stripped binary (`ls -l wgit` after `make`) and report its size; it was 64,168 bytes.
  The size changes in steps of 4096 bytes, so count the imports of a change.
- Commit scopes are the areas: `git:`, `diff:`, `log:`, `build:`, `docs:`.
  End the message with the `Co-Authored-By` line of the model that did the work.
- Every bug fix comes with a regression test in `test.sh`, and the failing test comes first.
  The window code has no test harness; say so in the commit message.
- The repo has no remote. Never add one and never push unless the user says so.
