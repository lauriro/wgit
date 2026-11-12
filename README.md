
[1]: https://badgen.net/badge/icon/Buy%20Me%20A%20Tea/orange?icon=kofi&label
[2]: https://www.buymeacoffee.com/lauriro


LiteJS Wgit &ndash; [![Buy Me A Tea][1]][2]
==========

A small git UI for Wayland, written in C with GTK 3 only.

 - No dependencies besides GTK 3 and git

```sh
$ make
$ ./wgit c
```

`wgit commit` (or `wgit c`, any unique prefix) opens a commit window like gitg's:
staged, unstaged and untracked files in one list on the left,
the selected file's diff on the right,
a message box and Cancel, Rescan and Commit buttons below.
`wgit c <path>` works in the repo of that path.
`wgit log` (or `wgit l`) shows the history: the graph of all branches at the left,
the selected commit and its diff at the right.


## Commit window

- Select files with Ctrl+click or Shift+click, all in one section.
  Stage, Unstage, Revert and Ignore above the list work on the selected files.
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
  Stage or Unstage lines (the selected lines, or with no selection the `+` or `-` line under the pointer),
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
- wgit follows the dark or light colour scheme of the desktop (the freedesktop `color-scheme` setting)
  and switches while it runs.

### Keys

| Key          | Action                                                    |
|--------------|-----------------------------------------------------------|
| `Ctrl+Enter` | Commit                                                    |
| `F5`         | Reload the file lists, as Rescan does                     |
| `Delete`     | Revert the selected files, in the file list, asks first   |


## Contributing

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

- `git.c` runs git, parses the status and builds hunk and line patches. No GTK.
- `diff-view.c` shows a diff, shared with a later `wgit log`.
- `commit.c` is the commit window.
- `log.c` is the history window.
- `ui.c` is what all windows share: start, colour scheme and saved layout.
- `wgit.c` picks the subcommand.


> Copyright (c) 2026 Lauri Rooden &lt;lauri@rooden.ee&gt;\
[MIT License](https://litejs.com/MIT-LICENSE.txt) |
[GitHub repo](https://github.com/litejs/wgit) |
[Buy Me A Tea][2]

