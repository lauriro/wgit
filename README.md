
[1]: https://badgen.net/badge/icon/Buy%20Me%20A%20Tea/orange?icon=kofi&label
[2]: https://www.buymeacoffee.com/lauriro


LiteJS Wgit &ndash; [![Buy Me A Tea][1]][2]
==========

A small git UI for Wayland, written in C with GTK 3 only.

 - No dependencies besides GTK 3 and git

```sh
$ make
$ ./wgit
```

wgit is one window with two views, and the `[ Log | Commit ]` switcher at the top right switches between them.
`wgit` alone opens the commit view when the repo has changes, else the log view.

`wgit commit` (or `wgit c`, any unique prefix) opens the commit view, like gitg's:
staged, unstaged and untracked files in one list on the left,
the selected file's diff on the right,
a message box and Cancel, Rescan and Commit buttons below.
`wgit log` (or `wgit l`) opens the log view, like gitk:
the graph of all branches at the left, the selected commit and its diff at the right.
`wgit c <path>` and `wgit l <path>` work in the repo of that path.


## Commit view

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
- wgit remembers the window size and the pane positions of each view in `~/.local/state/wgit`.
  It does not save the size of a maximized or fullscreen window.
- wgit follows the dark or light colour scheme of the desktop (the freedesktop `color-scheme` setting)
  and switches while it runs.

### Keys

| Key          | Action                                                    |
|--------------|-----------------------------------------------------------|
| `Ctrl+Enter` | Commit                                                    |
| `F5`         | Reload the file lists, as Rescan does                     |
| `Delete`     | Revert the selected files, in the file list, asks first   |


## Log view

- The commits of all branches with their graph, as `git log --graph --all` draws it.
  A long history loads 10,000 commits at a time; the next part loads when you scroll to the end.
- A switch to the log view reloads it only when branches, tags, notes or HEAD changed;
  otherwise the selection and scroll position stay.
- The filter box over the list keeps only the commits whose hash, refs, subject or author contain the text,
  in any case. The graph is hidden while you filter.
- Each row: hash, refs as coloured boxes, subject, author and date.
  The HEAD branch is bold green, other local branches green, remote branches grey, tags yellow.
  A subject that starts with `!` (a breaking change) shows the `!` in bold red.
- Click a commit to see its details at the right top: hash, parents, author, date,
  the signature check in words (green good, yellow good with a warning, red bad, grey none),
  the message and its git notes.
  A commit with git notes has a yellow square on the graph instead of a dot.
- Click a yellow tag chip to see an annotated tag instead: target, tagger, date, signature and message.
- The diff has one section per file: a title with the path and `+added −removed`, then its hunks.
  Click a title to close or open that file; Expand all and Collapse all do it for every file.


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
- `commit.c` is the commit view.
- `log.c` is the log view.
- `ui.c` is the window: start, colour scheme, saved layout and the switch between the views.
- `wgit.c` picks the subcommand.


> Copyright (c) 2026 Lauri Rooden &lt;lauri@rooden.ee&gt;\
[MIT License](https://litejs.com/MIT-LICENSE.txt) |
[GitHub repo](https://github.com/litejs/wgit) |
[Buy Me A Tea][2]

