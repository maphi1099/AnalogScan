# OE-01 Messung Linux (WSL 2, Arch), clang version 23.1.3, 2026-10-22 15:10
Highway-Ziel: AVX2, Threads: 1 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 1 | 132.5 | 17.9 | 36.4 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 3e6f1d670aaa8fb4 | 742b456f83a83abd |
| Gradation (Tabelle 4096) | 1 | 35.8 | 22.1 | 31.2 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3eb5ad50c70b8fb5 | 5593ed91fe489d7d |
| Basis + Gradation verschmolzen | 1 | 205.7 | 52.5 | 82.9 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e357a394d69ca9e2 | 8225d134c7f9bb53 |
| Schärfen (USM, r=4) | 1 | 187.2 | 22.8 | 57.8 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 33e54f62fd811223 | ee544a9a2786c091 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 1 | 1645.6 | 221.5 | 454.1 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 8251740d1418f053 | 28283730bc3b20c2 |
| Gradation (Tabelle 4096) | 1 | 459.1 | 280.3 | 401.5 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3be4bc673b4bf199 | 2adce01951207e26 |
| Basis + Gradation verschmolzen | 1 | 2580.3 | 655.4 | 1035.5 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | a84beabebce8ee18 | 89b72328e0063b6a |
| Schärfen (USM, r=4) | 1 | 2434.4 | 309.3 | 740.8 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 8f49a8814a9c76e7 | 835971854af215b8 |

Highway-Ziel: AVX2, Threads: 8 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 8 | 18.2 | 7.2 | 7.5 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 3e6f1d670aaa8fb4 | 742b456f83a83abd |
| Gradation (Tabelle 4096) | 8 | 7.4 | 7.4 | 7.6 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3eb5ad50c70b8fb5 | 5593ed91fe489d7d |
| Basis + Gradation verschmolzen | 8 | 28.7 | 9.1 | 11.8 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e357a394d69ca9e2 | 8225d134c7f9bb53 |
| Schärfen (USM, r=4) | 8 | 26.4 | 13.0 | 12.3 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 33e54f62fd811223 | ee544a9a2786c091 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 8 | 214.6 | 99.0 | 98.4 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 8251740d1418f053 | 28283730bc3b20c2 |
| Gradation (Tabelle 4096) | 8 | 101.7 | 97.5 | 95.4 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3be4bc673b4bf199 | 2adce01951207e26 |
| Basis + Gradation verschmolzen | 8 | 347.4 | 106.1 | 136.0 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | a84beabebce8ee18 | 89b72328e0063b6a |
| Schärfen (USM, r=4) | 8 | 359.8 | 321.7 | 254.5 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 8f49a8814a9c76e7 | 835971854af215b8 |

Highway-Ziel: AVX2, Threads: 32 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 32 | 12.3 | 7.7 | 7.5 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 3e6f1d670aaa8fb4 | 742b456f83a83abd |
| Gradation (Tabelle 4096) | 32 | 8.0 | 7.4 | 7.5 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3eb5ad50c70b8fb5 | 5593ed91fe489d7d |
| Basis + Gradation verschmolzen | 32 | 15.8 | 7.8 | 8.4 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e357a394d69ca9e2 | 8225d134c7f9bb53 |
| Schärfen (USM, r=4) | 32 | 26.9 | 24.7 | 21.8 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 33e54f62fd811223 | ee544a9a2786c091 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 32 | 117.4 | 105.4 | 101.9 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 8251740d1418f053 | 28283730bc3b20c2 |
| Gradation (Tabelle 4096) | 32 | 103.6 | 101.4 | 102.3 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 3be4bc673b4bf199 | 2adce01951207e26 |
| Basis + Gradation verschmolzen | 32 | 145.4 | 100.6 | 102.7 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | a84beabebce8ee18 | 89b72328e0063b6a |
| Schärfen (USM, r=4) | 32 | 357.7 | 351.4 | 284.9 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 8f49a8814a9c76e7 | 835971854af215b8 |

