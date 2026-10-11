for dir in ./; do
    if [[ -d "$dir" ]]; then
        echo "Cleaning: $dir"
        rm -rf -- "$dir"
    fi
done

#!/bin/bash
cd "$(dirname "$0")" || exit 1
source ../git.sh
[ -n "$GH_TOKEN" ] || { echo "no GH_TOKEN"; exit 1; }

REPO="github.com/underdebug/2608B-TestCode.git"
BRANCH="master"
URL="https://${GH_USER}:${GH_TOKEN}@${REPO}"
MSG="${1:-update $(date '+%F %H:%M')}"

# Initialize repo on first run only
[ -d .git ] || {
    git init
    git config user.name "$GH_USER"
    git config user.email "mingjie2026@gmail.com"
}

# Point HEAD at the branch without touching any commits
# (safer than "checkout -B", which resets the branch pointer)
git symbolic-ref HEAD "refs/heads/$BRANCH"

# Pull remote content first, so "add -A" won't mark
# remote-only files as deleted. Silenced for empty repos.
git fetch "$URL" "$BRANCH" 2>/dev/null && \
    git merge FETCH_HEAD --no-edit --allow-unrelated-histories

git add -A
git commit -m "$MSG" || echo "no changes"

# No -f: git will reject the push if the remote has
# commits we don't have locally, instead of overwriting them
git push "$URL" "$BRANCH"
