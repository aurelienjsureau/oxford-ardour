# Windows build kit — Oxford-Ardour

Sauvegarde des fichiers de build qui vivaient a cote du repo dans `D:\Oxford` et
`Downloads\Oxford`.

- `oxford-build.sh` / `oxford-configure.sh` : recette de build MSYS2 (a executer depuis
  le dossier PARENT du checkout, layout d'origine `D:\Oxford\Ardour-9.7.0\`).
- `oxford_installer.nsi` : script installeur NSIS Windows.
- `enable/disable-pageheap.bat` : outils de debug du crash IME (fix ImmDisableIME dans main.cc).
- `OxfordDSP/` : version standalone des headers DSP + programmes de test (bench, sweep, etc.).
  Attention : `OxfordBus.h` differe de la version integree dans `libs/ardour/ardour/oxford/` —
  la version du repo est celle qui a ete shippee.
- `reference/` : doc OXF-R3, photos console, visuels Sonnox.

Les installeurs (Oxford-9.7.0-Setup.exe, dmg mac) sont en assets de la release `archives`.
