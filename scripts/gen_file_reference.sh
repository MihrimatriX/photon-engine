#!/usr/bin/env bash
# gen_file_reference.sh — docs/file-reference.md'yi kaynak dosyaların başlık
# yorumlarından üretir. Başlık = dosyanın en üstündeki ardışık "//" satırları.
#   bash scripts/gen_file_reference.sh
set -euo pipefail
cd "$(dirname "$0")/.."
out=docs/file-reference.md
{
  echo "# Dosya başvurusu"
  echo
  echo "Her kaynak dosyanın başındaki açıklama yorumundan üretildi (\`scripts/gen_file_reference.sh\`)."
  echo "Ayrıntı için dosyanın kendisine bakın."
  find src -name '*.h' -o -name '*.cpp' | sort | while read -r f; do
    echo
    echo "### \`$f\`"
    echo
    tr -d '\r' < "$f" | awk '/^\/\//{ sub(/^\/\/ ?/, ""); print; next } { exit }'
  done
} > "$out"
echo "$out: $(grep -c '^### ' "$out") dosya"
