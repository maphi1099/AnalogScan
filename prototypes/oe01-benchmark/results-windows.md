# OE-01 Messung Windows 11, clang-cl 23.1.3, 2026-10-22 14:54

Highway-Ziel: AVX2, Threads: 1 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 1 | 142.6 | 19.2 | 38.3 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 33f82a199f703f9e | 1e342beaa9d694de |
| Gradation (Tabelle 4096) | 1 | 68.0 | 23.0 | 32.6 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | e48578fa604022fc | 1759ea43270a2592 |
| Basis + Gradation verschmolzen | 1 | 234.6 | 52.1 | 83.5 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e2a9cb9aacd5b7aa | 037e27437cb9e9d8 |
| Schärfen (USM, r=4) | 1 | 382.8 | 47.0 | 81.3 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 3f49285324000fae | 6499c467391de539 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 1 | 1702.1 | 224.5 | 457.8 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 9b960a20bc2865c6 | f7f6647a7260e311 |
| Gradation (Tabelle 4096) | 1 | 814.7 | 284.0 | 392.7 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 017ad9d8ea09a361 | f3a7fe27103e247f |
| Basis + Gradation verschmolzen | 1 | 2985.9 | 654.4 | 1132.3 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | d767e2c6c7729d12 | a473ef7b3304cad6 |
| Schärfen (USM, r=4) | 1 | 4806.5 | 590.6 | 1034.4 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | eedeafa688a31e01 | 8703ee6c3fa1febf |

Highway-Ziel: AVX2, Threads: 8 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 8 | 19.1 | 9.2 | 8.0 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 33f82a199f703f9e | 1e342beaa9d694de |
| Gradation (Tabelle 4096) | 8 | 9.6 | 6.8 | 7.9 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | e48578fa604022fc | 1759ea43270a2592 |
| Basis + Gradation verschmolzen | 8 | 33.1 | 8.0 | 11.6 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e2a9cb9aacd5b7aa | 037e27437cb9e9d8 |
| Schärfen (USM, r=4) | 8 | 53.1 | 22.2 | 22.5 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 3f49285324000fae | 6499c467391de539 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 8 | 333.3 | 98.5 | 105.0 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 9b960a20bc2865c6 | f7f6647a7260e311 |
| Gradation (Tabelle 4096) | 8 | 184.2 | 101.4 | 100.9 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 017ad9d8ea09a361 | f3a7fe27103e247f |
| Basis + Gradation verschmolzen | 8 | 577.9 | 156.4 | 194.5 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | d767e2c6c7729d12 | a473ef7b3304cad6 |
| Schärfen (USM, r=4) | 8 | 919.3 | 333.2 | 365.2 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | eedeafa688a31e01 | 8703ee6c3fa1febf |

Highway-Ziel: AVX2, Threads: 32 von 32, Läufe je Messung: 7 (Median)

## 8 MP (3464 x 2309)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 32 | 12.2 | 7.8 | 8.0 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 33f82a199f703f9e | 1e342beaa9d694de |
| Gradation (Tabelle 4096) | 32 | 8.6 | 7.7 | 8.0 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | e48578fa604022fc | 1759ea43270a2592 |
| Basis + Gradation verschmolzen | 32 | 18.8 | 8.5 | 8.2 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | e2a9cb9aacd5b7aa | 037e27437cb9e9d8 |
| Schärfen (USM, r=4) | 32 | 33.2 | 27.9 | 24.1 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | 3f49285324000fae | 6499c467391de539 |

## 100 MP (12248 x 8165)

| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) | Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |
|---|---|---|---|---|---|---|---|---|
| Basiskorrektur (log10) | 32 | 117.8 | 105.0 | 106.9 | 1.2e-07 / 0.0000% | 1.8e-07 / 0.0000% | 9b960a20bc2865c6 | f7f6647a7260e311 |
| Gradation (Tabelle 4096) | 32 | 109.3 | 105.9 | 105.0 | 6.0e-08 / 0.0000% | 0.0e+00 / 0.0000% | 017ad9d8ea09a361 | f3a7fe27103e247f |
| Basis + Gradation verschmolzen | 32 | 196.3 | 105.3 | 103.8 | 2.4e-07 / 0.0000% | 3.6e-07 / 0.0000% | d767e2c6c7729d12 | a473ef7b3304cad6 |
| Schärfen (USM, r=4) | 32 | 365.2 | 346.6 | 303.8 | 1.2e-03 / 0.0000% | 1.2e-03 / 0.0000% | eedeafa688a31e01 | 8703ee6c3fa1febf |

