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
