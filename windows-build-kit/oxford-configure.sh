#!/usr/bin/env bash
cd /d/Oxford/Ardour-9.7.0 || exit 2
echo "=== waf configure (fork OXFORD, cible mingw) ==="
python3 ./waf configure --check-c-compiler=gcc --check-cxx-compiler=g++ \
  --dist-target=mingw --prefix=/d/Oxford/install \
  --program-name=Oxford \
  --configdir=share --no-execstack --no-dr-mingw --optimize --cxx17 --ptformat
echo "CONFIGURE_RC=$?"
