# OE-01: Halide oder Google Highway + oneTBB – Messergebnis

Stand 22.10.2026. Rohdaten: `results-windows.md`, `results-linux.md`. Messrechner: AMD Ryzen 9 5900XT
(16 Kerne / 32 Threads, AVX2 + FMA, kein AVX-512), 32 GB; Windows 11 (clang-cl 23.1.3) und Linux in
WSL 2 (clang 23.1.3). „8 Threads“ entspricht dem Referenzsystem des Lastenhefts (8 Kerne).

## Aufbau

- Kernels laut Architektur (OE-01): Basiskorrektur (Skalierung, −log10, Tonwertgrenzen, Belichtung),
  Gradation (Tabelle mit 4096 Stützstellen), beides verschmolzen (wie im Punktprogramm), Schärfen
  (Unscharfmaskierung, Gauß σ = 1,2 px, r = 4, Schwellwert).
- RGB-float, Kanäle verschachtelt wie `autoneg::image::ImageBuffer`; 8 MP (Vorschau) und 100 MP (Export).
- Varianten: skalare Referenz + oneTBB, Highway (Laufzeitauswahl, hier AVX2) + oneTBB, Halide 21 (AOT,
  eigener Thread-Pool). Alle mit Zeilenblöcken zu 64 Zeilen. Median aus 7 Läufen.
- Halide stammt aus dem offiziellen vorkompilierten Paket (Python-Wheel); der vcpkg-Port baut LLVM
  komplett aus dem Quellcode.

## Laufzeiten 100 MP (ms, Median)

| Kernel | Threads | Win skalar | Win Highway | Win Halide | Linux skalar | Linux Highway | Linux Halide |
|---|---|---|---|---|---|---|---|
| Basiskorrektur | 1 | 1702 | **225** | 458 | 1646 | **222** | 454 |
| Gradation (Tabelle) | 1 | 815 | **284** | 393 | 459 | **280** | 402 |
| Basis + Gradation verschmolzen | 1 | 2986 | **654** | 1132 | 2580 | **655** | 1036 |
| Schärfen | 1 | 4807 | **591** | 1034 | 2434 | **309** | 741 |
| Basiskorrektur | 8 | 333 | **99** | 105 | 215 | 99 | **98** |
| Gradation (Tabelle) | 8 | 184 | 101 | **101** | 102 | 98 | **95** |
| Basis + Gradation verschmolzen | 8 | 578 | **156** | 195 | 347 | **106** | 136 |
| Schärfen | 8 | 919 | **333** | 365 | 360 | 322 | **255** |
| Basis + Gradation verschmolzen | 32 | 196 | 105 | **104** | 145 | **101** | 103 |
| Schärfen | 32 | 365 | 347 | **304** | 358 | 351 | **285** |

8 MP mit 8 Threads (Vorschau): verschmolzenes Punktprogramm Highway 8–9 ms, Halide 12 ms; Schärfen 13–22 ms
bzw. 12–23 ms.

## Genauigkeit und Determinismus

- Abweichung zur skalaren Referenz: Highway ≤ 2,4·10⁻⁷, Halide ≤ 3,6·10⁻⁷ (absolut, Werte 0…1). Beim
  Schärfen 1,2·10⁻³ an einzelnen Werten (Anteil < 0,0001 %), weil Rundung bei Werten genau an der
  Schwellwertkante entscheidet – bei allen Varianten gleich, unkritisch.
- Bitgleich über 1, 8 und 32 Threads: Highway ja, Halide ja (Prüfsummen identisch, NF-ARC-16).
- Zwischen Windows und Linux nicht bitgleich (andere Compiler-Optimierungen bzw. Mathematikbibliothek),
  Abweichungen im Bereich 10⁻⁷ – vereinbar mit NF-QUAL-02 („plattformübergreifend nahezu identisch“).

## Befunde

1. **Ab etwa 8 Threads begrenzt der Arbeitsspeicher, nicht die Rechenleistung.** Ein Durchlauf über
   100 MP liest und schreibt 2,4 GB und braucht bei allen Varianten rund 100 ms. Entscheidend ist dann
   die Zahl der Speicherdurchläufe (Fusion, AE-05) und die Kachelung der Nachbarschaftskernels, nicht
   die Wahl zwischen Halide und Highway.
2. **Einkernig ist Highway 1,5–2,5× schneller als die Halide-Fassung** (bei gleichem Aufwand für beide;
   Halide-Schedules ließen sich mit Erfahrung weiter verbessern).
3. **Schärfen mit 8+ Threads: Halide 10–20 % schneller**, weil es die Zwischenwerte besser im Cache
   hält. Bei Highway ist das mit kachelweiser Verarbeitung nachholbar (Prototyp legt je Block einen
   Zwischenpuffer an).
4. Leistungsziele sind mit beiden erreichbar: Vorschau 8 MP < 10 ms je Punktdurchlauf (Ziel 100 ms
   gesamt), Export 100 MP bei 9 Durchläufen deutlich unter 3 s (Ziel 10 s).

## Bewertung

| Kriterium | Highway + oneTBB | Halide |
|---|---|---|
| Geschwindigkeit, mehrere Threads | gleichauf (speichergebunden) | gleichauf; Schärfen etwas besser |
| Geschwindigkeit, ein Thread | **besser** | schlechter (Schedules müssten getunt werden) |
| Einbindung in die Arenen (Kap. 6, Vorrang Interaktiv) | **direkt**, Threads gehören oneTBB | eigener Thread-Pool; Umleiten auf oneTBB über `halide_set_custom_do_par_for` nötig |
| Build und Abhängigkeiten | **kleine Bibliothek über vcpkg, Minuten** | vcpkg baut LLVM (Stunden, viele GB) oder vorkompiliertes Paket außerhalb von vcpkg; Generatoren laufen beim Build |
| Plattformen Windows/Linux, clang | **problemlos** | geht; Linux-Paket brauchte Nacharbeit (Ausführungsrechte) |
| SIMD-Auswahl zur Laufzeit, Test je Stufe (U-BCK-02) | **eingebaut** (AVX2, AVX-512, NEON …), im Test erzwingbar | über Multi-Target-AOT möglich, größere Binaries |
| Code und Wartung | ausführlicher (Ränder, Kanalmuster von Hand), gewöhnliches C++, gut debugbar | **knapper**, Algorithmus und Schedule getrennt; eigene Sprache, Fehlersuche schwieriger |
| GPU ab Version 1.1 | eigener Code für Vulkan nötig | Vulkan-Backend in Halide vorhanden (weniger ausgereift) – möglicher Vorteil für OE-02 |
| Lizenz | Apache-2.0 / BSD-3, oneTBB Apache-2.0 | MIT |

## Empfehlung

**Google Highway + oneTBB.** Die Geschwindigkeit ist bei realistischer Threadzahl gleichwertig, einkernig
sogar besser; Highway fügt sich ohne Umwege in das Thread- und Arenenmodell der Architektur ein, ist eine
kleine vcpkg-Abhängigkeit ohne LLVM und bietet die SIMD-Auswahl, die U-BCK-02 verlangt. Den Vorsprung von
Halide beim Schärfen hole ich im CpuBackend durch kachelweise Nachbarschaftskernels auf. Das Argument
„GPU mit demselben Code“ betrifft erst Version 1.1 (OE-02) und rechtfertigt den Build-Aufwand jetzt nicht.
