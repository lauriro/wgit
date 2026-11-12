
# wgit

A small git tool for Wayland, written in C with GTK 3 only.

`wgit commit` (or `wgit c`, any unique prefix) opens a commit window like gitg's:
staged, unstaged and untracked files in one list on the left,
the selected file's diff on the right,
a message box and a Commit button below.
`wgit c <path>` works in the repo of that path.

## Use

- Select files with Ctrl+click or Shift+click, all in one section.
  Stage, Unstage and Revert above the list work on the selected files.
- Revert asks first.
  In Unstaged it takes the files back to their staged content.
  In Untracked it deletes the files.
- Ignore (Untracked) opens a dialog with one `.gitignore` line per file,
  the path from the repo root.
  Change a line to a pattern if you like.
  Sort .gitignore sorts each block of lines between empty lines.
- Double-click a file to stage or unstage it.
- Click a section title to collapse or open the section.
- Right-click a section title for Stage all, Unstage all, Revert all, Delete all or Ignore all.
  The section opens and all its files are selected first, so you see what changes.
- Click a hunk header (`@@ ...`) to stage or unstage that hunk.
- Right-click the diff for more:
  Stage or Unstage hunk (the hunk under the pointer),
  Stage or Unstage lines (the selected lines),
  Revert hunk and Revert lines (in Unstaged only, asks first),
  More context and Less context.
  Ctrl+C copies the selected text.
- Amend loads the last message and commits with `--amend`.
  While Amend is on, Staged also shows the changes of the last commit.
  Unstage a file, hunk or line to take it out of the last commit.
- Cancel closes wgit without a commit.
- Drag the line over the message box to make the box higher.
- wgit remembers the window size and the pane positions in `~/.local/state/wgit`.
  It does not save the size of a maximized or fullscreen window.

Keys:

- `Ctrl+Enter` commits.
- `F5` reloads the file lists, as the Rescan button does.
- `Delete` in the file list does Revert on the selected files, and asks first.

## Build and test

The build machine needs no compiler, the build runs in Debian testing:

```sh
podman run --rm -v "$PWD:$PWD" -w "$PWD" docker.io/library/debian:testing-slim sh -c 'apt-get update -qq && apt-get install -y -qq gcc make pkg-config libgtk-3-dev git >/dev/null && make && make test'
```

For faster builds, make the local image once from `Containerfile`:

```sh
podman build -t wgit-build .
podman run --rm -v "$PWD:$PWD" -w "$PWD" wgit-build sh -c 'make && make test'
```

`make test` builds `wgit-test`,
a binary over the git plumbing in `git.c` without GTK,
and `test.sh` runs it against throwaway repos.

## Files

- `git.c` runs git, parses the status and builds hunk and line patches. No GTK.
- `diff-view.c` shows a diff, shared with a later `wgit log`.
- `commit.c` is the commit window.
- `wgit.c` picks the subcommand.

