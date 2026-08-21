#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/codex-buddy-previews.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cmake -S "$project_dir/test/host/ui_preview" -B "$work_dir/build" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$work_dir/build" --target render_buddy_ui --parallel
"$work_dir/build/render_buddy_ui" "$work_dir"

for source in "$work_dir"/*.ppm; do
    name=$(basename "$source" .ppm)
    sips -s format png "$source" \
        --out "$project_dir/docs/images/$name.png" >/dev/null
done
