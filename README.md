
# wgit

A small git tool for Wayland, written in C with GTK 3 only.

`wgit commit` (or `wgit c`, any unique prefix) opens a commit window like gitg's:
unstaged and staged files on the left,
the selected file's diff on the right,
a message box and a Commit button below.
`wgit c <path>` works in the repo of that path.

## Use

- Double-click a file, or press Stage file / Unstage file, to move the whole file.
- Click a hunk header (`@@ ...`) to stage or unstage that hunk.
- Select lines in the diff and press Stage lines / Unstage lines to move only those changes.
  With no selection the button moves the line under the cursor.
- Amend loads the last message and commits with `--amend`.

Keys:

- `Ctrl+Enter` commits.
- `F5` reloads the file lists.

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

