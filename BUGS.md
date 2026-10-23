# BUGS – wiederkehrende Probleme bei der Entwicklung

Sammlung von Problemen, die bei Build, Tests, Werkzeugen oder Plattformen wiederholt auftreten, mit Ursache und Abhilfe. Einmalige Programmfehler gehören in einen Test plus Fix (siehe CLAUDE.md), nicht hierher – außer sie treten erneut auf.

Format je Eintrag:

```
## B-<Nr>: <Kurztitel>
- Erstmals: <Datum>, zuletzt: <Datum>, Häufigkeit: <n>
- Plattform: Windows / Linux / beide
- Symptom: <Fehlermeldung oder Verhalten>
- Ursache: <falls bekannt>
- Abhilfe: <Workaround oder Lösung>
- Status: offen / umgangen / behoben
```

---

## B-01: Windows-Build ohne MSVC-Toolset und Windows SDK nicht möglich
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Windows
- Symptom: Visual Studio Build Tools 2022 sind installiert, aber `VC\Tools\MSVC` und `Windows Kits\10` fehlen; clang (`x86_64-pc-windows-msvc`) findet weder STL noch CRT noch SDK-Header.
- Ursache: Workload „Desktopentwicklung mit C++“ nicht installiert. clang-cl braucht unter Windows die MSVC-Standardbibliothek und das Windows SDK, auch wenn MSVC selbst nicht als Compiler dient.
- Abhilfe: Workload „Desktopentwicklung mit C++“ nachinstalliert (mit UAC-Bestätigung durch den Nutzer):
  `setup.exe modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended --passive --norestart`
  Ergebnis: MSVC 14.44.35207, Windows SDK 10.0.26100.0.
- Hinweis: clang-cl mit Ninja braucht die VS-Umgebung (INCLUDE/LIB). Auf der Kommandozeile vorher `vcvars64.bat` aufrufen bzw. aus der „Developer PowerShell for VS 2022“ arbeiten; CMake Tools in VS Code richtet sie selbst ein. LLVM (`C:\Program Files\LLVM\bin`) muss im PATH stehen.
- Status: behoben (19.10.2026)

## B-02: Linux-Werkzeuge in WSL (Arch) unvollständig
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Linux (WSL 2, Arch Linux)
- Symptom: Nur GCC 16.1.1 vorhanden; `clang`, `cmake`, `ninja` fehlen. Damit sind die Presets `linux-clang`, `linux-clang-asan` und `linux-clang-tsan` lokal nicht nutzbar.
- Abhilfe: `pacman -S` allein scheitert mit 404, weil die Paketdatenbank veraltet ist; Arch unterstützt nur Komplett-Updates. Lösung (WSL-Benutzer ist root): `wsl -d archlinux -- pacman -Syu --needed --noconfirm clang lld compiler-rt cmake ninja python zip unzip`. Danach: clang 23.1.3, CMake 4.4.4, Ninja 1.13.2, Python 3.14.8. vcpkg für Linux liegt in WSL unter `~/vcpkg` (Klon von `D:\vcpkg`, gleicher Commit, `./bootstrap-vcpkg.sh -disableMetrics`); Presets brauchen `VCPKG_ROOT=$HOME/vcpkg`.
- Status: behoben (19.10.2026)

## B-05: Befehle an WSL aus Git-Bash werden verfälscht
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 2
- Plattform: Windows (Git-Bash → WSL)
- Symptom: (a) Shell-Variablen in `wsl -- sh -c '…$p…'` sind leer, (b) Pfade wie `/mnt/c/…` werden zu `C:/Program Files/Git/mnt/c/…`.
- Ursache: (a) `wsl.exe` lässt die Befehlszeile vor `sh -c` noch einmal von der Login-Shell auswerten; (b) MSYS-Pfadumwandlung in Git-Bash.
- Abhilfe: Mehrzeilige Abläufe als Skriptdatei ablegen und mit `MSYS_NO_PATHCONV=1 wsl -d archlinux -- sh /mnt/…/skript.sh <args>` aufrufen. Aus PowerShell tritt (b) nicht auf.
- Status: umgangen

## B-06: Scheinabhängigkeiten `::@(0x…)` in LINK_LIBRARIES
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: beide
- Symptom: Architekturprüfung meldet „model linkt ::@(0x57e0…)“.
- Ursache: `target_link_libraries` aus einem anderen Verzeichnis als dem des Ziels; CMake fügt Verzeichnismarker `::@(…)` in die Eigenschaft ein.
- Abhilfe: `autoneg_finalize_architecture` überspringt Einträge `^::@`.
- Status: behoben

## B-07: Unter Windows findet gtest keine Tests („does NOT link in any test case“)
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Windows (vcpkg `x64-windows`, gtest als DLL)
- Symptom: CTest meldet nur die Architekturtests; das Testprogramm gibt „This test program does NOT link in any test case“ aus und endet trotzdem mit 0. Die Tests verschwinden still.
- Ursache: `gmock.dll` enthält eine eigene Kopie von gtest. Das Testprogramm registrierte seine TESTs über `gmock.dll`, `main()` aus `gtest_main.dll` fragte aber die Registrierung in `gtest.dll` ab.
- Abhilfe: Testprogramme linken nur `GTest::gmock_main` (zieht gmock und gtest nach, eine einzige Instanz). Schutz: Jedes Testprogramm hat zusätzlich den CTest `<ziel>.linked` mit `--gtest_fail_if_no_test_linked`; er schlägt fehl, wenn kein Test registriert ist.
- Status: behoben

## B-03: Python-Ausgabe unter Windows mit falscher Kodierung
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 3
- Plattform: Windows
- Symptom: Umlaute in Ausgaben von Python-Skripten (z. B. Ordner „Marius Götz“, Texte aus der Excel-Datei) erscheinen als `�`; per Heredoc über stdin übergebene Skripte finden Textstellen mit „…“ oder Umlauten nicht.
- Ursache: Konsole, Umleitung und stdin nutzen die ANSI-Codepage statt UTF-8.
- Abhilfe: Python immer mit `python -X utf8` starten (gilt für stdin, stdout und Dateien); Python-Werkzeuge im Repository öffnen Dateien immer mit `encoding="utf-8"`; `check_architecture.py` stellt stdout/stderr selbst auf UTF-8 um.
- Status: umgangen

## B-04: Text aus den Planungs-PDFs mit falscher Kodierung
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Windows (Git-Bash, `pdftotext` aus MinGW)
- Symptom: `pdftotext` liefert Latin-1, Umlaute werden beim Lesen zu `�`; mit `-layout` werden mehrspaltige Tabellen verschoben (IDs und Texte nicht mehr zugeordnet).
- Abhilfe: `pdftotext -enc UTF-8`; Tabellen seitenweise ohne `-layout` extrahieren.
- Status: umgangen

## B-08: Python schreibt unter Windows CRLF-Zeilenenden
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Windows
- Symptom: Git warnt „CRLF will be replaced by LF“ für Dateien, die per Python-Skript geändert wurden.
- Ursache: `Path.write_text`/`open(..., "w")` übersetzt `\n` unter Windows in `\r\n`.
- Abhilfe: Beim Schreiben immer `newline="\n"` angeben (`open(p, "w", encoding="utf-8", newline="\n")`, `Path.write_text(..., newline="\n")` ab Python 3.10). Im Repository sorgt `.gitattributes` (`eol=lf`) ohnehin für LF.
- Status: umgangen

## B-09: WSL-VM stürzt bei stark parallelen Builds ab („Wsl/Service/E_UNEXPECTED“)
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 3
- Plattform: Linux (WSL 2.6.3, Kernel 6.6.87, Arch) auf Windows 11, Ryzen 9 5900XT (32 Threads), WSL-VM mit 15 GB
- Symptom: Während vcpkg Highway baut, endet `wsl.exe` ohne Ausgabe bzw. mit „Catastrophic failure – Error code: Wsl/Service/E_UNEXPECTED“; die VM startet neu, `/tmp` ist leer.
- Ursache: nicht abschließend geklärt. Tritt nur bei voller Parallelität (vcpkg/Ninja mit 32 Jobs) auf; mit 2 Jobs läuft derselbe Build stabil (Spitze 2,9 GB).
- Abhilfe: In WSL `VCPKG_MAX_CONCURRENCY=4` setzen und Ninja mit `-j 8` aufrufen; danach `wsl --shutdown`, falls WSL hängt. Lange Läufe aus PowerShell starten und das Protokoll auf die persistente Platte (`/root/…`) schreiben, nicht nach `/tmp`.
- Status: umgangen

## B-10: Sicht (span) auf einen temporären ImageBuffer
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: beide (nur unter ASan/TSan sichtbar; ohne Sanitizer läuft der Test zufällig grün)
- Symptom: ASan/TSan melden „heap-use-after-free“ in einem Test.
- Ursache: `auto v = synthetic(64, 48).host();` – `host()`, `hostBytes()` und `ImageView::row()` liefern Sichten in den Speicher des Puffers; der temporäre Puffer wird am Ende der Anweisung freigegeben.
- Abhilfe: Puffer immer zuerst in einer benannten Variable halten. Neue Tests stets auch mit `linux-clang-asan` laufen lassen.
- Status: behoben (Test korrigiert), Regel bleibt

## B-11: clang-format sortiert Highway-Includes um
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: beide
- Symptom: Nach `clang-format -i` scheitert `kernels_hwy.cpp` mit „redefinition of …“ in `N_EMU128` und „N_SSSE3 has not been declared“.
- Ursache: `IncludeBlocks: Regroup` sortiert `<hwy/highway.h>` vor `<hwy/foreach_target.h>`; Highway verlangt die umgekehrte Reihenfolge (Mehrfachübersetzung je Befehlssatz).
- Abhilfe: Block mit `// clang-format off` / `// clang-format on` einschließen. Gilt für jede neue Highway-Datei.
- Status: behoben

## B-12: ThreadSanitizer meldet Scheinrennen in oneTBB
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-tsan`
- Symptom: „WARNING: ThreadSanitizer: data race“ in `tbb::detail::d1::start_for` / Partitionierer, Speicher aus `std_cache_aligned_allocate`; kein eigener Code beteiligt.
- Ursache: vcpkg baut oneTBB ohne TSan; die Synchronisation im TBB-Scheduler (libtbb) ist für TSan unsichtbar, die inline instrumentierten Header-Teile sehen deshalb „Rennen“.
- Folgeproblem: Mit instrumentiertem oneTBB und 32 Threads spinnen die wartenden Worker so langsam, dass einzelne Tests nicht fertig werden (auch ein 301x211-Bild > 45 min). Mit 4 oder 8 CPUs: 4 s. Deshalb startet `tests/CMakeLists.txt` im TSan-Preset alle Testprogramme mit `taskset -c 0-7` (CMAKE_TEST_LAUNCHER). Gesamtlauf TSan ca. 6,5 min.
- Abhilfe: Eigenes Triplet `cmake/triplets/x64-linux-tsan.cmake` mit Toolchain `clang-linux-toolchain.cmake`: alle Abhängigkeiten mit clang und `-fsanitize=thread` bauen. oneTBB 2023.1 braucht mit clang zusätzlich `-mwaitpkg` (`_tpause`; das TBB-Build setzt es nur für GCC). Erster Bau der Abhängigkeiten ca. 5 min.
- Status: behoben

## B-13: Namenskollision zwischen Testprogramm und Testhilfe
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: beide
- Symptom: CMake: „target_compile_definitions may only set INTERFACE properties on INTERFACE targets“ bzw. „add_executable cannot create target … because another target with the same name already exists“.
- Ursache: `autoneg_add_test(<name>)` legt `autoneg_test_<name>` an; die Testhilfen heißen ebenfalls `autoneg_test_<x>` (`autoneg_test_support`, `_mocks`, `_reference`, `_synthetic`, `_fakesteps`). Das Testprogramm `steps` kollidierte mit der früheren Hilfe `autoneg_test_steps`.
- Abhilfe: Hilfe umbenannt in `autoneg_test_fakesteps`. Neue Testprogramme nicht nach einer vorhandenen Testhilfe benennen (und umgekehrt).
- Status: behoben

## B-14: Veraltete Testergebnisse nach abgebrochener Befehlskette
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: Linux (WSL), gilt allgemein
- Symptom: Ein bereits behobener Test schlägt in Debug weiter fehl, in Release nicht.
- Ursache: `clang-format -i $(git ls-files …) && … && cmake --build …; ctest …` – die Dateiliste war leer, `clang-format` brach ab („cannot use -i when reading from stdin“), die `&&`-Kette übersprang den Build, `ctest` lief danach trotzdem mit den alten Programmen.
- Abhilfe: Formatieren, Bauen und Testen als Skript mit expliziter Dateiliste (`git diff --name-only --diff-filter=AM` plus `git ls-files -o --exclude-standard`); `ctest` nur nach erfolgreichem Build.
- Status: behoben

## B-15: Heredocs aus Git-Bash mit Umlauten oder Apostrophen brechen ab
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: wiederholt
- Plattform: Windows (Git-Bash)
- Symptom: „unexpected EOF while looking for matching `''“ bei längeren Heredocs (Python- oder Quelltext mit Umlauten, typografischen Anführungszeichen, Backslashes).
- Ursache: Die Befehlszeile wird vor der Ausführung umkodiert und gequotet; Sonderzeichen im Heredoc verschieben die Quote-Erkennung.
- Abhilfe: Dateien mit Umlauten oder Backslashes direkt schreiben (Editor bzw. Schreibwerkzeug), Heredocs nur für reines ASCII. Längere WSL-Befehle als Skriptdatei übergeben (siehe B-05).
- Status: Umgehung

## B-16: OpenImageIO aus vcpkg ohne JPEG XL trotz Feature `jpegxl`
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: beide
- Symptom: `OiioImageWriter::formats()` enthält kein JPEG XL; OpenImageIO meldet beim Bau „JXL library not found“, obwohl `libjxl` installiert ist.
- Ursache: Der Port `openimageio` (Baseline 664933b8, OIIO 3.1.14) löscht `src/cmake/modules/FindJXL.cmake`; `libjxl` liefert keine CMake-Konfiguration, nur pkg-config.
- Folgeproblem (Linux, statisch): Das Original-`FindJXL.cmake` nennt nur `jxl` und `jxl_threads`; beim Linken fehlen `jxl_cms`, Brotli, Highway und Little CMS („undefined reference to BrotliEncoderCreateInstance … JxlGetDefaultCms … hwy::GetChosenTarget“). Unter Windows (DLLs) unsichtbar.
- Abhilfe: Overlay-Port `cmake/ports/openimageio` mit eigenem `FindJXL.cmake` (hängt bei statischem Bau die Abhängigkeiten aus `libjxl.pc` an), eingebunden über `VCPKG_OVERLAY_PORTS` in `CMakePresets.json`. Patches im Overlay behalten CRLF (`.gitattributes`: `cmake/ports/** -text`). Bei einer neuen Baseline prüfen, ob der Port korrigiert ist, und das Overlay dann entfernen.
- Status: Umgehung

## B-17: Python-Testleser unter WSL (Arch) nicht installierbar
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: Linux (WSL)
- Symptom: `pacman -S python-numpy` scheitert („failed to retrieve some files“), `pip` fehlt bzw. ist „externally managed“.
- Ursache: veraltete Paketdatenbank (B-02); ein Komplett-Update während eines laufenden Baus würde den Compiler tauschen.
- Abhilfe: eigene Umgebung `python -m venv /root/pyenv`, darin `pip install numpy pillow tifffile imagecodecs rawpy`. CMake unter Linux mit `-DPython3_EXECUTABLE=/root/pyenv/bin/python` konfigurieren (K-02, K-05 rufen Python auf). Unter Windows liegen die Pakete in der Benutzerinstallation von Python 3.12.
- Status: Umgehung

## B-18: OpenImageIO scheitert im TSan-Triplet an CMAKE_SYSTEM_PROCESSOR
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-tsan`
- Symptom: vcpkg-Bau von `openimageio:x64-linux-tsan` bricht ab: „CMake Error at src/cmake/compiler.cmake:210 (if): … "Linux" "STREQUAL" "FreeBSD" "AND" "STREQUAL" "i386" Unknown arguments“.
- Ursache: Mit eigener Chainload-Toolchain und gesetztem `CMAKE_SYSTEM_NAME` bleibt `CMAKE_SYSTEM_PROCESSOR` leer; die Standard-Toolchain von vcpkg setzt es, unsere `clang-linux-toolchain.cmake` nicht.
- Abhilfe: `CMAKE_SYSTEM_PROCESSOR` in `cmake/triplets/clang-linux-toolchain.cmake` auf `x86_64` setzen. Achtung: jede Änderung an der Toolchain ändert den ABI-Hash; alle TSan-Abhängigkeiten werden neu gebaut.
- Status: behoben

## B-19: ThreadSanitizer meldet Rennen in OpenImageIO und in glibc (über LibRaw)
- Erstmals: 22.10.2026, zuletzt: 22.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-tsan`
- Symptom: „data race“ in `OpenImageIO::spin_mutex::lock` (Thread-Pool von OIIO) bei jedem Schreiben; „data race“ mit `free`/`malloc` unter `LibRaw::get_timestamp` → `mktime` bei gleichzeitigem Dekodieren.
- Ursache: Beides gutartig und in Fremdcode. OIIO liest den Sperrzustand in der Spin-Schleife absichtlich nicht-atomar. glibc legt in `mktime`/`tzset` den Zeitzonenpuffer unter internen Futex-Sperren neu an, die TSan nicht sieht. Ein einmaliger Vorab-Aufruf von `mktime` hilft nicht: Ohne `TZ` prüft glibc `/etc/localtime` bei jedem Aufruf.
- Abhilfe: Die Adapter schalten den Thread-Pool von OIIO je Instanz ab (`threads(1)`); die Nebenläufigkeit steuert die Anwendung. Unterdrückungen eng gefasst in `tests/tsan.supp` (nur `spin_mutex::lock` von OIIO und `LibRaw::get_timestamp`), gesetzt über `CMAKE_TEST_LAUNCHER` (`TSAN_OPTIONS`). Eigener Code wird nie unterdrückt.
- Status: Umgehung

## B-20: Unter Windows gebauter Code scheitert unter Linux (Warnungen, Includes, Linker)
- Erstmals: 22.10.2026 (M3), zuletzt: 23.10.2026 (M6), Häufigkeit: 4
- Plattform: Linux, Preset `linux-gcc`
- Symptom: `-Werror=range-loop-construct` („loop variable '<structured bindings>' creates a copy“), `-Werror=shadow` (Lambda-Parameter verdeckt eine lokale Variable, z. B. `n`), `-Werror=conversion` (`std::mt19937::result_type` → `double`).
- Ursache: GCC prüft diese Fälle strenger als clang; unter clang-cl/clang baut derselbe Code ohne Warnung.
- Weitere Fälle: fehlende Includes, die die MSVC-Standardbibliothek indirekt mitbringt (`<algorithm>` für `std::ranges::sort`/`is_sorted`, `<utility>` für `std::exchange`, `<cmath>`, `<numbers>`); Linkfehler „undefined reference to `tbb::detail::d1::task_arena_base::automatic`“, wenn eine statische Konstante ohne Definition per Referenz übergeben wird (ODR-Nutzung, nur im Debug-Bau).
- Abhilfe: In Range-Schleifen mit strukturierter Bindung immer `const auto&`; Lambda-Parameter nicht wie lokale Variablen nennen; Zufallswerte explizit umwandeln; jeden benutzten Standard-Header selbst einbinden; statische Konstanten fremder Bibliotheken als Wert übergeben (`static_cast<int>(…)`). Nach jedem Teilschritt früh unter Linux bauen (`scratchpad/linux_quick.sh` mit `-k 0`).
- Status: wiederkehrend

## B-21: vcpkg baut Qt-Abhängigkeiten unter Linux nicht (gperf braucht Autotools)
- Erstmals: 23.10.2026 (M7), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux (WSL Arch), Preset `linux-clang` mit Feature `gui`
- Symptom: „building gperf:x64-linux failed with: BUILD_FAILED“, im Log „gperf currently requires the following programs from the system package manager: autoconf autoconf-archive automake libtoolize“.
- Ursache: gperf (für fontconfig, über qtbase) wird mit Autotools gebaut; vcpkg bringt sie nicht mit.
- Abhilfe: In WSL einmalig `pacman -S --needed autoconf autoconf-archive automake libtool pkgconf bison flex`, danach neu konfigurieren.
- Status: behoben

## B-22: Qt-Plattform offscreen hat unter Windows keine Schriften
- Erstmals: 23.10.2026 (M7), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Windows, Komponententest K-15 (`QtDocumentRenderer`)
- Symptom: PDF ohne jeden Text (keine Schriften, kein `Tj`); mit `QT_FORCE_STDERR_LOGGING=1` und `QT_LOGGING_RULES=qt.text.font*=true`: „QFontDatabase: Cannot find font directory …/lib/fonts. Note that Qt no longer ships fonts.“
- Ursache: Das offscreen-Plugin nutzt unter Windows eine eigene Schriftdatenbank (FreeType mit `lib/fonts`) statt der Systemschriften.
- Abhilfe: Unter Windows die native Plattform `windows` verwenden; sie braucht kein sichtbares Fenster und findet die Systemschriften. `QtGuiSession` setzt offscreen nur unter Linux ohne `DISPLAY`/`WAYLAND_DISPLAY` (dort mit fontconfig). Qt-Meldungen gehen unter Windows sonst an den Debugger: zum Sehen `QT_FORCE_STDERR_LOGGING=1` setzen.
- Status: behoben

## B-23: Qt-Makro `emit` zerstört `IStep::emit` (Kompilierfehler in step.hpp)
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle, Modul `ui`
- Symptom: in `pipeline/step.hpp` „field has incomplete type 'void'“, „'virtual' can only appear on non-static member functions“ an `virtual void emit(...)`, sobald ein Qt-Header davor eingebunden ist.
- Ursache: Qt definiert `emit` (und `signals`, `slots`) als Makros; `emit` wird leer ersetzt.
- Abhilfe: `QT_NO_KEYWORDS` für `autoneg_ui` (PUBLIC, gilt auch für Tests); in der Oberfläche `Q_EMIT`, `Q_SIGNALS`, `Q_SLOTS` schreiben.
- Status: behoben

## B-24: Simuliertes Ablegen (QDropEvent) kommt im Test nicht an
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle, E2E-Test E-02
- Symptom: `QCoreApplication::sendEvent(window, &drop)` ruft `dropEvent` nicht auf.
- Ursache: Qt stellt `Drop` nur nach einem angenommenen `DragEnter` zu (wie bei echtem Ziehen).
- Abhilfe: im Test erst `QDragEnterEvent` senden (und `isAccepted()` prüfen), dann `QDropEvent`.
- Status: behoben

## B-25: Tastaturkürzel wirken nach einem modalen Dialog nicht (offscreen)
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle, E2E-Test E-25 (Plattform offscreen)
- Symptom: Strg+Z über `QTest::keyClick(window, …)` löst nach dem Einfügen-Dialog nichts aus.
- Ursache: Nach dem Schließen des modalen Dialogs ist unter offscreen kein Fenster aktiv; Kürzel brauchen ein aktives Fenster.
- Abhilfe: im Test `window->activateWindow()` und `QTest::qWaitForWindowActive(window)` vor dem Kürzel.
- Status: Umgehung (nur Test)

## B-26: Gleiche künstliche Testbilder haben dieselbe FrameId
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle, E2E-Tests (Bewertung, Einfügen)
- Symptom: Bewertung oder Einfügen wirkt auf mehrere Bilder zugleich.
- Ursache: Die FrameId ist die Prüfsumme des Dateiinhalts; der Generator lieferte bei gleichen Einstellungen identische Dateien.
- Abhilfe: Testbilder je Nummer unterscheiden (Staub, Lage des Bildfelds). In der Anwendung offen (K-M8-11): echte Doppelscans mit identischem Inhalt teilen sich die FrameId.
- Status: Umgehung (Test), Anwendung offen

## B-27: PDF ohne Text unter Linux (WSL ohne Schriften); pypdf trennt Wörter mit Tabulatoren
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux (WSL Arch), K-15
- Symptom: K-15 meldet fehlende Kopfzeile; `fc-list` liefert 0 Schriften. Nach der Installation fehlen die Texte weiterhin im Vergleich, obwohl sie im PDF stehen: pypdf liefert „Rolle\tK-15\t\t·…“.
- Ursache: Die minimale WSL hat keine Schriftpakete; Qt (offscreen, fontconfig) kann dann keinen Text setzen. Unter Linux setzt die FreeType-Engine Wörter einzeln, pypdf trennt sie mit Tabulatoren.
- Abhilfe: In WSL `pacman -S --needed ttf-dejavu`. `verify_pdf.py` vergleicht Text mit vereinheitlichtem Leerraum. Für ein Linux-Paket (AppImage) eine Schrift mitliefern oder als Abhängigkeit nennen (M9).
- Hinweis: Fontconfig aus vcpkg ist älter als die Arch-Konfiguration (Warnungen zu `48-guessfamily.conf`); harmlos.
- Status: behoben

## B-28: Speichern-Dialog im E2E-Test liefert keinen Dateinamen (Linux)
- Erstmals: 23.10.2026 (M8), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, E2E-Test E-34 (offscreen)
- Symptom: `QFileDialog::selectFile()` + `accept()` schließt den Dialog, `getSaveFileName` liefert aber einen leeren Namen; kein PDF.
- Abhilfe: Im Test wie ein Anwender bedienen: Text in `fileNameEdit` schreiben und den Knopf „Speichern“ drücken.
- Status: behoben (Test)

## B-29: Speichern scheitert sporadisch unter Windows (Windows-Fehler 32 beim Ersetzen)
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1 (CTest parallel)
- Plattform: Windows, `AtomicFileWriter`
- Symptom: K-14 `EditsLastSaveWins` meldet einmal „Ersetzen fehlgeschlagen“; einzeln nicht reproduzierbar.
- Ursache: `ReplaceFileW` scheitert mit Freigabeverletzung (32) bzw. Zugriff verweigert (5), solange ein anderer Prozess die Zieldatei kurz offen hält (Virenscanner, Indexdienst, Sicherung, ein Leser).
- Abhilfe: Bei diesen Fehlern bis etwa 2 s mit wachsender Pause erneut versuchen. Reproduziert mit `M9_ReplaceSucceedsWhileTargetBrieflyOpen` (Datei offen halten, nach 150 ms freigeben).
- Status: behoben

## B-30: LeakSanitizer meldet Qts Schriftcaches in allen Qt-Tests
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-asan`
- Symptom: 35 Tests mit Qt scheitern mit „LeakSanitizer: detected memory leaks“ in `qt_getFreetypeData`, `QFontCache`, `QFreetypeFace::getFace`.
- Ursache: Die Tests legten ihre Qt-Anwendung mit `new` an und gaben sie nie frei.
- Abhilfe: Qt-Instanz in einer gtest-Umgebung halten, die sie nach dem letzten Test abbaut (`QtInstance<T>`, statisch registriert).
- Status: behoben (Nachweis unter ASan siehe PROGRESS)

## B-31: Leistung: serielle Farbumrechnung und RAW-Dekodierung
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle, Messprogramm `tests/tools/bench`
- Symptom: Export 61 MP 15–23 s, keine Skalierung mit der Threadzahl; Stapelexport von 36 Bildern 843 s und 19,8 GB Speicher.
- Ursache: `IColorTransform::apply` (Little CMS) und `LibRaw::dcraw_process` laufen auf einem Kern; der SourceProvider behielt jede Quelle in voller Auflösung.
- Abhilfe: `IComputeBackend::colorTransform` (Zeilenblöcke, oneTBB), LibRaw mit OpenMP, Freigabe der Quellen nach jedem Bild (D-17, K-M9-05). Danach: Export 61-MP-RAW 7,3 s, 100 MP (TIFF) 9,9 s, 5,3 GB.
- Status: behoben; Skalierung 1 → 8 Threads 2,5 statt ≥ 5 bleibt offen (serielles TIFF-Schreiben, Little-CMS-Gleitkommapfad)

## B-32: TSan meldet Rennen in `std::ctype<char>::narrow` (K-16)
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-tsan`
- Symptom: K-16 scheitert mit „data race“ in `std::ctype<char>::narrow`, aufgerufen aus `std::regex` in `OpenImageIO::ImageSpec::erase_attribute` (TIFF öffnen in Render-Thread und Job-Thread gleichzeitig).
- Ursache: libstdc++ füllt einen Zeichen-Cache ohne Sperre; alle Threads schreiben denselben Wert (GCC-Fehler 77704, gutartig).
- Abhilfe: Eintrag `race:std::ctype<char>::narrow` in `tests/tsan.supp`.
- Status: behoben

## B-33: linux-clang findet OpenMP nicht (Feature `raw-parallel`)
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux (Arch), Presets `linux-clang`, `linux-clang-asan`
- Symptom: Konfigurieren bricht ab: „Could NOT find OpenMP_CXX“ aus `librawConfig.cmake`; linux-gcc läuft (libgomp gehört zu GCC).
- Ursache: clang bringt seine OpenMP-Laufzeit nicht mit; unter Arch liegt `libomp` im Paket `openmp`.
- Abhilfe: `pacman -S openmp` (Ubuntu/Debian: `libomp-dev`), dann Build-Verzeichnis neu konfigurieren.
- Status: behoben

## B-34: LeakSanitizer meldet libomp und Qts Schriftcache im Job-Thread
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, Preset `linux-clang-asan`
- Symptom: K-16, K-17, E-17, E-24 scheitern mit 24 Byte „Direct leak“ aus `dlsym` in `libomp.so`; E-34 mit etwa 180 kB „Indirect leak“ in FreeType/HarfBuzz unter `QFontEngineFT` (Kontaktabzug-PDF in einem oneTBB-Worker).
- Ursache: Fremdcode. glibc legt in `dlsym` einen Fehlerpuffer an, den libomp nie freigibt. Qt hält den Schriftcache im Thread-Speicher des übernommenen Worker-Threads; der Worker endet erst mit dem Prozess.
- Abhilfe: `tests/lsan.supp` (eng gefasst, nur Fremdcode), unter ASan per `LSAN_OPTIONS` im Test-Launcher. Achtung: Bei `DISCOVERY_MODE PRE_TEST` übernimmt CTest einen geänderten Launcher erst, wenn das Testprogramm neu gebaut wird; sonst `build/<preset>/tests/**/*_tests.cmake` löschen.
- Status: behoben

## B-35: Datenrennen in Little CMS beim gleichzeitigen Anlegen von Transformationen
- Erstmals: 23.10.2026 (M9, K-16 über 10 min unter TSan), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle (gefunden unter `linux-clang-tsan`)
- Symptom: „data race“ in `cmsXYZ2xyY` aus `cmsD50_xyY` (Schwarzpunktkompensation in `cmsCreateTransformTHR`), zwei Miniaturen-Jobs gleichzeitig.
- Ursache: Little CMS 2.19 schreibt in `cmsD50_xyY` einen statischen, prozessweiten Puffer, auch bei getrennten Kontexten.
- Abhilfe: `LcmsColorEngine` serialisiert das Anlegen von Profilen und Transformationen mit einer prozessweiten Sperre; das Anwenden bleibt ohne Sperre. Reproduziert durch `LcmsComponent.M9_ConcurrentTransformCreationIsRaceFree`.
- Status: behoben

## B-36: clang-format sortiert `psapi.h` vor `windows.h`
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Windows (`tests/tools/bench/bench.cpp`)
- Symptom: „unknown type name 'BOOL'“ in `psapi.h`, nachdem clang-format nach dem letzten Bau gelaufen war.
- Ursache: clang-format sortiert Includes innerhalb einer Gruppe alphabetisch; `psapi.h` braucht die Typen aus `windows.h`.
- Abhilfe: Header, die `windows.h` voraussetzen, in eine eigene Gruppe (Leerzeile, Kommentar) nach `windows.h`. Nach dem Formatieren immer noch einmal bauen.
- Status: behoben

## B-37: K-16 über 10 min überschreitet das CTest-Zeitlimit (Rückstau im Test)
- Erstmals: 23.10.2026 (M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, `linux-clang-tsan`, `AUTONEG_STRESS_SECONDS=600`
- Symptom: Timeout nach 1500 s, kein Watchdog-Alarm.
- Ursache: Der Stress-Treiber startete unbegrenzt Stapelexporte und Erkennungen (alle paar Millisekunden); nach 600 s stand ein Rückstau von Tausenden Jobs an. Der Watchdog setzte den Herzschlag beim Abarbeiten auch ohne Fortschritt.
- Abhilfe: Höchstens 16 offene Jobs (wie bei echter Bedienung); in der Abarbeitungsphase zählt nur ein fertiger Job als Fortschritt.
- Status: behoben (Nachweis siehe PROGRESS)

## B-38: Miniaturen ohne Speicherbudget (bis zu 16 GB beim Öffnen einer Rolle)
- Erstmals: 23.10.2026 (nach M9, Leistungsmessung), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: alle (gemessen unter Windows, 32 Hardware-Threads)
- Symptom: Beim Rendern der Miniaturen von 36 RAW-Scans (61 MP) 15,7 GB über der Grundlast; eine frühere Messung brach Claude Code wegen Speichermangels ab.
- Ursache: Jeder Worker rendert eine Miniatur und dekodiert dafür eine Quelle in halber Auflösung; das MemoryBudget galt nur für den Export.
- Abhilfe: ThumbnailService fordert je Miniatur einen Anteil am Budget an (proxyRenderBytesPerPixel · Quellpixel, wie der Kontaktabzug). Reproduziert durch `ThumbnailBudgetTest.M10_ThumbnailsRespectMemoryBudget`.
- Status: behoben

## B-39: Release-E2E: „resource deadlock would occur“ und hängender K-17 (nicht reproduziert)
- Erstmals: 23.10.2026 (nach M9), zuletzt: 23.10.2026, Häufigkeit: 2 Läufe
- Plattform: Windows, `windows-clang-release`, `ctest -j 8`
- Symptom: (1) E-01, E-26, E-29, E-30 werfen „resource deadlock would occur“, danach Zugriffsverletzung in `TearDown`, weitere E2E-Prozesse hängen. (2) In einem späteren Lauf hängt K-17 (Export über die Anwendung) über 70 Minuten, ohne Kindprozess.
- Ursache: offen. MSVC wirft diese Ausnahme, wenn ein Thread einen `std::mutex` erneut sperrt oder sich selbst joinen will. Geprüft und ausgeschlossen: oneTBB übernimmt während eines wartenden `parallel_for` keine eingereihten Aufgaben (Test `TbbTaskExecutorComponent.M10_WaitingTaskRunsNoOtherQueuedTask`).
- Beobachtung: Nach komplettem Neubau des Release-Baus nicht mehr aufgetreten (E2E einzeln 3×, K-17 8-fach parallel 3×, volle Suite 4× grün). Der erste Fehlerlauf lief parallel zu den Linux-Tests in WSL; vor dem letzten grünen Lauf blockierte eine Datei den Linker („wird von einem anderen Prozess verwendet“).
- Abhilfe: Release-Läufe mit `--timeout 600`, damit ein Hänger nicht unbegrenzt blockiert. Bei erneutem Auftreten: Release mit `/Z7` bauen und den Prozess mit lldb anhängen.
- Status: beobachten

## B-40: TBB-Assertion „Object with intrusive list node …“ in allen TBB-Vertragstests (Linux, Debug)
- Erstmals: 23.10.2026 (nach M9), zuletzt: 23.10.2026, Häufigkeit: 1
- Plattform: Linux, `linux-clang` Debug (unter Windows unauffällig)
- Symptom: Alle Tests mit `TbbTaskExecutor` in `autoneg_test_contracts` brechen sofort ab: „Assertion node(val).my_prev_node == &node(val) … failed (push_front)“.
- Ursache: Ein Test bindet `<tbb/parallel_for.h>` ein, ohne gegen `TBB::tbb` zu linken. Dadurch fehlen die Definitionen der Bibliothek (`TBB_USE_DEBUG`); der Linker mischt Inline-Funktionen mit unterschiedlicher Konfiguration aus Test und Adapter.
- Abhilfe: Jedes Ziel, das TBB-Header einbindet, linkt `TBB::tbb` (hier `tests/component/contracts/CMakeLists.txt`).
- Status: behoben (Nachweis siehe PROGRESS)
