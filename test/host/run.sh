#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
output_dir=${TMPDIR:-/tmp}/codex-buddy-host-tests
cxx=${CXX:-c++}
cxxflags="-std=c++20 -Wall -Wextra -Werror -pedantic"

mkdir -p "$output_dir"

$cxx $cxxflags \
  -I"$project_dir/components/codex_protocol/include" \
  "$project_dir/components/codex_protocol/codex_protocol.cpp" \
  "$test_dir/test_codex_protocol.cpp" \
  -o "$output_dir/test_codex_protocol"

$cxx $cxxflags \
  -I"$project_dir/components/codex_model/include" \
  "$project_dir/components/codex_model/codex_model.cpp" \
  "$test_dir/test_codex_model.cpp" \
  -o "$output_dir/test_codex_model"

$cxx $cxxflags \
  -I"$project_dir/components/claude_model/include" \
  "$project_dir/components/claude_model/claude_model.cpp" \
  "$test_dir/test_claude_model.cpp" \
  -o "$output_dir/test_claude_model"

$cxx $cxxflags \
  -I"$project_dir/components/claude_protocol/include" \
  -I"$project_dir/components/claude_model/include" \
  "$project_dir/components/claude_protocol/claude_protocol.cpp" \
  "$project_dir/components/claude_model/claude_model.cpp" \
  "$test_dir/test_claude_protocol.cpp" \
  -o "$output_dir/test_claude_protocol"

$cxx $cxxflags \
  -I"$project_dir/components/buddy_ui/include" \
  -I"$project_dir/components/claude_model/include" \
  "$project_dir/components/buddy_ui/buddy_ui.cpp" \
  "$project_dir/components/claude_model/claude_model.cpp" \
  "$test_dir/test_buddy_ui.cpp" \
  -o "$output_dir/test_buddy_ui"

$cxx $cxxflags \
  -I"$project_dir/components/motion_detector/include" \
  "$project_dir/components/motion_detector/motion_detector.cpp" \
  "$test_dir/test_motion_detector.cpp" \
  -o "$output_dir/test_motion_detector"

$cxx $cxxflags \
  -I"$project_dir/components/codex_input/include" \
  -I"$project_dir/components/codex_model/include" \
  -I"$project_dir/components/codex_protocol/include" \
  "$project_dir/components/codex_input/codex_input.cpp" \
  "$project_dir/components/codex_model/codex_model.cpp" \
  "$test_dir/test_codex_input.cpp" \
  -o "$output_dir/test_codex_input"

$cxx $cxxflags \
  -I"$project_dir/components/codex_rpc/include" \
  -I"$project_dir/components/codex_protocol/include" \
  -I"$project_dir/components/codex_model/include" \
  "$project_dir/components/codex_rpc/codex_rpc.cpp" \
  "$project_dir/components/codex_model/codex_model.cpp" \
  "$test_dir/test_codex_rpc.cpp" \
  -lm \
  -o "$output_dir/test_codex_rpc"

$cxx $cxxflags \
  -I"$project_dir/components/codex_controller/include" \
  -I"$project_dir/components/codex_input/include" \
  -I"$project_dir/components/codex_model/include" \
  -I"$project_dir/components/codex_protocol/include" \
  "$project_dir/components/codex_controller/codex_controller.cpp" \
  "$project_dir/components/codex_model/codex_model.cpp" \
  "$project_dir/components/codex_protocol/codex_protocol.cpp" \
  "$test_dir/test_codex_controller.cpp" \
  -o "$output_dir/test_codex_controller"

$cxx $cxxflags \
  -I"$project_dir/components/codex_ui/include" \
  -I"$project_dir/components/codex_input/include" \
  -I"$project_dir/components/codex_model/include" \
  -I"$project_dir/components/codex_protocol/include" \
  "$project_dir/components/codex_ui/codex_ui.cpp" \
  "$project_dir/components/codex_model/codex_model.cpp" \
  "$test_dir/test_codex_ui.cpp" \
  -o "$output_dir/test_codex_ui"

"$output_dir/test_codex_protocol"
"$output_dir/test_codex_model"
"$output_dir/test_claude_model"
"$output_dir/test_claude_protocol"
"$output_dir/test_buddy_ui"
"$output_dir/test_motion_detector"
"$output_dir/test_codex_input"
"$output_dir/test_codex_rpc"
"$output_dir/test_codex_controller"
"$output_dir/test_codex_ui"
