#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"
output="$project_dir/web_assets_data.h"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

{
  echo '#pragma once'
  echo '#include <Arduino.h>'
  echo '// Generado por generate_web_assets.sh; no editar manualmente.'
  echo
  for page in index config calibracion; do
    gzip -c -n -9 "$project_dir/data/$page.html" > "$tmp_dir/$page.html.gz"
    xxd -i -n "web_${page}_gz" "$tmp_dir/$page.html.gz" |
      sed "s/^unsigned char web_${page}_gz\[\]/const uint8_t web_${page}_gz[] PROGMEM/;s/^unsigned int web_${page}_gz_len/const size_t web_${page}_gz_len/"
    echo
  done
} > "$output"

echo "Generado $output"
