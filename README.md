
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

wgit is one window with three views, and the `[ Commit | Log | Stat ]` switcher at the top right switches between them.
`wgit` alone opens the commit view when the repo has changes, else the log view.

`wgit commit` (or `wgit c`, any unique prefix) opens the commit view, like gitg's:
staged, unstaged and untracked files in one list on the left,
the diff of the selected file's section on the right, with a title per file,
a message box and Rescan and Commit buttons below.
`wgit log` (or `wgit l`) opens the log view, like gitk:
the graph of all branches at the left, the diff of the selected commit at the right, its details below.
`wgit stat` (or `wgit s`) opens the stat view: the commits, lines and authors of a path and a period.
`wgit c <path>`, `wgit l <path>` and `wgit s <path>` work in the repo of that path.


## Commit view

- Click a file to scroll the diff to it; a closed file opens.
  When you scroll the diff to the next file, the list selects that file, unless you selected more than one.
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
- Files are shown under their folders. A chain of folders with one child is one row, like `src/ui/`.
- The colour of a name tells its state: added is green, deleted is red, modified and untracked are plain.
  Any other state shows its letter after the name.
- Click a section title or a folder to collapse or open it.
- Right-click a file for Stage, Unstage, Revert, Delete or Ignore; it acts on the selected files.
- Right-click a section title or a folder for Stage all, Unstage all, Revert all, Delete all or Ignore all.
  It acts on all files below it, also those in closed folders.
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
- The × at the top right closes wgit without a commit.
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
- Click a commit to see its diff, and below it its details: hash, parents, author, date,
  the signature check in words (green good, yellow good with a warning, red bad, grey none),
  the message and its git notes.
  A commit with git notes has a yellow square on the graph instead of a dot.
- Click a yellow tag chip to see an annotated tag instead: target, tagger, date, signature and message.
- Both views show the diff the same way: one section per file, a title with the path and `+added-removed` (a count of 0 is left out), then its hunks.
  Click a title to close or open that file; Expand all and Collapse all do it for every file.
  The title of the file at the top stays visible while you scroll, until the next title pushes it out.



## Stat view

- The file tree at the left shows the files of HEAD. Select a folder or a file to see the stats of that path only;
  `All files` shows the whole repo. The tree is read once, when the view opens.
- The `[ Last month | Last year | All ]` switcher sets the period: the last 30 days, the last 365 days, or the whole history of HEAD.
- The first section counts the commits and the added and removed lines, then lists the authors,
  each a name with an email, with their commits, their share of them and their lines.
- Each section comes from the same git log, for the path and the period.
  A merge counts as a commit but adds no lines, as git log shows it.
- Lines changed by file type adds the added and removed lines of each extension; a name with none, as `Makefile`,
  is `(no extension)`.
- The 20 files changed most often, each with the share of the commits that changed it and its lines:
  a commit changes many files, so the shares add up to more than 100%.
- Files added/deleted counts the files added and deleted of each extension, empty and binary ones too;
  a rename is a deletion and an addition.
- `All` reads the whole history in one git run, so a large repo takes some time.

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
- `stat.c` is the stat view; `stats()` in `git.c` makes its text.
- `ui.c` is the window: start, colour scheme, saved layout and the switch between the views.
- `wgit.c` picks the subcommand.


> Copyright (c) 2026 Lauri Rooden &lt;lauri@rooden.ee&gt;\
[MIT License](https://litejs.com/MIT-LICENSE.txt) |
[GitHub repo](https://github.com/litejs/wgit) |
[Buy Me A Tea][2]

