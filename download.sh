#!/bin/bash

REPO="https://github.com/mingjie2026/2608B-TestCode.git"
NAME="2608B-TestCode"

if [[ -n "$1" ]]; then
    NAME="${NAME}${1}"
fi

git clone --recursive "$REPO" "$NAME"
