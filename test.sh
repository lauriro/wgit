
#!/bin/sh
set -eu

T=$(cd "$(dirname "$0")" && pwd)/wgit-test
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
mkdir "$D/main"
cd "$D/main"
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
git checkout -q -b dev && git commit -q --allow-empty -m dev1
git checkout -q main && GIT_COMMITTER_DATE=2030-01-01T00:00:00 git commit -q --allow-empty -m four
check 'log joins' "$(printf '%s\n' 'four -d' 'dev1 -d' 'merge ud' 'two ud' 'three ud' 'one u-')" "$("$T" joins)"
check 'log notes' three "$("$T" notes)"
check 'log limit' "$(printf '%s\t%s\t%s\n' '* ' 'HEAD -> refs/heads/main' four '| * ' refs/heads/dev dev1 '|/  ' '' '')" "$("$T" log 2)"

# ---- Section diffs ----

nth() {
	sed -n '/^@@/=' | sed -n "$1p" | awk '{print $1 - 1}'
}

seq 20 > p.txt
seq 101 120 > q.txt
git add p.txt q.txt
git commit -qm files
sed -i 's/^2$/two/' p.txt
sed -i 's/^103$/three/; s/^118$/eighteen/' q.txt
echo new > u.txt
echo other > v.txt
"$T" stage :1 "$(git diff | nth 3)"
check 'section stage hunk' "$(printf -- 'q.txt\n-118\n+eighteen')" "$(git diff --cached --name-only; changes --cached)"
"$T" unstage :0 "$(git diff --cached | nth 1)"
check 'section unstage hunk' '' "$(changes --cached)"
check 'untracked section' "$(printf '+++ b/u.txt\n+++ b/v.txt')" "$("$T" diffs 2 | grep '^+++')"
"$T" stage :2 "$("$T" diffs 2 | nth 2)"
check 'section stage new file' '+other' "$(changes --cached)"
echo x > 'ä.txt'
check 'unquoted path' '+++ b/ä.txt' "$("$T" diffs 2 | grep '^+++' | tail -1)"
echo x > '"w".txt'
echo x > 'x\y.txt'
echo x > "$(printf 'n\nl.txt')"
check 'odd names' 5 "$("$T" diffs 2 | grep -c '^@@')"
"$T" stage :2 0 10000
check 'stage all odd names' '' "$(git ls-files --others --exclude-standard)"
check 'newline filename content' x "$(git show ":$(printf 'n\nl.txt')")"
check 'backslash filename content' x "$(git show ':x\y.txt')"

# ---- No newline at the end ----

mkdir ../fix && cd ../fix && git init -q && git config user.name t && git config user.email t@t
printf 'a\nb\nlast' > n.txt
seq 20 > a.txt
seq 101 120 > q.txt
git add .
git commit -qm init
printf 'a\nb\nlast\nnew1\n' > n.txt
"$T" stage n.txt "$(at '' n.txt '^+new1')" "$(at '' n.txt '^+new1')"
check 'stage a line after an end without newline' "$(printf 'a\nb\nlast\nnew1\n.')" "$(git show :n.txt; echo .)"
git reset -q --hard
printf 'a\nb\nnewlast' > n.txt
"$T" discard n.txt "$(at '' n.txt '^-last')" "$(at '' n.txt '^-last')"
check 'revert a line before an end without newline' "$(printf 'a\nb\nlast\nnewlast.')" "$(cat n.txt; echo .)"
git reset -q --hard

# ---- Odd diffs ----

lno() {
	git diff | grep -n -- "$1" | awk -F: '{print $1 - 1}'
}

sed -i 's/^2$/two/' a.txt
sed -i 's/^103$/three/; s/^118$/eighteen/' q.txt
"$T" stage :1 "$(lno '^-2$')" "$(lno '^+three')"
check 'lines of two files' "$(printf -- '-2\n+two\n-103\n+three')" "$(changes --cached)"
git reset -q --hard
git mv q.txt r.txt
sed -i 's/^103$/three/; s/^118$/eighteen/' r.txt
git add r.txt
"$T" unstage :0 "$(git diff --cached | nth 2)"
check 'unstage hunk of a rename' "$(printf -- 'RM r.txt < q.txt\n-103\n+three')" "$("$T"; changes --cached)"
git reset -q --hard
mv a.txt z.txt
git add -N z.txt
check 'rename in the work tree' ' R z.txt < a.txt' "$("$T")"
git reset -q --hard
rm -f z.txt
git config diff.noprefix true
git config diff.mnemonicPrefix true
sed -i 's/^2$/two/' a.txt
"$T" stage a.txt "$(at '' a.txt '^@@')"
check 'prefix config' "$(printf -- '-2\n+two')" "$(changes --cached)"
git config --unset diff.noprefix
git config --unset diff.mnemonicPrefix
git reset -q --hard
git init -q sub
git -C sub -c user.name=t -c user.email=t@t commit -q --allow-empty -m one
git -c protocol.file.allow=always submodule --quiet add ./sub > /dev/null 2>&1
git commit -qm sub
git -C sub -c user.name=t -c user.email=t@t commit -q --allow-empty -m two
git config diff.submodule log
sed -i 's/^2$/two/' a.txt
"$T" stage :1 "$(git diff --submodule=short | nth 1)" || :
check 'hunk above a submodule' "$(printf -- '-2\n+two')" "$(changes --cached -- a.txt)"
git config --unset diff.submodule
git -C sub reset -q --hard HEAD~1
git reset -q --hard
mv a.txt z.txt
sed -i 's/^2$/two/' z.txt
git add -N z.txt
"$T" stage :1 "$(git diff | nth 1)" || :
check 'stage hunk of a work tree rename' "$(printf -- 'R  z.txt < a.txt\n-2\n+two')" "$("$T" | grep z.txt; changes --cached)"
git reset -q --hard
rm -f z.txt
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'a\nb\nc\nd\n'; done > f.txt
git add f.txt
git commit -qm blocks
{ printf 'w\nx\ny\nz\n'; sed '18s/b/B/' f.txt; } > g.txt
mv g.txt f.txt
"$T" stage f.txt "$(at '' f.txt '^@@' 2)"
check 'stage hunk in repeated text' '@@ -18 +18 @@' "$(git diff --cached -U0 | grep '^@@' | cut -d' ' -f1-4)"
git reset -q
"$T" discard f.txt "$(at '' f.txt '^@@' 2)"
check 'revert hunk in repeated text' '@@ -0,0 +1,4 @@' "$(git diff -U0 | grep '^@@' | cut -d' ' -f1-4)"
git reset -q --hard
printf '*.log\n!keep.log\n' > .gitignore
touch x.tmp
"$T" ignore-sorted x.tmp
check 'sort keeps a block with !' "$(printf '*.log\n!keep.log\n/x.tmp')" "$(cat .gitignore)"
rm .gitignore x.tmp
printf '#!/bin/sh\nsleep 9 &\n' > .git/hooks/post-commit
chmod +x .git/hooks/post-commit
echo x >> a.txt
"$T" add a.txt
S=$(date +%s)
"$T" commit hook
check 'hook with a background job' 1 "$(($(date +%s) - S < 6))"
rm .git/hooks/post-commit
git checkout -q -b side
echo side > u.txt
git add u.txt
git commit -qm side
git checkout -q -
echo main > u.txt
git add u.txt
git commit -qm main
git merge -q side > /dev/null 2>&1 || :
sed -i 's/^2$/two/' a.txt
"$T" stage :1 "$(git diff | nth 2)" || :
check 'stage hunk next to an unmerged file' "$(printf -- '-2\n+two')" "$(changes --cached -- a.txt)"
"$T" unstage :0 "$(git diff --cached | nth 1)" || :
check 'unstage hunk next to an unmerged file' '' "$(changes --cached -- a.txt)"

# ---- Rename staging ----

mkdir ../drivers && cd ../drivers && git init -q && git config user.name t && git config user.email t@t
printf 'one\ntwo\nthree\n' > old.txt
git add old.txt
git commit -qm init
mv old.txt new.txt
git add -N new.txt
"$T" add new.txt
check 'stage whole work tree rename' 'R  new.txt < old.txt' "$("$T")"

# ---- Text conversion ----

git add -A
git commit -qm rename
printf '*.txt diff=upper\n' > .gitattributes
git config diff.upper.textconv 'tr a-z A-Z <'
printf 'four\n' >> new.txt
"$T" stage new.txt 4 || :
check 'stage hunk with textconv' "$(printf 'one\ntwo\nthree\nfour')" "$(git show :new.txt)"
git add new.txt
"$T" unstage new.txt 4 || :
check 'unstage hunk with textconv' "$(printf 'one\ntwo\nthree')" "$(git show :new.txt)"
git reset -q HEAD -- new.txt
"$T" discard new.txt 4 || :
check 'revert hunk with textconv' "$(printf 'one\ntwo\nthree')" "$(cat new.txt)"

# ---- Alternate index ----

mkdir ../index && cd ../index && git init -q && git config user.name t && git config user.email t@t
echo tracked > tracked.txt
git add tracked.txt
git commit -qm init
cp .git/index .git/alternate
echo untracked > new.txt
GIT_INDEX_FILE="$PWD/.git/alternate" "$T" stage new.txt 5
check 'untracked hunk uses alternate index' 'new.txt' "$(GIT_INDEX_FILE="$PWD/.git/alternate" git diff --cached --name-only)"
check 'untracked diff leaves default index alone' '' "$(git diff --cached --name-only)"

# ---- Stat ----

mkdir ../stat && cd ../stat && git init -q && git config user.name t && git config user.email t@t
NOW=$(date -ud 2026-01-31T12:00 +%s)

by() {
	git add . && GIT_AUTHOR_DATE="$2 +0000" GIT_COMMITTER_DATE="$2 +0000" git commit -q --author="$1 <$1@t>" -m "$1 $2"
}

stat() {
	TZ=UTC "$T" stat "$@" "$NOW" | sed 's/ *$//'
}

echo 1 > old.txt
by Ann 2025-06-01T10:00:00
echo all: > Makefile
by Bob 2026-01-10T09:00:00
mkdir src && seq 3 > src/a.c && seq 2 > b.txt
by Ann 2026-01-30T09:00:00
sed -i 's/^2$/two/' src/a.c
by Ann 2026-01-30T15:00:00
check 'stat last month' 'Commits: 3 +7-1' "$(stat 30 | head -1)"
check 'stat last year' 'Commits: 4 +8-1' "$(stat 365 | head -1)"
check 'stat all' 'Commits: 4 +8-1' "$(stat 0 | head -1)"
check 'stat empty' 'Commits: 0' "$("$T" stat 1 $((NOW + 864000)) | head -1)"
check 'stat path' 'Commits: 2 +4-1' "$(TZ=UTC "$T" stat 30 "$NOW" src/ | head -1)"
check 'stat authors' "$(printf '     2  66.7%%  Ann <Ann@t> +6-1\n     1  33.3%%  Bob <Bob@t> +1')" "$(stat 30 | sed -n '3,4p')"
check 'stat extension' '     5  62.5%  c +4-1' "$(stat 30 | sed -n '/Lines changed/{n;n;p;}')"
check 'stat extensionless file' '     1  12.5%  (no extension) +1' "$(stat 30 | grep '(no extension)')"
check 'stat frequent file' '     2  66.7%  src/a.c +4-1' "$(stat 30 | sed -n '/Most frequently/{n;n;p;}')"
ODD=$(printf 'src/new\nname\t.js')
echo odd > "$ODD"
by Ann 2026-01-31T10:30:00
check 'stat unusual filename' '     1  25.0%  src/new\nname\t.js +1' "$(stat 30 | grep 'src/new')"
check 'stat unusual path filter' 'Commits: 1 +1' "$(TZ=UTC "$T" stat 30 "$NOW" "$ODD" | head -1)"
# An author is a name and an email: one name with two emails is two authors
git commit -q --allow-empty --author='Ann <another@t>' -m names
check 'stat author email identity' 2 "$(stat 0 | grep -c 'Ann <')"

printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]

