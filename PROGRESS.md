# PROGRESS – AutoNeg 1.0

Arbeitsgedächtnis zwischen Sitzungen. **Zu Beginn jeder Sitzung zuerst lesen**, nach jedem Arbeitsschritt aktualisieren.
Wiederkehrende Entwicklungsprobleme stehen in `BUGS.md`, Abweichungen von der Planung in `../Planning/abweichungen_planung.txt`.

Stand: 23.10.2026 – **M7, M8 und M9 fertig (autonom, D-15); Version 1.0 bis auf die offenen Punkte unten umgesetzt.** Wartet auf Nutzer: Bestätigung von D-15, D-16, D-17 und der K-M7…K-M9/T-16/T-17-Einträge, Annotationen für K-11 (L-06/L-07), OE-03 (Lizenz), Zustimmung zu K-M3…K-M6, T-10…T-15 (siehe `abweichungen_planung.txt`).

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
| `101MSDCF` | 29 | Dia (Sichtprüfung M3) |
| `101MSDCF-2` | 52 | gemischt: S/W-Negativ (DSC07505) und Farbnegativ (DSC07531) |
| `102MSDCF`, `102MSDCF-2`, `103MSDCF` | 33 / 36 / 43 | Farbnegativ |
| `Ali` | 36 | S/W-Negativ |
| `Ying` | 36 | S/W-Negativ |
| `Marius Götz` | 39 | S/W-Negativ (erstes Bild unscharfe Leuchtfläche) |
| `Fuji RAW` | 1 | S/W-Negativ |

### Arbeitsumgebung

- Windows 11: CMake 4.4.4, Ninja, LLVM/clang-cl 23.1.3, Build Tools 2022 mit MSVC 14.44.35207 (nur STL/Linker-Umgebung) und Windows SDK 10.0.26100.0, vcpkg `D:\vcpkg` (Commit `664933b8…`, 18.10.2026, `VCPKG_ROOT` als Benutzervariable), Python 3.12. clang-cl braucht die VS-Umgebung (B-01).
- WSL 2 Arch Linux (Benutzer root): clang 23.1.3, GCC 16.1.1, CMake 4.4.4, Ninja 1.13.2, Python 3.14.8, vcpkg `/root/vcpkg` (gleicher Commit, `VCPKG_ROOT` in `/root/.profile`). Aufruf aus Git-Bash siehe B-05.

## Meilensteine

| MS | Inhalt | Status | Umgesetzte Szenarien |
|---|---|---|---|
| M0 | Projektgerüst, Presets, vcpkg, Architekturprüfung | Fertig, OK am 19.10.2026 | – (kein Szenario) |
| M1 | base, model, image, ports | Fertig, OK am 22.10.2026 | U-MOD-01…13, U-IMG-01…04 (alle grün) |
| M2 | Pipeline, CPU-Backend | Fertig, OK am 22.10.2026 | U-PIP-01…18, U-BCK-01…06 (alle grün; U-BCK-01 mit Toleranz nach T-07) |
| M3 | Verarbeitungsschritte, Multigrade, Negativ-Generator | Fertig (Weiterarbeit auf Anweisung des Nutzers, OK formal offen) | U-STP-01…28, K-08, K-09, K-10 (alle grün) |
| M4 | LibRaw, OIIO, LCMS, Vertragstests | Fertig (Weiterarbeit auf Anweisung, OK formal offen) | K-01…K-05, K-07, K-14 (Teil Dateien/Farbe) |
| M5 | Analyse, K-11 | **Technik fertig, K-11 wartet auf Annotationen (L-06/L-07)** | U-ANA-01…12, K-14 (IVision) |
| M6 | Datenhaltung und Anwendung | Fertig (autonom, D-15) | U-DAT-01…15, U-APP-01…16, K-14 (ITaskExecutor) |
| M7 | Kontaktabzug | Fertig (autonom, D-15) | U-LAY-01…09, K-15 |
| M8 | Oberfläche, E2E | Fertig (autonom, D-15); Lücken siehe K-M8-11 | U-UI-01…05, K-14 (Repositories, Cache), E-01…03, E-05, E-09, E-10, E-12, E-13 (Teil), E-15, E-17…E-19, E-21…E-29, E-30, E-31…E-34, E-36, E-38, E-39, E-41…E-46, E-48 |
| M9 | CLI, Leistung, Abschluss | Fertig (autonom, D-15) | K-16, K-17, K-18 (Messprogramm, T-17), K-20, E-49 (über K-18) |

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

### M3 – Ergebnis

- `autoneg::steps` (Architektur 5, 9.5, 9.6): `multigrade.hpp` (GradeData, SplitGrade, PaperCurve, MultigradeModel mit 12 Stufen nach L-05), `pointwise_steps.hpp` (BwMix, FilmBase, Density, DensityRange, Exposure, WhiteBalance, Gradation, ColorGrade, Contrast, ToneCurve, Saturation), `other_steps.hpp` (FilmMode, Orientation, Rotation, Crop, FlatFieldCache/FlatField, Retouch, ShadowsHighlights, Denoise, Sharpen), `step_ids.hpp` (Kennungen, Ränge), `registry.hpp` (`registerStandardSteps`). 20 Schritte, alle Version 1 (Referenzliste `tests/unit/steps/step_ids.txt`).
- `pipeline`: `StepDescriptor::alwaysActive`, `RegionStep::halo(p, scale, image)`, `WarpBuilder::canvas()`. `image`: Punktoperation `Saturate`.
- `localTone` (Lichter/Tiefen): Guided Filter im log2-Raum (Halo 0,6 % statt 3,6 %). `inpaint` (Retusche): Heilen mit Helligkeitsausgleich über einen Ring. Beides in Referenz und CpuBackend.
- Testhilfen: `autoneg_test_synthetic` (`synthetic.hpp`: künstliche Farb-/S/W-Negative und Dias mit Filmrand, Perforation, Halter, Staub, Lichtleck, Drehung und Sollwerten; `color_metrics.hpp`: Rec. 2020 → Lab, CIEDE2000). Hilfe `autoneg_test_steps` heißt jetzt `autoneg_test_fakesteps` (B-13).
- Tests: `tests/unit/steps` (U-STP-01…28), `tests/component/pipeline` (K-08, K-09, K-10, Label `component`, echtes CpuBackend).
- Sichtprüfung (D-14): `tests/tools/render_pfm` (C++, PFM durch Pipeline + CpuBackend, kein CTest) und `tools/visual_check/visual_check.py` mit `cases.json` (rawpy → PFM → Pipeline → PNG). Ergebnis an 6 echten Scans (2 Farbnegativ, 2 S/W, 2 Dia, je 12–15 MP in 0,8–1,7 s): Konvertierung aller Filmtypen plausibel, S/W neutral, Multigrade 0/2/5 mit stabilem Mittelton. Befunde für M4/M5 als K-M3-14/-15 eingetragen (Basis bei S/W hinter bwmix messen; Weißabgleich vor der Farbmatrix; robuste Perzentile).

Szenarien (grün): U-STP-01…28, K-08, K-09, K-10. Auslegung einzelner Erwartungen: T-10, T-11.
Nachweise (22.10.2026, Stand 3d3a55a): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan, linux-clang-tsan je 103/103, keine Warnungen; clang-format sauber.

Gefundene und behobene Fehler: Halo bei Lichter/Tiefen (K-M3-05), Rand bei der Retusche auf Verläufen (K-M3-06), synthetisches S/W-Negativ mit Farbmaske (Generator), Namenskollision der Testziele (B-13).

Offen aus M3:
- Automatiken (Bildfeld, Basis, Dichtegrenzen, Auto-Tonwerte) → M5; U-STP-04/K-08 dann zusätzlich mit echten Automatiken (T-10).
- FlatFieldCache aus dem Repository laden → M6.
- SIMD für Retusche, Rauschminderung, Verzerrung → M9.

### M4 – Ergebnis

- `autoneg::adapter::libraw::LibRawDecoder` (Pool von LibRaw-Instanzen, Rec. 2020 linear, Weißabgleich der Aufnahme, Erkennung abgeschnittener Dateien, verständliche Fehler mit Abhilfe).
- `autoneg::adapter::oiio::OiioImageReader` (TIFF/PNG/JPEG, ICC-Profil über `profile()`) und `OiioImageWriter` (TIFF 8/16/32, PNG 8/16, JPEG, JPEG XL; ICC, EXIF; temporäre Datei + Umbenennen; `encode()` in den Speicher).
- `autoneg::adapter::lcms::LcmsColorEngine` / `LcmsTransform` (Profile Rec. 2020 linear, sRGB, Adobe RGB, ProPhoto, Grau 2,2; float, threadsicher, `lut3d`).
- Port-Ergänzungen K-M4-01/02; vcpkg-Features `raw;imageio;color` in den Presets, Overlay-Port OIIO (B-16).
- Tests: `tests/component/contracts` (K-14 gegen Fakes und echte Adapter), `tests/component/adapters` (K-01, K-03, K-04, K-05, K-07; K-02 über `k02_linearity.py` + `tests/tools/decode_raw`), eingecheckte Testdateien `tests/component/data` aus `tools/gen_testdata/gen_component_data.py`, Fakes `tests/mocks/fakes.hpp`, `testing::TempDir`.
- Nachweise (22.10.2026): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan, linux-clang-tsan je 151/151. Unter TSan zwei gutartige Meldungen in Fremdcode unterdrückt (`tests/tsan.supp`, B-19).
- Gefundene und behobene Probleme: JPEG XL fehlte (B-16, Overlay-Port), statische JXL-Abhängigkeiten (B-16), TSan-Toolchain ohne `CMAKE_SYSTEM_PROCESSOR` (B-18), abgeschnittene RAW-Dateien wurden stillschweigend aufgefüllt (K-M4-06), Thread-Pool von OIIO (K-M4-11).
- Messwerte (Windows Debug): K-01 448 × ILCE-7RM4 + 1 × X-E5, Schachbrettenergie 0,9 % (< 3 %); K-02 99. Perzentil 0,04 % (< 1 %); K-05 alle verlustfreien Formate bitgleich, JPEG 1,38 Stufen; K-07 größtes dE00 0,003 (sRGB, Adobe RGB, ProPhoto, Monitor P3).

### M5 – Ergebnis

- `autoneg::analysis`: `FrameDetector` (`detect`, `detectAll`), `SkewEstimator`, `RegionMap`/`RegionClass`, `BaseMeter` (`measure`, `measureRect`), `BaseEstimator`, `AutoLevels`, `AutoNeutral`, `HistogramCalculator`; interne Ebenen-Hilfen `src/planes.hpp`.
- `autoneg::adapter::opencv::OpenCvVision` (Canny, Konturen, HoughLinesP, Morphologie; vcpkg-Feature `vision`, nur core/imgproc). Vertrag von `IVision` präzisiert (K-M5-03), `FakeVision` in `tests/mocks`.
- Generator: mehrere Bildfelder je Streifen (`NegativeSpec::frames`).
- Tests: `tests/unit/analysis` (U-ANA-01…12), K-14 für `IVision` (Fake und OpenCV), Werkzeug `tests/tools/analyze` (Bericht + Übersichtsbilder für K-11).
- Echte Scans (Stichprobe 31, Sichtprüfung): etwa 21 von 26 bewertbaren richtig; Schräglage durchweg plausibel (Konsens paralleler Kanten). Fehlerfälle und Bedarf an Annotationen: L-07.
- Nachweise (22.10.2026): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan, linux-clang-tsan je 174/174 (GCC nach Behebung zweier Warnungen).
- Laufzeit: Unit-Tests im Debug-Bau sequenziell 57 s (Vorgabe < 1 min, knapp; Release 8,5 s). Größter Einzeltest U-PIP-13 (18 s).

Offen aus M5:
- K-11 (Abnahme an echten Scans) braucht Soll-Annotationen (L-06). Vorschlag: Annotationen als JSON je Scan (Bildfeld-Ecken, Basis-Rechteck), erstellt mit einem kleinen Python-Werkzeug; danach Kennzahlen automatisch.
- Erkennung verbessern für: dunkle Bildränder (Dia/Negativ unterbelichtet), dünne Farbnegative, schwarze Masken (Fuji-RAF), leere Scans ablehnen.

### M6 – Ergebnis

- `autoneg::adapter::json`: `AtomicFileWriter` (temporär, fsync/FlushFileBuffers, rename/ReplaceFileW, Ordner-Sync; Prüfpunkte für Tests), `SchemaMigrator`, `EditCodec` (Bearbeitungsliste ↔ JSON über `ports::StepCatalog`), `JsonSidecarStore` (Begleit- und Rollendateien, Ersatzablage, Zuordnung über Größe/Zeit/Prüfsumme, defekte Dateien, neueres Schema nur lesend, Sperrdatei, Umbenennen), `JsonConfigStore` (Einstellungen ohne Standardwerte, Vorlagen je Art).
- `autoneg::adapter::xxhash::XxHasher` (XXH3-128, 8-MB-Blöcke). `autoneg::adapter::tbb::TbbTaskExecutor` (drei Arenen mit Priorität, Threadzahl zur Laufzeit).
- `autoneg::application`: Befehle (`SetEntry`, `RemoveEntry`, `ToggleEntry`, `Paste` mit `PasteMode`, `ApplyPreset`, `SetRollDefaults`, `SetAutomaticEntries`), `EditClipboard`, `CommandBus` mit `UndoStack`/`UndoGroup`, `Session`, `SyncController`, `PersistenceQueue`, `MemoryBudget`, `JobScheduler`/`JobHandle`/`ThreadConfig`, `RenderService` (+ `IRenderer`, `PipelineRenderer`), `AnalysisService`, `ExportService` (Dateinamen, Export-Job), `stepCatalog(registry)`, Platzhalter `NoInferenceRuntime`, `NoFilmProfiles`.
- Ports: `StepCatalog`, `lock`/`unlock`, `relink`, `ErrorCode::Conflict`; `Roll::extra`.
- Tests: `tests/unit/data` (U-DAT-01…15), `tests/unit/application` (U-APP-01…16 + 4 × `M6_`), K-14 für `ITaskExecutor` (Fake und oneTBB), Fakes `FakeTaskExecutor`.
- Gefundener und behobener Fehler (zuerst durch Test reproduziert): `TbbTaskExecutor` benachrichtigte nach dem Freigeben der Sperre; `shutdown()` konnte zurückkehren und das Objekt zerstören, bevor `notify_all()` lief (TSan, Vertragstest). Ebenso in `MemoryBudget::release` vorsorglich umgestellt. Weitere: Datei beim Umbenennen nach `.defekt` noch geöffnet (Windows), `extra` = `{}` statt leer.
- Nachweise (23.10.2026, Stand f4844c3): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan, linux-clang-tsan je 216/216.
- Laufzeit Unit-Tests Debug 58 s (Vorgabe < 1 min, knapp), Release 9,4 s.

Offen aus M6:
- `FileThumbnailCache`, `ThumbnailService`, `RollService` (Ordner öffnen, Frames, Prüfsummen) mit der Oberfläche (M8); K-14 für die Repositories (IEditRepository, IRollRepository) folgt mit `RollService`.
- Unit-Laufzeit: U-PIP-13 (18 s) und die Analyse-Tests verkürzen, bevor neue Unit-Tests dazukommen.

### M7 – Ergebnis

- `autoneg::layout`: Seiten- und Filmformate (`PaperSize`, `FilmFormat::catalog()` mit 7 Formaten), `ContactSheetSpec`, `ContactSheetLayouter` (Streifen in Aufnahmereihenfolge, Filmrand, Heftrand mit Lochmarken, Kopfzeile, Seitenzähler, 1:1 mit Hinweis, mehrseitig), Elemente `ImageSlot`, `TextBox` (mit Ausrichtung), `PunchMark`.
- Port `IDocumentRenderer` (`RenderedPage`, `PrintSettings`), Adapter `QtDocumentRenderer` (QPdfWriter, ICC als Output Intent PDF/X-4, QPrinter mit Druckdialog im UI-Thread) und `QtGuiSession`.
- `ContactSheetService` (application): Layout, Rendern je Slot in Slotauflösung, einheitlich/wie bearbeitet, Filmrand durch erweiterten Zuschnitt, PDF- und Druck-Job.
- Qt 6.11.2 über vcpkg (Feature `gui`, D-16), Option `AUTONEG_WITH_GUI`; TSan-Preset ohne GUI.
- Tests: U-LAY-01…09, K-15 (pypdf: Seitengröße, Lage/Größe ±0,2 mm, ≥ 300 dpi, ICC, Text), `M7_*` für Dienst und Adapter.
- Gefundene Probleme: offscreen ohne Schriften unter Windows (B-22), Autotools für gperf in WSL (B-21).

### M8 – Ergebnis

- `autoneg::ui`: `MainWindow` (Filmstreifen, Arbeitsfläche, Werkzeuge, Bearbeitungsliste, Histogramm, Menüs, Kürzel, Drag & Drop, Bewertung und Filter, Vorlagen, Einstellungen), `ImageCanvas` (QRhiWidget, Textur-Kopie, Zoom Einpassen/100 %/200 %), Werkzeuge `CropRotateTool`, `RectPickerTool`, `RetouchTool`, Felder `ConversionPanel`, `GradationPanel`, `AdjustPanel` (`ParamSlider`), `EditListModel`, `FilmstripModel`/`FilmstripView`, `HistogramWidget`, Dialoge (Export, Kontaktabzug, Einfügen, Einstellungen), `QtSessionBridge`, `UiController`, `GuiApplication`, englische Oberfläche über `TableTranslator` (Tabelle, Prüfung `tools/check_translations.py`).
- application: `RollService` (Ordner öffnen, Prüfsummen, Begleitdateien, Umbenennungen), `ThumbnailService` (JPEG, Cache, Bündelung), `AnalysisService::measureBase` (Rechteck), `CommandBus::setRating`, `ExportSettings::longEdge/grayscale` (Pipeline verkleinert beim Export).
- adapter_json: `FileThumbnailCache` (LRU, 2 GB); Vorlagennamen mit Sonderzeichen und Umlauten (Fehler zuerst per Test `M8_PresetNamesWithSpecialCharacters` reproduziert, dann behoben).
- Programm `autoneg` mit Kompositionswurzel `apps/autoneg/composition.cpp` (auch von den E2E-Tests genutzt), Optionen `--config`/`--cache`, `qt.conf` im Entwicklungsbau (Windows).
- Tests: U-UI-01…05, K-14 für IEditRepository/IRollRepository/ICacheStore (Fakes und JSON-Adapter), E2E mit Qt Test (offscreen, künstliche Negative als TIFF), `M8_*` für Dienste.
- Probezug mit echten Scans (Windows, eigene Fenster): Rolle öffnet, Vorschau und Miniaturen erscheinen.
- Unit-Laufzeit Debug nacheinander 59,4 s (Vorgabe < 1 min, knapp; U-PIP-13 18,5 s, Bildgrößen vom Szenario vorgegeben).

Offen nach M8 (K-M8-11): Weißabgleich-Pipette, Kurven-Werkzeug (Tonwerte und Kurven), Basis übernehmen/sperren (E-06/E-07), Flat-Field-Kalibrierung (E-08), Messbereich anzeigen (E-04), einheitlicher Kontaktabzug in E2E (E-35), Drucken (E-37, braucht Drucker), schreibgeschützter Ordner (E-40), hohe Pixeldichte (E-47), Leistung (E-49, M9), Detailansicht mit ROI. Gleiche Scans mit identischem Inhalt teilen sich die FrameId.

### M9 – Ergebnis

- Gemeinsame Kompositionswurzel `apps/shared` (`autoneg::app::Composition`, ohne Qt) für GUI, Kommandozeile und Tests; Benutzerverzeichnisse nach Architektur 8.
- `autoneg-cli`: `info`, `detect`, `export` (Optionen wie im Exportdialog, `--preset`), `contact-sheet` (PDF, nur mit Qt); UTF-8-Manifest unter Windows.
- `SpdLogger` (adapter_spdlog) mit eigener rotierender Senke (Unicode-Pfade unter Windows); Protokoll `autoneg.log` im Cache-Verzeichnis.
- Leistung (D-17): `IComputeBackend::colorTransform` (Farbumrechnung datenparallel, bitgleich), LibRaw mit OpenMP (Feature `raw-parallel`, nicht unter TSan), Freigabe der Quellen nach jedem Exportbild.
- Behoben (je zuerst per Test reproduziert): Ersetzen unter Windows bei kurz geöffneter Zieldatei (B-29); Lecks der Qt-Instanz in Tests (B-30); Datenrennen in Little CMS beim gleichzeitigen Anlegen von Transformationen (B-35, Sperre im Adapter).
- Sanitizer: Unterdrückungen nur für Fremdcode – TSan `std::ctype<char>::narrow` (B-32), LSan `libomp`/Qt-Schriftcache im Worker (`tests/lsan.supp`, B-34). K-16-Treiber begrenzt offene Jobs, Watchdog zählt nur Fortschritt (B-37).
- Nachweise (23.10.2026): windows-clang Debug + Release, linux-clang, linux-gcc, linux-clang-asan je 307/307; linux-clang-tsan 263/263 (ohne GUI). K-16 über 600 s unter TSan bestanden (627 s, 74 253 Befehle, 24 789 Miniaturen, keine Meldung).
- Tests: K-17 (CLI = Anwendung, Pixel bitgleich), K-16 (Stress, Watchdog, Konsistenz), K-20 (Protokoll, Rotation, Umlaute), `M9_*` (Backend, Datenhaltung, CLI-Aufruffehler).

Leistungsmessung (K-18/E-49, `tests/tools/bench`, Release, Entwicklerrechner mit 32 Hardware-Threads – nicht der Referenzrechner mit 8 Kernen):

| Messung | Ergebnis | Ziel |
|---|---|---|
| Vorschau nach Regler, 95. Perzentil (61 MP / 100 MP) | 9,3 / 9,2 ms | ≤ 100 ms |
| Bild öffnen bis Vorschau (61-MP-RAW / 100-MP-TIFF) | 0,86 / 1,03 s | ≤ 3 s |
| Export volle Auflösung 100 MP (TIFF 16 bit) | 9,9 s | ≤ 10 s |
| Export 61-MP-RAW inkl. Dekodieren | 7,3 s | – |
| Skalierung 1 → 8 Threads | 2,5 × | ≥ 5 (offen) |
| Miniaturen 36 Scans | 8,9 s | ≤ 15 s |
| Kontaktabzug 36 Felder | 29,5 s | ≤ 30 s (knapp) |
| Stapelexport 36 Bilder JPEG | 308 s | – |
| Spitzenspeicher | 5,3 GB | ≤ 6 GB |

Offen nach M9: Skalierung (serielles TIFF-Schreiben, Gleitkommapfad von Little CMS), Stapelexport rechnet Bilder nacheinander, Werte auf dem Referenzrechner prüfen; Lücken aus K-M8-11; K-11 (Annotationen).

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
| D-14 | 22.10.2026 | Sichtprüfung echter Scans bis M4 über Python `rawpy` (`tools/visual_check`) |
| T-07 | 22.10.2026 | Toleranz 2e-6 für Punktprogramme mit exp10 |
| L-05 | 22.10.2026 | Multigrade-Kennwerte aus Ilford MULTIGRADE IV RC DeLuxe |
| A-09…A-12 | 19.10.2026 | genehmigt: ui → Kernmodule, Adapterregeln (TBB in adapter_tbb und backend_cpu, OS-Header nur in Adaptern), Drittbibliotheken unter Windows mit vcpkg-Standard-Toolchain, statische Module |
| K-M1-01…14 | 19.10.2026 | Abweichungen von den Klassendiagrammen in M1 (Details in `abweichungen_planung.txt`, Abschnitt G) |
| D-15 | 23.10.2026 | Weiterarbeit bis M9 ohne Meilenstein-OK; eigene Entscheidungen als ENTWICKLER |
| D-16 | 23.10.2026 | Qt 6.11.2 (vcpkg, reduzierte Features), Option `AUTONEG_WITH_GUI`, TSan ohne GUI (ENTWICKLER) |
| D-17 | 23.10.2026 | Farbumrechnung datenparallel im Backend, LibRaw mit OpenMP (ENTWICKLER) |

## Offene Fragen an den Nutzer

1. Annotationen (L-06) – spätestens für M5. Einordnung der Testordner siehe Tabelle (nach Sichtprüfung M3, bitte bestätigen).
2. Zustimmung zu K-M3-01…15, T-10, T-11.

## Plattformabhängiger Code (auf dem anderen System prüfen)

- `cmake/triplets/*` und `tests/CMakeLists.txt` (taskset): nur Linux/TSan.
- `src/backends/cpu/src/kernels_hwy.cpp`: Highway-Zielauswahl zur Laufzeit; auf beiden Systemen AVX2 + EMU128 getestet (Rechner ohne AVX-512).
- `src/pipeline/src/source_provider.cpp`: std::shared_future / Threads (U-PIP-16 auf beiden Systemen grün).

- `cmake/AutoNegWarnings.cmake`: getrennte Warnungs-Zweige für clang-cl und clang/GCC – nach jeder Änderung beide Systeme bauen (beide am 19.10.2026 grün).
- `cmake/AutoNegTest.cmake`: gtest wird unter Windows als DLL, unter Linux statisch gelinkt; unterschiedliches Verhalten siehe B-07.
- `src/image/include/autoneg/image/storage.hpp`: `AlignedAllocator` nutzt `operator new(size, align_val_t)` – auf beiden Systemen getestet (U-IMG-03).
- `src/base/src/hash.cpp`: plattformunabhängige Prüfsumme (feste Bytereihenfolge); Referenzwert im Test auf beiden Systemen gleich.
- `src/adapters/libraw/src/libraw_decoder.cpp`: Windows öffnet mit `wstring` (Umlaute im Pfad, z. B. „Marius Götz“), Linux mit UTF-8.
- `src/adapters/oiio/src/oiio_image_io.cpp`: Umbenennen über vorhandene Datei (`std::filesystem::rename`) – Windows ersetzt per MoveFileEx, Linux per rename(2); Contract-Test K-14 auf beiden Systemen.
- `tests/component/adapters/oiio_test.cpp`: `std::system` mit unterschiedlicher Quotierung (cmd.exe vs. sh).
- `tests/component/adapters/libraw_test.cpp`: `_dupenv_s` unter Windows statt `getenv`.
- `src/adapters/json/src/atomic_file_writer.cpp`: Windows CreateFileW/FlushFileBuffers/ReplaceFileW bzw. MoveFileExW, Linux open/fsync/rename/fsync(Ordner) – auf beiden Systemen geprüft (U-DAT-06).
- `src/adapters/json/src/json_sidecar_store.cpp`: Prozesskennung, Rechnername, Lebenszeichen (OpenProcess/WaitForSingleObject bzw. kill(pid, 0)); Änderungszeit über `clock_cast` (U-DAT-13, U-DAT-10).
- `src/adapters/tbb/src/tbb_task_executor.cpp`: Arena-Prioritäten und global_control; Verhalten unter TSan geprüft.
- `tools/visual_check/visual_check.py`: ruft das Linux-Programm aus Windows über `wsl` auf (WSL-Pfade); unter Linux direkt mit dem Programmpfad.
- `src/model/include/autoneg/model/roll.hpp`: `Frame::path`/`Roll::folder` sind `std::filesystem::path` – Pfadtrennzeichen und Kodierung (Umlaute, z. B. „Marius Götz“) ab M4/M6 auf beiden Systemen prüfen.
- `src/adapters/qtprint/src/qt_document_renderer.cpp`: `QtGuiSession` setzt nur unter Linux ohne Anzeige `QT_QPA_PLATFORM=offscreen`; Pfade über `fromStdU16String` (B-22).
- `tests/component/adapters/qtprint_test.cpp`, `tests/unit/ui/ui_test.cpp`, `tests/e2e/e2e_test.cpp`: Umgebungsvariablen über `_putenv_s`/`setenv`; Qt-Plugins unter Windows über `QT_PLUGIN_PATH` (Qt als DLL), unter Linux statisch eingebunden (`qt_import_plugins`).
- `apps/autoneg/CMakeLists.txt`: `qt.conf` nur bei Qt als DLL (Windows); `WIN32_EXECUTABLE`.
- `src/ui/src/gui_application.cpp`: Benutzerverzeichnisse über `QStandardPaths` (%APPDATA% bzw. ~/.config).
- `src/adapters/json/src/json_config_store.cpp`: Vorlagennamen als UTF-8-Pfad (`u8path`), Windows-unzulässige Zeichen kodiert.
- `src/application/src/roll_service.cpp`: Änderungszeit über `clock_cast`, Dateiendungen ohne Beachtung der Groß-/Kleinschreibung.
- `apps/shared/composition.cpp`: Benutzerverzeichnisse über `%APPDATA%`/`%LOCALAPPDATA%` (UTF-16, `_wdupenv_s`) bzw. `XDG_*`/`HOME`.
- `apps/shared/utf8.manifest`: nur Windows (Codepage UTF-8 für argv); unter Linux sind Argumente ohnehin UTF-8.
- `src/adapters/spdlog/src/spd_logger.cpp`: Datei über `std::ofstream(std::filesystem::path)`; Zeilenende CR LF unter Windows.
- `src/adapters/json/src/atomic_file_writer.cpp`: Wiederholversuch nur unter Windows (B-29).
- `tests/tools/bench/bench.cpp`: Spitzenspeicher über `GetProcessMemoryInfo` bzw. `getrusage`.
- `vcpkg.json` Feature `raw-parallel`: OpenMP-Laufzeit vcomp (Windows) bzw. libgomp/libomp (Linux).
