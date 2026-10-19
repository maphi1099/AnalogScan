#!/usr/bin/env python3
"""Prüfung der Architekturregeln von AutoNeg (NF-ARC-01 bis -03).

Geprüft wird:
  1. Jedes Ziel ist ein bekanntes Modul (tools/architecture_rules.json).
  2. Direkte Link-Abhängigkeiten liegen in der Erlaubnisliste des Moduls;
     Fremdbibliotheken nur in den Modulen, denen sie zugeordnet sind.
  3. Der Abhängigkeitsgraph der eigenen Module ist zyklenfrei.
  4. #include-Anweisungen: Projekt-Header nur aus erlaubten Modulen,
     Fremd-Header (inkl. Qt und Betriebssystem-Header) nur im zugehörigen Adapter,
     unbekannte Header werden abgelehnt.

Eingabe ist die beim Konfigurieren erzeugte Datei <build>/architecture/targets.json.
Rückgabewert 0 = alle Regeln erfüllt, 1 = Verstöße, 2 = Aufruffehler.
Nur Python-Standardbibliothek.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

SOURCE_SUFFIXES = {".hpp", ".h", ".hh", ".hxx", ".cpp", ".cc", ".cxx", ".ipp", ".inl", ".tpp"}

STD_HEADERS = frozenset("""
algorithm any array atomic barrier bit bitset charconv chrono codecvt compare complex concepts
condition_variable coroutine deque exception execution expected filesystem flat_map flat_set format
forward_list fstream functional future generator initializer_list iomanip ios iosfwd iostream
istream iterator latch limits list locale map mdspan memory memory_resource mutex new numbers numeric
optional ostream print queue random ranges ratio regex scoped_allocator semaphore set shared_mutex
source_location span spanstream sstream stack stacktrace stdexcept stdfloat stop_token streambuf
string string_view strstream syncstream system_error thread tuple type_traits typeindex typeinfo
unordered_map unordered_set utility valarray variant vector version
cassert cctype cerrno cfenv cfloat cinttypes climits clocale cmath csetjmp csignal cstdarg cstddef
cstdint cstdio cstdlib cstring ctime cuchar cwchar cwctype
assert.h ctype.h errno.h fenv.h float.h inttypes.h limits.h locale.h math.h setjmp.h signal.h
stdarg.h stddef.h stdint.h stdio.h stdlib.h string.h time.h uchar.h wchar.h wctype.h
""".split())

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)


@dataclass
class Report:
    errors: list[str] = field(default_factory=list)

    def add(self, message: str) -> None:
        self.errors.append(message)


@dataclass
class HeaderGroup:
    name: str
    prefixes: list[str]
    exact: set[str]
    regex: list[re.Pattern[str]]

    def matches(self, header: str) -> bool:
        return (header in self.exact
                or any(header.startswith(p) for p in self.prefixes)
                or any(r.search(header) for r in self.regex))


def load_groups(rules: dict) -> list[HeaderGroup]:
    groups = []
    for name, spec in rules.get("headerGroups", {}).items():
        groups.append(HeaderGroup(
            name=name,
            prefixes=list(spec.get("prefixes", [])),
            exact=set(spec.get("exact", [])),
            regex=[re.compile(r) for r in spec.get("regex", [])],
        ))
    return groups


def find_cycles(graph: dict[str, set[str]]) -> list[list[str]]:
    """Liefert je einen Zyklus pro stark zusammenhängender Komponente (Tiefensuche)."""
    cycles: list[list[str]] = []
    state: dict[str, int] = {}  # 0 unbesucht, 1 auf dem Stapel, 2 fertig
    stack: list[str] = []

    def visit(node: str) -> None:
        state[node] = 1
        stack.append(node)
        for nxt in sorted(graph.get(node, ())):
            if state.get(nxt, 0) == 0:
                visit(nxt)
            elif state.get(nxt) == 1:
                cycles.append(stack[stack.index(nxt):] + [nxt])
        stack.pop()
        state[node] = 2

    for node in sorted(graph):
        if state.get(node, 0) == 0:
            visit(node)
    return cycles


def check_dependencies(rules: dict, targets: list[dict], report: Report) -> dict[str, set[str]]:
    modules = rules["modules"]
    target_to_module = {t["target"]: t["module"] for t in targets}
    graph: dict[str, set[str]] = {}

    for t in targets:
        module = t["module"]
        spec = modules.get(module)
        if spec is None:
            report.add(f"[Modul] Ziel {t['target']}: Modul '{module}' fehlt in architecture_rules.json")
            continue
        allowed = set(spec.get("deps", []))
        external_patterns = [re.compile(p) for p in spec.get("externalTargets", [])]
        graph.setdefault(module, set())
        for dep in t.get("deps", []):
            if dep in target_to_module:
                dep_module = target_to_module[dep]
                graph[module].add(dep_module)
                if "*" not in allowed and dep_module not in allowed:
                    report.add(f"[Abhängigkeit] {module} -> {dep_module} ist nicht erlaubt "
                               f"(erlaubt: {', '.join(sorted(allowed)) or 'keine'})")
            elif dep.startswith("autoneg"):
                report.add(f"[Abhängigkeit] {module} -> {dep}: unbekanntes eigenes Ziel")
            elif not any(p.search(dep) for p in external_patterns):
                report.add(f"[Fremdbibliothek] {module} linkt {dep}; Fremdbibliotheken nur im zugehörigen Adapter")

    for cycle in find_cycles(graph):
        report.add("[Zyklus] " + " -> ".join(cycle))
    return graph


def iter_sources(directory: Path):
    if not directory.is_dir():
        return
    for path in sorted(directory.rglob("*")):
        if path.suffix in SOURCE_SUFFIXES and path.is_file():
            yield path


def project_module(header: str, include_roots: dict[str, str]) -> str | None:
    best = None
    for module, root in include_roots.items():
        if header.startswith(root) and (best is None or len(root) > len(include_roots[best])):
            best = module
    return best


def classify(header: str, quoted: bool, file_dir: Path, include_roots: dict[str, str],
             groups: list[HeaderGroup]) -> tuple[str, str | None]:
    """Ergebnis: (Art, Detail) mit Art in project, std, group, local, unknown."""
    if header.startswith("autoneg/"):
        return "project", project_module(header, include_roots)
    if header in STD_HEADERS:
        return "std", None
    for group in groups:
        if group.matches(header):
            return "group", group.name
    if quoted and (file_dir / header).is_file():
        return "local", None
    return "unknown", None


def check_includes(root: Path, rules: dict, targets: list[dict], report: Report) -> None:
    modules = rules["modules"]
    include_roots = rules.get("includeRoots", {})
    groups = load_groups(rules)

    def check_file(path: Path, owner: str, allowed_modules: set[str] | None,
                   allowed_groups: set[str] | str) -> None:
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(root).as_posix()
        for match in INCLUDE_RE.finditer(text):
            quoted, header = match.group(1) == '"', match.group(2).strip()
            line = text.count("\n", 0, match.start()) + 1
            kind, detail = classify(header, quoted, path.parent, include_roots, groups)
            where = f"{rel}:{line}"
            if kind == "project":
                if detail is None:
                    report.add(f"[Include] {where}: <{header}> gehört zu keinem bekannten Modul")
                elif allowed_modules is not None and detail != owner and detail not in allowed_modules:
                    report.add(f"[Include] {where}: {owner} bindet <{header}> aus Modul {detail} ein "
                               f"(keine erlaubte Abhängigkeit)")
            elif kind == "group":
                if allowed_groups != "*" and detail not in allowed_groups:
                    report.add(f"[Fremd-Header] {where}: <{header}> ({detail}) ist in {owner} nicht erlaubt")
            elif kind == "unknown":
                report.add(f"[Include] {where}: unbekannter Header <{header}> in {owner} "
                           f"(weder Standardbibliothek noch zugeordnete Bibliothek)")

    for t in targets:
        module = t["module"]
        spec = modules.get(module)
        if spec is None:
            continue
        deps = set(spec.get("deps", []))
        allowed_modules = None if "*" in deps else deps
        allowed_groups = set(spec.get("foreign", []))
        for path in iter_sources(root / t["sourceDir"]):
            check_file(path, module, allowed_modules, allowed_groups)

    tests = rules.get("tests", {})
    component_dir = (root / tests["componentDir"]) if "componentDir" in tests else None
    for directory in tests.get("dirs", []):
        for path in iter_sources(root / directory):
            in_component = component_dir is not None and component_dir in path.parents
            groups_allowed: set[str] | str = (tests.get("componentForeign", "*") if in_component
                                              else set(tests.get("foreign", [])))
            check_file(path, "tests", None, groups_allowed)


def run(root: Path, rules_path: Path, targets_path: Path) -> Report:
    rules = json.loads(rules_path.read_text(encoding="utf-8"))
    targets = json.loads(targets_path.read_text(encoding="utf-8"))["targets"]
    report = Report()
    check_dependencies(rules, targets, report)
    check_includes(root, rules, targets, report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Architekturprüfung AutoNeg")
    parser.add_argument("--root", required=True, type=Path, help="Wurzel des Quellbaums")
    parser.add_argument("--rules", required=True, type=Path, help="architecture_rules.json")
    parser.add_argument("--targets", required=True, type=Path, help="<build>/architecture/targets.json")
    args = parser.parse_args(argv)
    # Windows-Konsolen nutzen sonst die ANSI-Codepage (BUGS.md B-03)
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")

    for path in (args.rules, args.targets):
        if not path.is_file():
            print(f"Architekturprüfung: Datei fehlt: {path}", file=sys.stderr)
            return 2

    report = run(args.root.resolve(), args.rules, args.targets)
    if report.errors:
        print(f"Architekturprüfung FEHLGESCHLAGEN ({len(report.errors)} Verstöße):", file=sys.stderr)
        for error in report.errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print("Architekturprüfung bestanden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
