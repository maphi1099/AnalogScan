# PROGRESS – AutoNeg 1.0

Arbeitsgedächtnis zwischen Sitzungen. **Zu Beginn jeder Sitzung zuerst lesen**, nach jedem Arbeitsschritt aktualisieren.
Wiederkehrende Entwicklungsprobleme stehen in `BUGS.md`, Abweichungen von der Planung in `../Planning/abweichungen_planung.txt`.

Stand: 19.10.2026 – **M0 fertig unter Windows und Linux, wartet auf OK des Nutzers.**

## Ablage

| Ort | Inhalt |
|---|---|
| `D:\Development\AutoNeg\AnalogScan\` | Git-Repository (Remote `github.com/maphi1099/AnalogScan`, Zweig `main`, Autor Max Philipp): gesamter Quellcode, `PROGRESS.md`, `BUGS.md` |
| `..\Planning\` | `lastenheft_autoneg.pdf` (0.5), `architektur_autoneg.pdf` (0.3), `abweichungen_planung.txt` |
| `..\Testing\` | `testszenarien_autoneg.xlsx` (126 U, 20 K, 50 E), `Data\` (nur lesen) |
| `..\CLAUDE.md` | Projektregeln (liegt außerhalb des Repositorys) |

### Testdaten (Stand 19.10.2026)

- 448 Sony-ARW einer **ILCE-7RM4** (61 MP, je ca. 62 MB), alle Kleinbild mit sichtbarer Perforation.
- 1 Fujifilm-RAF: `Fuji RAW/MSCF0358.RAF`, **X-E5** (X-Trans), 35 MB. Ggf. folgen TIFF.
- Keine Soll-Annotationen, keine Hasselblad- und Fremdhersteller-Dateien, kein Mittelformat, keine defekten Dateien, keine erkennbare Leeraufnahme.

Vorläufige Einordnung nach dem eingebetteten Vorschaubild des jeweils ersten Scans (vom Nutzer zu bestätigen):

| Ordner | Dateien | Vermutung |
|---|---|---|
| `Testing/Data/*.ARW` (DSC08009–08082) | 64 | Dia |
| `100MSDCF` | 43 | Dia |
| `100MSDCF-2` | 37 | S/W-Negativ |
| `101MSDCF` | 29 | Dia (oder Positiv) |
| `101MSDCF-2` | 52 | S/W-Negativ |
| `102MSDCF`, `102MSDCF-2`, `103MSDCF` | 33 / 36 / 43 | Farbnegativ |
| `Ali` | 36 | S/W-Negativ |
| `Ying` | 36 | S/W-Negativ |
| `Marius Götz` | 39 | unklar (erstes Bild unscharfe Leuchtfläche) |
| `Fuji RAW` | 1 | noch nicht angesehen |

### Arbeitsumgebung

- Windows 11: CMake 4.4.4, Ninja, LLVM/clang-cl 23.1.3, Build Tools 2022 mit MSVC 14.44.35207 (nur STL/Linker-Umgebung) und Windows SDK 10.0.26100.0, vcpkg `D:\vcpkg` (Commit `664933b8…`, 18.10.2026, `VCPKG_ROOT` als Benutzervariable), Python 3.12. clang-cl braucht die VS-Umgebung (B-01).
- WSL 2 Arch Linux (Benutzer root): clang 23.1.3, GCC 16.1.1, CMake 4.4.4, Ninja 1.13.2, Python 3.14.8, vcpkg `/root/vcpkg` (gleicher Commit, `VCPKG_ROOT` in `/root/.profile`). Aufruf aus Git-Bash siehe B-05.

## Meilensteine

| MS | Inhalt | Status | Umgesetzte Szenarien |
|---|---|---|---|
| M0 | Projektgerüst, Presets, vcpkg, Architekturprüfung | **Fertig (Windows + Linux), wartet auf OK** | – (kein Szenario; Beispieltest `Scaffold.M0_ModelModuleIsLinked`) |
| M1 | base, model, image, ports | Offen | – |
| M2 | Pipeline, CPU-Backend (vorher OE-01-Benchmark) | Offen | – |
| M3 | Verarbeitungsschritte, Multigrade, Negativ-Generator | Offen | – |
| M4 | LibRaw, OIIO, LCMS, Vertragstests | Offen | – |
| M5 | Analyse, K-11 | Offen | – |
| M6 | Datenhaltung und Anwendung | Offen | – |
| M7 | Kontaktabzug | Offen | – |
| M8 | Oberfläche, E2E | Offen | – |
| M9 | CLI, Leistung, Abschluss | Offen | – |

### M0 – Ergebnis

Dateien:
- `CMakeLists.txt` (C++23, nur Clang/GCC zugelassen, Optionen `AUTONEG_BUILD_TESTS`, `AUTONEG_CHECK_ARCHITECTURE`, `AUTONEG_ARCH_VIOLATION_DEMO`, `AUTONEG_SANITIZER`, `AUTONEG_TESTDATA_DIR`)
- `CMakePresets.json`: `windows-clang`, `linux-clang`, `linux-gcc`, `linux-clang-asan`, `linux-clang-tsan`; Build-/Test-Presets `<preset>-debug`, `-release` (Sanitizer nur Debug). Ninja Multi-Config, Ausgabe `build/<preset>`.
- `vcpkg.json`: Baseline `664933b8d385dfc01f4e0cdff4c7c74e71f6a740`; Standard nur `gtest`; Features `raw`, `imageio`, `color`, `vision`, `threading`, `persistence`, `hashing`, `logging`, `gui` (noch nicht aktiviert).
- `cmake/AutoNegModule.cmake` (`autoneg_add_module`, `autoneg_add_program`, `autoneg_finalize_architecture`), `cmake/AutoNegWarnings.cmake` (Warnungen als Fehler, Sanitizer), `cmake/AutoNegTest.cmake` (`autoneg_add_test` mit Labels unit/component/e2e; linkt nur `GTest::gmock_main`; zusätzlicher CTest `<ziel>.linked` gegen still verschwundene Tests, B-07).
- 20 Module als statische Ziele mit Platzhalter `moduleName()`: `base`, `model`, `image`, `layout`, `ports`, `pipeline`, `steps`, `analysis`, `application`, `ui`, `adapter_{libraw,oiio,lcms,opencv,json,xxhash,qtprint,spdlog,tbb}`, `backend_cpu`. Kein Vulkan-Ziel.
- Programme `autoneg` (leer) und `autoneg-cli` (`--version`).
- `tools/check_architecture.py` + `tools/architecture_rules.json`: erlaubte direkte Abhängigkeiten, Fremdbibliotheken je Modul, Zyklen, Includes (Projekt-Header nur aus erlaubten Modulen, Fremd-/Qt-/OS-Header nur im zugehörigen Adapter, unbekannte Header abgelehnt). Läuft bei jedem Build (Ziel `check_architecture`) und als CTest `architecture.check`.
- `tests/architecture/test_check_architecture.py`: 15 Selbsttests mit absichtlichen Verstößen (CTest `architecture.selftest`).
- `tests/unit/model/scaffold_test.cpp`: Beispieltest.
- `.gitignore`, `.gitattributes`, `.clang-format`, `.clang-tidy`, `.vscode/extensions.json`, `.vscode/settings.json`.

Nachweise (19.10.2026):
- Windows: `windows-clang` Debug und Release bauen ohne Warnungen; je 4/4 Tests grün (`unit` 2, `arch` 2).
- Linux (WSL Arch): `linux-clang`, `linux-gcc`, `linux-clang-asan`, `linux-clang-tsan` Debug (clang auch Release): ohne Warnungen; je 4/4 grün.
- Absichtlicher Verstoß (`-DAUTONEG_ARCH_VIOLATION_DEMO=ON`, model → ports) auf beiden Systemen: Build bricht ab mit „[Abhängigkeit] model -> ports ist nicht erlaubt“ und „[Zyklus] ports -> layout -> model -> ports“.
- `clang-format --dry-run --Werror` auf allen Quellen: sauber.
- Gefundener Fehler B-07 (unter Windows verschwanden die gtest-Tests still): zuerst mit `autoneg_test_model.linked` reproduziert, dann behoben.

## Entscheidungen

| Nr | Datum | Entscheidung |
|---|---|---|
| D-01 | 19.10.2026 | C++23 statt C++20; kein `std::flat_map` |
| D-02 | 19.10.2026 | Nur clang (Windows clang-cl, Linux clang); GCC als zusätzlicher Build; Linux-Kompatibilität ist Hauptanforderung |
| D-03 | 19.10.2026 | GPU/Vulkan erst in 1.1; in 1.0 nur Backend-Abstraktion, Tests mit Attrappen-Backend |
| D-04 | 19.10.2026 | KI-Inferenz in 1.0 nur Schnittstelle (bestätigt) |
| D-05 | 19.10.2026 | Quellcode im Repository `AnalogScan/` |
| D-06 | 19.10.2026 | Python-Testwerkzeuge immer zulässig |
| D-07/D-10 | 19.10.2026 | Fujifilm-RAW vorhanden (X-E5), TIFF folgt ggf. |
| D-08 | 19.10.2026 | Keine CI; Prüfungen lokal (Build + CTest, Linux in WSL) |
| D-09 | 19.10.2026 | Keine Lizenz-/Copyright-Kopfzeile |
| D-11 | 19.10.2026 | Neues Modul `autoneg_base` (Error, Expected, Vektortypen) |
| D-12 | 19.10.2026 | Neuer Adapter `autoneg_adapter_tbb` mit Port `ITaskExecutor` |
| A-01…A-07 | 19.10.2026 | Architekturanpassungen genehmigt (Wertetypen nach image, TBB hinter Port, eigener JsonValue, Basismodul, steps→ports, ports→layout, Ergänzung fehlender Typen) |

Vorgeschlagen, Zustimmung offen: A-09 (ui → Kernmodule), A-10 (Adapterregeln, TBB in adapter_tbb und backend_cpu, OS-Header nur in Adaptern), A-11 (Drittbibliotheken unter Windows mit vcpkg-Standard-Toolchain), A-12 (statische Module). In `architecture_rules.json` bereits so umgesetzt.

## Offene Fragen an den Nutzer

1. Zustimmung zu A-09 bis A-12.
2. Einordnung der Testordner, Annotationen (L-06).

## Plattformabhängiger Code (auf dem anderen System prüfen)

- `cmake/AutoNegWarnings.cmake`: getrennte Warnungs-Zweige für clang-cl und clang/GCC – nach jeder Änderung beide Systeme bauen (beide am 19.10.2026 grün).
- `cmake/AutoNegTest.cmake`: gtest wird unter Windows als DLL, unter Linux statisch gelinkt; unterschiedliches Verhalten siehe B-07.
