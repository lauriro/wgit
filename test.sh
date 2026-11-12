
#!/bin/sh
set -eu

T=$(cd "$(dirname "$0")" && pwd)/wgit-test
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cd "$D"
git init -q
git config user.name t
git config user.email t@t
PASS=0
FAIL=0

# ---- Helpers ----

check() {
	if [ "$2" = "$3" ]; then
		PASS=$((PASS + 1))
	else
		FAIL=$((FAIL + 1))
		printf 'FAIL %s\n--- want\n%s\n--- got\n%s\n' "$1" "$2" "$3"
	fi
}

changes() {
	git diff -U0 "$@" | grep '^[-+][^-+]' || :
}

at() {
	git diff $1 -- "$2" | grep -n -- "$3" | sed -n "${4:-1}p" | awk -F: '{print $1 - 1}'
}

seq 20 > a.txt
seq 10 > b.txt
seq 3 > d.txt
seq 3 > 'e f.txt'
git add .
git commit -qm init

# ---- Hunk ----

sed -i 's/^2$/two/; s/^18$/eighteen/' a.txt
"$T" stage a.txt "$(at '' a.txt '^@@' 2)"
check 'stage hunk' "$(printf -- '-18\n+eighteen')" "$(changes --cached)"
check 'other hunk stays' "$(printf -- '-2\n+two')" "$(changes -- a.txt)"
"$T" unstage a.txt "$(at --cached a.txt '^@@')"
check 'unstage hunk' '' "$(changes --cached)"
git checkout -q a.txt

# ---- Lines ----

printf '1\n2\n3\n4\nX\nY\nZ\n7\n8\n9\n10\n' > b.txt
"$T" stage b.txt "$(at '' b.txt '^-6')" "$(at '' b.txt '^+X')"
check 'stage lines' "$(printf -- '-6\n+X')" "$(changes --cached)"
check 'rest of lines stay' "$(printf -- '-5\n+Y\n+Z')" "$(changes -- b.txt)"
"$T" unstage b.txt "$(at --cached b.txt '^+X')" "$(at --cached b.txt '^+X')"
check 'unstage a line' '-6' "$(changes --cached)"
"$T" reset b.txt
git checkout -q b.txt

# ---- Files ----

seq 2 > c.txt
rm d.txt
printf 'one\n2\n3\n' > 'e f.txt'
check 'status' "$(printf ' D d.txt\n M e f.txt\n?? c.txt')" "$("$T")"
"$T" add c.txt
check 'new file' "$(printf 'A  c.txt\n+1\n+2')" "$("$T" | grep c.txt; changes --cached)"
"$T" reset c.txt
"$T" stage c.txt 5
check 'new file hunk' "$(printf 'A  c.txt\n+1\n+2')" "$("$T" | grep c.txt; changes --cached)"
"$T" reset c.txt
check 'unstage new file' '?? c.txt' "$("$T" | grep c.txt)"
"$T" add d.txt
check 'deleted file' "$(printf 'D  d.txt\n-1\n-2\n-3')" "$("$T" | grep d.txt; changes --cached)"
"$T" reset d.txt
check 'unstage deleted file' ' D d.txt' "$("$T" | grep d.txt)"
"$T" stage 'e f.txt' "$(at '' 'e f.txt' '^@@')"
check 'space in name' "$(printf -- '-1\n+one')" "$(changes --cached)"

# ---- Commit ----

"$T" commit "$(printf 'Change e f\n\nWhy it changed')"
check 'commit' "$(printf 'Change e f\n\nWhy it changed')" "$(git log -1 --format=%B)"
check 'committed' "$(printf -- '-1\n+one')" "$(git show --format= -U0 | grep '^[-+][^-+]')"
check 'index clean' '' "$(changes --cached)"

# ---- Several files ----

sed -i 's/^1$/uno/' a.txt
"$T" add a.txt c.txt d.txt
check 'stage files' "$(printf 'M  a.txt\nR  c.txt < d.txt')" "$("$T")"
"$T" reset a.txt c.txt
check 'unstage files' "$(printf ' M a.txt\n D d.txt\n?? c.txt')" "$("$T")"
"$T" revert a.txt d.txt
check 'revert files' '?? c.txt' "$("$T")"
"$T" revert c.txt
check 'delete untracked' '' "$("$T")"
sed -i 's/^2$/two/; s/^18$/eighteen/' a.txt
"$T" stage a.txt "$(at '' a.txt '^@@' 2)"
"$T" revert a.txt
check 'revert keeps index' "$(printf -- '-18\n+eighteen')" "$(changes --cached; changes -- a.txt)"

# ---- Amend ----

sed -i 's/^2$/two/' a.txt
"$T" add a.txt
"$T" commit 'Two and eighteen'
check 'amend status' 'M  a.txt' "$("$T"; "$T" amend)"
"$T" amend unstage a.txt "$(at '--cached HEAD^' a.txt '^@@' 2)"
check 'amend unstage hunk' "$(printf -- '-2\n+two')" "$(changes --cached HEAD^)"
"$T" amend reset a.txt
check 'amend unstage file' "$(printf -- '-2\n+two\n-18\n+eighteen')" "$(changes --cached HEAD^; changes -- a.txt)"
"$T" stage a.txt "$(at '' a.txt '^@@')"
"$T" amend commit 'Two'
check 'amend drops a change' "$(printf -- '-2\n+two\n-18\n+eighteen')" "$(git show --format= -U0 | grep '^[-+][^-+]'; changes -- a.txt)"
mkdir root && cd root && git init -q && git config user.name t && git config user.email t@t
seq 2 > r.txt && git add r.txt && git commit -qm root
check 'amend root commit' "$(printf 'A  r.txt\n?? r.txt')" "$("$T" amend; "$T" amend reset r.txt; "$T" amend)"

# ---- Ignore ----

cd ..
rm -rf root
touch 'x*y.txt' x1y.txt
"$T" ignore 'x*y.txt'
check 'ignore file' "$(printf '?? .gitignore\n?? x1y.txt\n/x\\*y.txt\n.')" "$("$T" | grep '^??'; cat .gitignore; echo .)"
printf '\n# build\nz.o\nb.o\n\nlog\n\n' > .gitignore
"$T" ignore x1y.txt
check 'ignore keeps end' "$(printf '\n# build\nz.o\nb.o\n\nlog\n/x1y.txt\n\n.')" "$(cat .gitignore; echo .)"
"$T" ignore-sorted 'x*y.txt'
check 'ignore sorted' "$(printf '\n# build\nb.o\nz.o\n\n/x1y.txt\n/x\\*y.txt\nlog\n\n.')" "$(cat .gitignore; echo .)"

# ---- Revert lines ----

printf '1\n2\n3\n4\nX\nY\nZ\n7\n8\n9\n10\n' > b.txt
"$T" discard b.txt "$(at '' b.txt '^-6')" "$(at '' b.txt '^+X')"
check 'revert lines' "$(printf -- '-5\n+Y\n+Z')" "$(changes --cached; changes -- b.txt)"
"$T" discard b.txt "$(at '' b.txt '^@@')"
check 'revert hunk' '' "$(changes --cached; changes -- b.txt)"

# ---- Context ----

sed -i 's/^2$/two/; s/^7$/seven/' b.txt
"$T" -U1 stage b.txt "$(at -U1 b.txt '^@@' 2)"
check 'less context' "$(printf -- '-7\n+seven')" "$(changes --cached)"

# ---- Log ----

mkdir ../log && cd ../log && git init -q -b main && git config user.name t && git config user.email t@t
git commit -q --allow-empty -m one
git checkout -q -b side && git commit -q --allow-empty -m two
git checkout -q main && git commit -q --allow-empty -m three
git merge -q --no-ff -m merge side && git tag -a v1 -m 'Version 1'
check 'log graph' "$(printf '%s\t%s\t%s\n' \
	'*   ' 'HEAD -> refs/heads/main, tag: refs/tags/v1' merge \
	'|\  ' '' '' \
	'| * ' refs/heads/side two \
	'* | ' '' three \
	'|/  ' '' '' \
	'* ' '' one)" "$("$T" log)"
git notes add -m 'A note' HEAD~1
check 'log without notes' '' "$("$T" log | grep 'Notes added')"

printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]

