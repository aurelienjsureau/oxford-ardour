#!/usr/bin/env bash
cd /d/Oxford/Ardour-9.7.0 || exit 2
echo "=== waf build (fork OXFORD, mingw) ==="
python3 ./waf -j"$(nproc)"
BRC=$?
echo "BUILD_RC=$BRC"
if [ "$BRC" = "0" ]; then
  echo "=== waf install -> D:/Oxford/install ==="
  python3 ./waf install
  echo "INSTALL_RC=$?"
fi
