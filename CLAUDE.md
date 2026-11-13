
# CLAUDE.md

Guidance for a Claude session that takes over this repo.

## What wgit is

wgit is a Wayland-native git UI for the user's TarkOS laptop.
The laptop runs a Debian testing (glibc) desktop with the dwl compositor.
GTK 3.24 is already in the desktop's stack image, so wgit uses GTK 3 only:
no GTK 4, no libadwaita, no other libraries.
The binary goes into a desktop image later, so it is built against Debian testing.
The desktop has no xdg-desktop-portal and no gsettings or dconf tools,
but GTK reads GSettings through the dconf module from `~/.config/dconf/user` (written by `/usr/libexec/dconf-service`).
An old Ubuntu copy of that file set `gtk-theme` to an uninstalled Yaru-dark and kept wgit light;
the user renamed it to `user.ubuntu` on 2026-11-13.
The bar, tark-shell, owns `org.freedesktop.portal.Desktop` on the session bus
and implements only `org.freedesktop.portal.Settings` for `org.freedesktop.appearance` `color-scheme`:
`ReadOne`, the older `Read` that wraps the value in one more variant (1 dark, 2 light),
and `SettingChanged` on `/org/freedesktop/portal/desktop` when the user switches.

- wgit is one window with two views, commit and log; a `[ Log | Commit ]` switcher at the top right switches them.
  `wgit` alone opens the commit view if `git status` lists anything, else the log view.
- `wgit c` / `wgit commit [path]` opens the commit view, like gitg's commit view.
  `wgit l` / `wgit log [path]` opens the log view, gitk-like:
  the graph of `git log --graph --all` at the left, the diff of the selected commit at the right and its details below.
  Any unique prefix of a subcommand works; an unknown one prints the usage and exits 1.

## Files

- `git.c`, `git.h`: the plumbing, with no GTK, so it is tested without a screen.
  It runs git (stdin in, stdout out; on failure the output is git's stderr),
  parses `git status --porcelain=v1 -z`,
  gets a file's diff (`--no-index` against `/dev/null` for an untracked file),
  or with `diffs()` the diff of a whole section (0 staged, 1 unstaged, 2 all untracked files),
  and builds hunk and line patches for `git apply --cached [-R] -`,
  or `git apply -R -` on the work tree to revert lines.
  `files()` stages, unstages or reverts several files in one git run;
  revert checks out tracked files from the index and deletes untracked files with `git clean -f`.
  When `base` is set (`parent()`: HEAD^, or the empty tree on a root commit),
  the staged side compares the index with `base` instead of HEAD, for amend:
  `status()` takes the staged letters from `git diff --cached --name-status base`,
  `diff()` diffs against it and unstage resets to it.
  `patterns()` makes one `.gitignore` line per file, `/` plus the path with `\*?[` and a trailing space escaped.
  `ignore()` adds lines to the root `.gitignore` above its trailing empty lines,
  and optionally sorts each block between empty lines.
  `history(n, max)` splits `git log --graph --all -n max` (without `refs/notes`) into rows (graph, hash, full hash, refs, author, date, subject;
  a connector row such as `|/` has no hash); `note` marks a commit listed by `git notes list`.
  `joins()` says if a `*` at column k joins the row above or below: a `|` or `*` at k,
  or a slash from the next column that ends or starts at k.
  `GIT_LITERAL_PATHSPECS=1` is set for every git run, so names with `:` or `*` are plain paths.
  `core.quotePath=false` is set the same way through `GIT_CONFIG_COUNT`, so a diff names a file as `git status -z` does.
- `diff-view.c`, `diff-view.h`: the coloured diff view, shared by commit and log.
  Each view keeps its callbacks on its own widget, so one window can hold several;
  `diff_show()` and `diff_lines()` take the view.
  `diff_files()` shows a diff of many files, in both views: a bold title line per file (`▾ path +a −d`,
  in the GTK font with the `pan-down-symbolic` or `pan-end-symbolic` icon, as the commit view list has)
  instead of the `diff --git`, `index`, `---` and `+++` lines; a click on a title hides its lines with an invisible tag,
  and `diff_fold()` does it for all titles. A map from view lines to diff lines keeps the hunk click, the menu
  and `diff_lines()` on diff lines. `diff_tools()` makes the Expand all, Collapse all and stats bar part, and
  `diff_goto()` opens a file's section and scrolls to it.
  `stick()` draws the title of the file at the top over the view once the title has scrolled away, pushed up by
  the next title; a press or click on it folds that file and never reaches the line under it.
  A click on a line that starts with `@` calls the hunk callback with `(line, -1)`.
  A right click adds to the text view menu through the menu callback,
  with the header line of the hunk under the pointer and the `+` or `-` line under it, each -1 if there is none.
- `commit.c`: the commit view (`commit_view()`); it shows the diff of the whole section of the selected file
  and, within that section, only scrolls to the file.
- `log.c`: the log view (`log_view()`); it checks the signature of the selected commit only, as `%G?` for all rows is slow.
  The list holds commit rows only; connector lines such as `|/` get no row.
  It loads 10,000 commits at a time: when `paint()` draws the last row of a full page, `more()` reruns
  `history()` with a bigger limit and `fill()` adds only the new rows, so selection and scroll stay.
  A filter search loads all commits first.
  `stale()` keys the refs (`git for-each-ref` with `%(HEAD)`, and `git rev-parse HEAD`); a switch to the log view
  reloads only when the key changed, and `more()` reloads in full instead of adding a page then.
  The graph column is empty; after GTK draws the visible rows, `paint()` draws the graph with cairo,
  mirrored so the first column is next to the hash. `span()` maps the git text lines from one commit to the next
  onto the one row height between their centres, so connector lines share that space and lines join:
  `|` vertical, `/` and `\` to the next column, `*` a dot (a yellow square with notes) with half lines where `joins()` says so.
  Colours go by column; a slash takes its right column.
  The filter box drives a `GtkTreeModelFilter`; while it has text, the graph is hidden.
  A button release on a row finds the chip under the pointer in the row's Pango layout (its background attribute);
  `tag()` shows an annotated tag from `git for-each-ref` and `git verify-tag`, a lightweight tag changes nothing.
- `ui.c`, `ui.h`: the window. `app()` checks the repo, starts GTK, follows the desktop colour scheme (`theme()`),
  restores the window size and both views' pane positions from `~/.local/state/wgit` (six numbers)
  and holds both views as pages of a `GtkStack`, with a `GtkStackSwitcher` in the bar that `tabs()` puts over each view's right side.
  A page is empty until it is first shown; then `show()` builds the view into it, and later shows reload it.
  A view function builds its view when its pane array is empty and only reloads it otherwise.
- `wgit.c`: the subcommand table and the prefix match.
- `test.c`, `test.sh`: the test binary `wgit-test` and the script that drives it.
- `Containerfile`: the local build image `wgit-build`.

How a line patch is made (`patch()` in `git.c`; in a diff of many files it uses only the block of the file that holds line FROM):
lines in the chosen range are kept;
an unchosen `+` line is dropped and an unchosen `-` line becomes context when staging;
for unstaging (`rev`, applied with `-R` to a `--cached` diff) the two rules swap.
Revert uses the unstage rules on the work tree diff and applies with `-R` to the work tree.
The hunk header counts are recomputed, the start numbers are kept.
`to < 0` means the whole hunk whose header is at line `from`.

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
podman run --rm -v "$PWD:$PWD" -w "$PWD" wgit-build sh -c 'make && make test'
```

`make` builds `wgit` with `-Os -fno-asynchronous-unwind-tables -s -Wl,-z,noseparate-code`; `make test` builds `wgit-test` and runs `test.sh`.
`wgit-test` with no arguments prints the status;
`add|reset|revert FILE...` stages, unstages or reverts whole files;
`ignore|ignore-sorted FILE...` adds the files' patterns to `.gitignore`;
`stage|unstage|discard FILE FROM [TO]` stages, unstages or reverts diff lines FROM..TO (0-based), or the hunk whose header is FROM;
FILE `:N` uses the diff of section N instead; `diffs N` prints that diff;
an unknown command exits 2;
`commit MSG` commits;
`log [N]` prints the graph, the refs and the subject, tab-separated, of the first N commits (all without N).
`joins` prints each commit subject with `u` and `d` when its dot joins the row above and below.
`notes` prints the subjects of the commits that have git notes.
A first argument `amend` sets `base`, and then `commit` amends.
After it, `-U<n>` sets the diff context (`context`, 3 by default).

The 42 tests in `test.sh`, checked against the index with `git diff --cached` or against the status:

1. stage hunk: the second hunk of a file with two hunks is staged.
2. other hunk stays: the first hunk stays unstaged.
3. unstage hunk: the staged hunk goes back.
4. stage lines: a removed and an added line next to each other in one hunk are staged.
5. rest of lines stay: the other `-` and `+` lines of that hunk stay unstaged.
6. unstage a line: one added line is unstaged from the index.
7. status: deleted, modified (name with a space) and untracked entries parse.
8. new file: an untracked file is staged whole.
9. new file hunk: an untracked file is staged through its hunk.
10. unstage new file: it is untracked again.
11. deleted file: a deletion is staged.
12. unstage deleted file: the deletion goes back.
13. space in name: a hunk of `e f.txt` is staged.
14. commit: the message comes back from `git log`.
15. committed: the commit holds the staged change.
16. index clean: nothing is staged after the commit.
17. stage files: a modified file and a rename (a new file plus a deleted one) are staged in one run.
18. unstage files: they go back, the rename by its new name only.
19. revert files: a modified and a deleted file get their index content back.
20. delete untracked: revert deletes an untracked file.
21. revert keeps index: revert of a partly staged file keeps the staged hunk.
22. amend status: a clean tree shows the last commit's file as staged in amend mode only.
23. amend unstage hunk: one hunk of the last commit goes back to HEAD^.
24. amend unstage file: the whole file goes back to HEAD^, the work tree keeps the changes.
25. amend drops a change: the amended commit holds only the change still staged.
26. amend root commit: on the first commit the file shows as added and unstages to untracked.
27. ignore file: a new `.gitignore` gets `/x\*y.txt`, which hides `x*y.txt` but not `x1y.txt`.
28. ignore keeps end: a new line goes into the last block, above the trailing empty line.
29. ignore sorted: each block between empty lines is sorted, the empty lines stay.
30. revert lines: a removed and an added line are reverted in the work tree, the index stays.
31. revert hunk: the rest of the hunk is reverted, the file is clean.
32. less context: with `-U1` two near changes are two hunks, and the second one is staged alone.
33. log graph: a branch and a merge give the graph rows, refs and subjects of `git log --graph`.
34. log without notes: the commit that holds git notes is not in the history.
35. log joins: a dot joins a `|` above and below, the `\` it starts from and the `/` it goes into.
36. log notes: the commit with a git note is marked.
37. log limit: `-n 2` gives the first two commits and the connector line after them, as a full run starts.
38. section stage hunk: a hunk of the second file of an unstaged section diff is staged in that file.
39. section unstage hunk: it is unstaged through the staged section diff.
40. untracked section: the untracked diff holds every untracked file.
41. section stage new file: a hunk of the second untracked file is staged.
42. unquoted path: the untracked diff names `ä.txt` as it is, not as `"b/\303\244.txt"`.

The summary line is `42 passed, 0 failed`.

## Checking the window

The GUI cannot be seen here. It was checked under Xvfb in a throwaway container, driven with xdotool.
Build the image (delete it after with `podman rmi wgit-xv`), in the scratchpad, not in the repo:

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

`wgit c` opens at 0,0 without decorations, 1000x700,
when `~/.local/state/wgit` (the size and pane positions saved on close) does not exist.
At that size the file pane is 341 px wide; Stage, Unstage, Revert and Ignore are at 41, 127, 215 and 297, y 22.
List rows are 23 px apart starting at y 57,
so with nothing staged the first unstaged row is at y 103.
The diff shows the whole section, a title line per file, 17 px apart from y 54, under the bar with Expand all and Collapse all
(399 and 511, y 22) and the Log and Commit switcher (811 and 933, y 22);
the message handle is at y 527, Amend at 360,676, Cancel, Rescan and Commit at 776, 861 and 950, y 676.
In the diff menu, Stage hunk is 17 px and Stage lines 42 px below the right click.
To check the log view, run `$W l` in a clone of `../litejs` (mount it read-only and `git clone` it in the container):
the filter box is at y 20, list rows are 23 px apart from y 57,
Expand all and Collapse all are at 508 and 620, y 22, the details below the diff.
Read the PNGs with the Read tool to see them.
To check the theme, add `dbus-daemon dbus-bin` to the image, run the script under `dbus-run-session --`,
and start a small GDBus mock that owns `org.freedesktop.portal.Desktop` like tark-shell does,
answers `ReadOne` and `Read` and emits `SettingChanged` on a signal.
The window is still to be tried by the user on Wayland.

## Known limits

- GTK 3 drops the dark variant when `gtk-theme-name` names a theme that is not installed:
  it falls back to plain Adwaita, light, whatever the colour scheme says.

- A lone `\r` or a U+2029 in a diff line splits it into two buffer lines,
  so the line selection after it points at the wrong diff lines.
- Unstaging part of a newly added file fails: git will not remove only part of a new file.
  Unstaging the whole file works.

## The user's rules

- The shortest general code that does the job, no frameworks.
  Measure the stripped binary (`ls -l wgit` after `make`) and report its size; it was 66,384 bytes.
- No comments that say what the code does; a rare comment says why.
- Tabs for indentation.
- Every file except JSON starts with an empty line and ends with an empty line.
- No trailing whitespace, no indented empty lines.
- File names use hyphens.
- Semantic line breaks in docs: break at the end of a sentence or clause.
- One thing per commit.
  Subject `scope: Capitalized imperative summary`, at most 50 characters, no period, the scope optional (`diff:`, `build:`, `docs:`).
  The body says why, wrapped at about 72.
  End the message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Every bug fix comes with a regression test in `test.sh`, and the failing test comes first.
- The repo has no remote. Never add one and never push unless the user says so.
- Explain things to the user in ASD-STE100 Simplified Technical English:
  short sentences, one fact per sentence, active voice, common words.

