#!/bin/bash
set -e
cd "$(dirname "$0")" || exit 1
source ../git.sh
[ -n "$GH_TOKEN" ] || { echo "no GH_TOKEN"; exit 1; }
[ -n "$GH_USER" ] || { echo "no GH_USER"; exit 1; }

REPO="github.com/underdebug/2608B-TestCode.git"
BRANCH="master"
URL="https://${GH_USER}:${GH_TOKEN}@${REPO}"
MSG="${1:-update $(date '+%F %H:%M')}"

# Initialize repo on first run only
if [ ! -d .git ]; then
    git init
    git config user.name "$GH_USER"
    git config user.email "mingjie2026@gmail.com"
fi

# Point HEAD at the branch without touching any commits
git symbolic-ref HEAD "refs/heads/$BRANCH"

# Retrieve remote changes before staging local files
if git fetch "$URL" "$BRANCH"; then
    git merge FETCH_HEAD --no-edit --allow-unrelated-histories
else
    echo "Fetch failed. Upload canceled to avoid overwriting remote history."
    exit 1
fi

# Stage source changes, excluding local build and Python cache directories.
git add -A -- . ':(exclude)build/**' ':(exclude,glob)**/build/**' ':(exclude)__pycache__/**' ':(exclude,glob)**/__pycache__/**'

if ! git diff --cached --quiet; then
    git commit -m "$MSG"
else
    echo "no changes"
fi

# Only clean local build directories after a successful push.
git push "$URL" "$BRANCH"

echo "Cleaning local build and __pycache__ directories..."
find . -type d \( -name build -o -name __pycache__ \) -not -path './.git/*' -prune -print -exec rm -rf -- {} +
echo "Upload complete; local build and __pycache__ directories cleaned."
