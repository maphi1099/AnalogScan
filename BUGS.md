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
- Abhilfe: Im Visual Studio Installer bei Build Tools 2022 die Workload „Desktopentwicklung mit C++“ mit MSVC v143 und Windows 11 SDK installieren (braucht Administratorrechte, daher durch den Nutzer).
- Folge: Preset `windows-clang` ist geschrieben, aber noch nie gelaufen. Bis zur Installation wird nur unter Linux (WSL) gebaut und getestet.
- Status: offen

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

## B-03: Python-Ausgabe unter Windows mit falscher Kodierung
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 2
- Plattform: Windows
- Symptom: Umlaute in Ausgaben von Python-Skripten (z. B. Ordner „Marius Götz“, Texte aus der Excel-Datei) erscheinen als `�`.
- Ursache: Konsole/Umleitung nutzt die ANSI-Codepage statt UTF-8.
- Abhilfe: Skripte mit `PYTHONIOENCODING=utf-8` bzw. `python -X utf8` starten; Python-Werkzeuge im Repository schreiben Dateien immer mit `encoding="utf-8"`.
- Status: umgangen

## B-04: Text aus den Planungs-PDFs mit falscher Kodierung
- Erstmals: 19.10.2026, zuletzt: 19.10.2026, Häufigkeit: 1
- Plattform: Windows (Git-Bash, `pdftotext` aus MinGW)
- Symptom: `pdftotext` liefert Latin-1, Umlaute werden beim Lesen zu `�`; mit `-layout` werden mehrspaltige Tabellen verschoben (IDs und Texte nicht mehr zugeordnet).
- Abhilfe: `pdftotext -enc UTF-8`; Tabellen seitenweise ohne `-layout` extrahieren.
- Status: umgangen
