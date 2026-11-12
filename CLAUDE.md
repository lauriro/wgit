
# CLAUDE.md

Guidance for a Claude session that takes over this repo.

## What wgit is

wgit is a Wayland-native git UI for the user's TarkOS laptop.
The laptop runs a Debian testing (glibc) desktop with the dwl compositor.
GTK 3.24 is already in the desktop's stack image, so wgit uses GTK 3 only:
no GTK 4, no libadwaita, no other libraries.
The binary goes into a desktop image later, so it is built against Debian testing.

- `wgit` alone prints the usage and exits 0; an unknown subcommand exits 1.
- `wgit c` / `wgit commit [path]` opens the commit window, like gitg's commit view.
  Any unique prefix of a subcommand works.
- `wgit l[og]` comes next: a history browser.
  It reuses `diff-view.c` and `git.c`,
  and draws the branch graph from `git log --graph` ASCII with cairo, one cell per character.

## Files

- `git.c`, `git.h`: the plumbing, with no GTK, so it is tested without a screen.
  It runs git (stdin in, stdout out; on failure the output is git's stderr),
  parses `git status --porcelain=v1 -z`,
  gets a file's diff (`--no-index` against `/dev/null` for an untracked file),
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
  `GIT_LITERAL_PATHSPECS=1` is set for every git run, so names with `:` or `*` are plain paths.
- `diff-view.c`, `diff-view.h`: the coloured diff view, shared by commit and the later log.
  The buffer holds the diff line for line, so a buffer line number is a diff line number.
  This is what lets a selection map straight onto `patch()`.
  A click on a line that starts with `@` calls the hunk callback with `(line, -1)`.
  A right click adds to the text view menu through the menu callback,
  with the header line of the hunk under the pointer, or -1.
- `commit.c`: the commit window (`cmd_commit`).
- `wgit.c`: the subcommand table and the prefix match.
- `test.c`, `test.sh`: the test binary `wgit-test` and the script that drives it.
- `Containerfile`: the local build image `wgit-build`.

How a line patch is made (`patch()` in `git.c`):
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

`make` builds `wgit` with `-Os -s -Wl,-z,noseparate-code`; `make test` builds `wgit-test` and runs `test.sh`.
`wgit-test` with no arguments prints the status;
`add|reset|revert FILE...` stages, unstages or reverts whole files;
`ignore|ignore-sorted FILE...` adds the files' patterns to `.gitignore`;
`stage|unstage|discard FILE FROM [TO]` stages, unstages or reverts diff lines FROM..TO (0-based), or the hunk whose header is FROM;
an unknown command exits 2;
`commit MSG` commits.
A first argument `amend` sets `base`, and then `commit` amends.
After it, `-U<n>` sets the diff context (`context`, 3 by default).

The 32 tests in `test.sh`, checked against the index with `git diff --cached` or against the status:

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

The summary line is `32 passed, 0 failed`.

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
xdotool mousemove 400 201 click 1; sleep 1; shot 3
git diff --cached -U0 > $O/cached.txt
xdotool mousemove 370 117 mousedown 1 mousemove 380 125 mousemove 385 134 mouseup 1; sleep 1
xdotool mousemove 385 134 click 3; sleep 1; xdotool mousemove 455 176 click 1; sleep 1; shot 5
xdotool mousemove 600 600 click 1 type 'Hello commit'; xdotool key ctrl+Return; sleep 1
git log --stat -1 >> $O/cached.txt
xdotool mousemove 360 676 click 1; sleep 1; shot 8
```

The window opens at 0,0 without decorations, 1000x700,
when `~/.local/state/wgit` (the size and pane positions saved on close) does not exist.
At that size the file pane is 341 px wide; Stage, Unstage, Revert and Ignore are at 41, 127, 215 and 297, y 22.
List rows are 23 px apart starting at y 57,
so with nothing staged the first unstaged row is at y 103.
Diff lines are 17 px apart starting at y 14,
the message handle is at y 527, Amend at 360,676, Cancel, Rescan and Commit at 776, 861 and 950, y 676.
In the diff menu, Stage hunk is 17 px and Stage lines 42 px below the right click.
Read the PNGs with the Read tool to see them.
The window is still to be tried by the user on Wayland.

## Known limits

- A lone `\r` or a U+2029 in a diff line splits it into two buffer lines,
  so the line selection after it points at the wrong diff lines.
- Unstaging part of a newly added file fails: git will not remove only part of a new file.
  Unstaging the whole file works.

## The user's rules

- The shortest general code that does the job, no frameworks.
  Measure the stripped binary (`ls -l wgit` after `make`) and report its size; it was 36,632 bytes.
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

