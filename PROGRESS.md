# PROGRESS – AutoNeg 1.0

Arbeitsgedächtnis zwischen Sitzungen. **Zu Beginn jeder Sitzung zuerst lesen**, nach jedem Arbeitsschritt aktualisieren.
Wiederkehrende Entwicklungsprobleme stehen in `BUGS.md`, Abweichungen von der Planung in `../Planning/abweichungen_planung.txt`.

Stand: 22.10.2026 – **M2 fertig unter Windows und Linux, wartet auf OK des Nutzers.** Offen zur Entscheidung: T-07 (Toleranz exp10).

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
| M0 | Projektgerüst, Presets, vcpkg, Architekturprüfung | Fertig, OK am 19.10.2026 | – (kein Szenario) |
| M1 | base, model, image, ports | Fertig, OK am 22.10.2026 | U-MOD-01…13, U-IMG-01…04 (alle grün) |
| M2 | Pipeline, CPU-Backend | **Fertig (Windows + Linux), wartet auf OK** | U-PIP-01…18, U-BCK-01…06 (alle grün; U-BCK-01 mit Toleranz nach T-07) |
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

### M1 – Ergebnis

Umgesetzt (Architektur 9.1, 9.2, 9.9–9.11 mit A-01…A-07; Abweichungen K-M1-01…14 in `abweichungen_planung.txt`):
- `autoneg::base`: `error.hpp` (Error, ErrorCode, Expected, makeError), `hash.hpp` (Hasher), `geometry.hpp` (Vec3, Point2, Size, Mat3, Mat2x3), `bytes.hpp`.
- `autoneg::model`: `ids.hpp` (FrameId, RollId, StepId, UndoGroupId), `film.hpp` (FilmType, Rating, Origin, SourceFormat, FilmInfo, SourceInfo), `values.hpp` (NormPoint, NormRect, Curve, Spot, SpotList), `param_set.hpp` (ParamValue, ParamSet), `edit_entry.hpp`, `edit_list.hpp`, `edit_history.hpp` (Revision, EditHistory), `roll.hpp` (Frame, Roll), `json_value.hpp`.
- `autoneg::image`: `image_spec.hpp` (PixelFormat, ColorSpace, MemoryLocation, ImageSpec, Roi), `storage.hpp` (IStorage, AlignedAllocator, HostStorage, DeviceStorage), `image_buffer.hpp` (ImageBuffer, ImageView), `tile_grid.hpp` (Tile, TileGrid), `cancel_token.hpp`, `pixel_program.hpp` (Lut1D, Lut3D, PixelOp, PixelProgram), `warp_plan.hpp`, `histogram.hpp`.
- `autoneg::ports` (nur Schnittstellen): `raw_decoder.hpp`, `image_io.hpp`, `color.hpp`, `compute_backend.hpp` (KernelContext, IComputeBackend), `vision.hpp`, `repositories.hpp` (SourceStamp, SidecarData, SidecarRef, IRollRepository, IEditRepository, IPresetRepository, ISettingsStore, ICacheStore), `services.hpp` (IContentHasher, ILogger), `task_executor.hpp` (Priority, ITaskExecutor), `inference.hpp`, `film_profile_source.hpp`. `IDocumentRenderer` folgt in M7 (K-M1-13).
- Tests: `tests/mocks` (gmock-Attrappen aller Ports, Ziel `autoneg_test_mocks`), `tests/support` (Testhilfen, Ziel `autoneg_test_support`, Ränge der Stufenfolge), Unit-Tests in `tests/unit/{base,model,image,ports}`.
- Architekturprüfung: `autoneg/mocks/` und `autoneg/testing/` nur in `tests/` erlaubt (Selbsttest 16 Fälle).

Szenarien (alle grün auf beiden Systemen): U-MOD-01, -02, -03, -04, -05, -06, -07, -08, -09, -10, -11, -12, -13; U-IMG-01, -02, -03, -04.
Zusätzliche Tests ohne Szenario (Präfix `M1_`): 13 (base, ParamSet, Roll, JsonValue, ImageBuffer, CancelToken, Ports).

Nachweise (19.10.2026): `windows-clang` Debug + Release, `linux-clang`, `linux-gcc`, `linux-clang-asan`, `linux-clang-tsan` – je 37/37 Tests grün, keine Warnungen; `clang-format --dry-run --Werror` sauber. Die festgelegte Prüfsumme in `Hasher.M1_DeterministicAndSensitive` ist auf Windows und Linux identisch.

Offen aus M1:
- Platzhalter-Implementierungen für `IInferenceRuntime` und `IFilmProfileSource` (NF-ARC-15 „vorhanden und getestet“) → M6.
- `IDocumentRenderer` → M7.

### M2 – Ergebnis

- OE-01 entschieden: Highway + oneTBB (D-13). Benchmark-Prototyp: `prototypes/oe01-benchmark/` (ERGEBNIS.md, Rohdaten Windows/Linux).
- `autoneg::pipeline`: `step.hpp` (StepDescriptor, ParamSpec, FilmTypeMask, IStep, ControlStep/PointwiseStep/GeometricStep/RegionStep, StepContext, ExecContext), `pixel_program_builder.hpp`, `warp_builder.hpp`, `step_registry.hpp`, `render.hpp` (RenderMode, RenderProfile, RenderRequest), `compiled_pipeline.hpp` (Stage, PixelPass, WarpPass, RegionPass, OutputPass, CompiledPipeline), `pipeline_compiler.hpp`, `tile_planner.hpp`, `stage_cache.hpp`, `source_provider.hpp`, `pipeline_executor.hpp`, `backend_selector.hpp`.
- `autoneg::backend::cpu`: `CpuBackend` (Highway-Zeilenkernels in `src/kernels_hwy.cpp` für Punktprogramm, Multiplikation, Gauß, Schärfen, Lichter/Tiefen; Verzerrung, Skalierung, Rauschminderung, Retusche nur parallelisiert; Histogramm mit fester Blockeinteilung). Alle Highway-Ziele inkl. EMU128 mitkompiliert.
- `image::ImageBuffer::copyRegion` für die Kachelausführung.
- Tests: `tests/reference` (skalare Referenzkernels = Definition, `ReferenceBackend` mit Aufrufzähler und Abbruch-Haken), `tests/support/test_steps.hpp` (Test-Schritte nach Architektur 5.2/Tabelle 1), `tests/unit/pipeline`, `tests/unit/backend_cpu`.
- TSan-Preset: eigenes vcpkg-Triplet `cmake/triplets/x64-linux-tsan.cmake` (Abhängigkeiten mit TSan und clang), Testprogramme auf 8 CPUs begrenzt (B-12).

Szenarien (grün auf allen 6 Konfigurationen): U-PIP-01…18, U-BCK-01…06. Zusätzlich 11 Tests mit Präfix `M2_`.
Nachweise (22.10.2026): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan, linux-clang-tsan je 70/70, keine Warnungen; clang-format sauber.

Gefundene und behobene Fehler: Cache-Markierung ohne useCache (Compiler), Sicht auf temporären Puffer (Test, B-10), Testmatrizen der 90°-Drehung (Test), Include-Reihenfolge Highway (B-11), Scheinrennen TSan/oneTBB (B-12).

Offen aus M2:
- **T-07**: U-BCK-01 fordert 1e-6; Ketten mit exp10 erreichen 1,6e-6 (Fehlerfortpflanzung). Test prüft linear/log10 mit 1e-6, exp10-Ketten mit 2e-6 – Entscheidung des Nutzers.
- T-01 (U-PIP-05 nach Architektur geprüft), T-08 (monotone Kante weich), T-09 (Proxy im Cache): zur Einarbeitung.
- Parallele Kacheln (Ebene 2) mit dem JobScheduler in M6; SIMD für Verzerrung/Skalierung/Rauschminderung/Retusche mit der Leistungsmessung in M9.

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

| D-13 | 22.10.2026 | OE-01 entschieden: Google Highway + oneTBB |
| A-09…A-12 | 19.10.2026 | genehmigt: ui → Kernmodule, Adapterregeln (TBB in adapter_tbb und backend_cpu, OS-Header nur in Adaptern), Drittbibliotheken unter Windows mit vcpkg-Standard-Toolchain, statische Module |
| K-M1-01…14 | 19.10.2026 | Abweichungen von den Klassendiagrammen in M1 (Details in `abweichungen_planung.txt`, Abschnitt G) |

## Offene Fragen an den Nutzer

1. Einordnung der Testordner, Annotationen (L-06) – spätestens für M3/M5.
2. T-07: Toleranz für Punktprogramme mit exp10 (Vorschlag 2e-6).

## Plattformabhängiger Code (auf dem anderen System prüfen)

- `cmake/triplets/*` und `tests/CMakeLists.txt` (taskset): nur Linux/TSan.
- `src/backends/cpu/src/kernels_hwy.cpp`: Highway-Zielauswahl zur Laufzeit; auf beiden Systemen AVX2 + EMU128 getestet (Rechner ohne AVX-512).
- `src/pipeline/src/source_provider.cpp`: std::shared_future / Threads (U-PIP-16 auf beiden Systemen grün).

- `cmake/AutoNegWarnings.cmake`: getrennte Warnungs-Zweige für clang-cl und clang/GCC – nach jeder Änderung beide Systeme bauen (beide am 19.10.2026 grün).
- `cmake/AutoNegTest.cmake`: gtest wird unter Windows als DLL, unter Linux statisch gelinkt; unterschiedliches Verhalten siehe B-07.
- `src/image/include/autoneg/image/storage.hpp`: `AlignedAllocator` nutzt `operator new(size, align_val_t)` – auf beiden Systemen getestet (U-IMG-03).
- `src/base/src/hash.cpp`: plattformunabhängige Prüfsumme (feste Bytereihenfolge); Referenzwert im Test auf beiden Systemen gleich.
- `src/model/include/autoneg/model/roll.hpp`: `Frame::path`/`Roll::folder` sind `std::filesystem::path` – Pfadtrennzeichen und Kodierung (Umlaute, z. B. „Marius Götz“) ab M4/M6 auf beiden Systemen prüfen.
