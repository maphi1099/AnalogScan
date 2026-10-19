"""Selbsttest der Architekturprüfung (NF-ARC-01 bis -03).

Jeder Test baut einen kleinen Quellbaum in einem temporären Verzeichnis und eine
passende targets.json und prüft, dass ein absichtlicher Verstoß erkannt wird
bzw. ein sauberer Baum besteht. Verwendet die echte Regeldatei des Projekts.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(os.environ.get("AUTONEG_TOOLS_DIR", Path(__file__).resolve().parents[2] / "tools"))
sys.path.insert(0, str(TOOLS_DIR))

import check_architecture  # noqa: E402

RULES = TOOLS_DIR / "architecture_rules.json"

# Saubere Grundausstattung: Modul -> (Quellordner, Abhängigkeiten als Zielnamen)
BASE_TARGETS = {
    "base": ("src/base", []),
    "model": ("src/model", ["autoneg_base"]),
    "image": ("src/image", ["autoneg_base"]),
    "layout": ("src/layout", ["autoneg_base", "autoneg_model"]),
    "ports": ("src/ports", ["autoneg_base", "autoneg_model", "autoneg_image", "autoneg_layout"]),
    "pipeline": ("src/pipeline", ["autoneg_base", "autoneg_model", "autoneg_image", "autoneg_ports"]),
    "application": ("src/application", ["autoneg_pipeline", "autoneg_ports"]),
    "adapter_opencv": ("src/adapters/opencv", ["autoneg_ports", "opencv_core"]),
    "autoneg-cli": ("apps/autoneg-cli", ["autoneg_application", "autoneg_adapter_opencv"]),
}


class ArchitectureCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.targets = {name: [src, list(deps)] for name, (src, deps) in BASE_TARGETS.items()}
        for module, (src, _) in BASE_TARGETS.items():
            self.write(f"{src}/module.cpp", "#include <string_view>\n")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def write(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def run_check(self, rules_path: Path = RULES) -> list[str]:
        targets = [
            {"module": m, "target": m if m.startswith("autoneg") else f"autoneg_{m}",
             "type": "STATIC_LIBRARY", "sourceDir": src, "deps": deps}
            for m, (src, deps) in self.targets.items()
        ]
        targets_path = self.root / "targets.json"
        targets_path.write_text(json.dumps({"targets": targets}), encoding="utf-8")
        return check_architecture.run(self.root, rules_path, targets_path).errors

    def assertViolation(self, errors: list[str], tag: str, fragment: str) -> None:
        hits = [e for e in errors if e.startswith(tag) and fragment in e]
        self.assertTrue(hits, f"erwarteter Verstoß {tag} '{fragment}' nicht gemeldet; gemeldet: {errors}")

    # --- sauberer Baum ---------------------------------------------------------

    def test_clean_tree_passes(self) -> None:
        self.write("src/model/edit_list.cpp", '#include "autoneg/model/module.hpp"\n#include <vector>\n')
        self.write("src/adapters/opencv/vision.cpp", "#include <opencv2/core.hpp>\n#include <unistd.h>\n")
        self.write("tests/unit/model/x_test.cpp", "#include <gtest/gtest.h>\n#include \"autoneg/model/module.hpp\"\n")
        self.assertEqual(self.run_check(), [])

    # --- Abhängigkeiten ---------------------------------------------------------

    def test_forbidden_dependency_is_reported(self) -> None:
        self.targets["model"][1].append("autoneg_ports")  # model -> ports verboten
        errors = self.run_check()
        self.assertViolation(errors, "[Abhängigkeit]", "model -> ports")

    def test_cycle_is_reported(self) -> None:
        self.targets["model"][1].append("autoneg_ports")  # ports -> model besteht bereits
        errors = self.run_check()
        self.assertViolation(errors, "[Zyklus]", "model")

    def test_cycle_is_reported_even_if_both_edges_allowed(self) -> None:
        rules = json.loads(RULES.read_text(encoding="utf-8"))
        rules["modules"]["model"]["deps"].append("image")
        rules["modules"]["image"]["deps"].append("model")
        rules_path = self.root / "rules.json"
        rules_path.write_text(json.dumps(rules), encoding="utf-8")
        self.targets["model"][1].append("autoneg_image")
        self.targets["image"][1].append("autoneg_model")
        errors = self.run_check(rules_path)
        self.assertViolation(errors, "[Zyklus]", "image -> model -> image")
        self.assertFalse([e for e in errors if e.startswith("[Abhängigkeit]")])

    def test_foreign_library_link_in_core_is_reported(self) -> None:
        self.targets["pipeline"][1].append("opencv_core")
        self.assertViolation(self.run_check(), "[Fremdbibliothek]", "pipeline linkt opencv_core")

    def test_unknown_module_is_reported(self) -> None:
        self.targets["gadget"] = ["src/gadget", []]
        self.assertViolation(self.run_check(), "[Modul]", "gadget")

    # --- Includes ------------------------------------------------------------------

    def test_foreign_header_in_core_is_reported(self) -> None:
        self.write("src/model/bad.cpp", "#include <opencv2/core.hpp>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "src/model/bad.cpp:1")

    def test_qt_header_outside_ui_is_reported(self) -> None:
        self.write("src/application/bad.cpp", "#include <vector>\n#include <QString>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "src/application/bad.cpp:2")

    def test_platform_header_in_core_is_reported(self) -> None:
        self.write("src/pipeline/bad.cpp", "#include <windows.h>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "platform")

    def test_gtest_in_production_code_is_reported(self) -> None:
        self.write("src/model/bad.hpp", "#include <gtest/gtest.h>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "gtest")

    def test_foreign_header_in_program_is_reported(self) -> None:
        self.write("apps/autoneg-cli/main.cpp", "#include <opencv2/core.hpp>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "apps/autoneg-cli/main.cpp")

    def test_unknown_header_is_reported(self) -> None:
        self.write("src/model/bad.cpp", "#include <boost/optional.hpp>\n")
        self.assertViolation(self.run_check(), "[Include]", "unbekannter Header <boost/optional.hpp>")

    def test_include_from_non_dependency_module_is_reported(self) -> None:
        self.write("src/model/bad.cpp", '#include "autoneg/ports/i_raw_decoder.hpp"\n')
        self.assertViolation(self.run_check(), "[Include]", "model bindet <autoneg/ports/i_raw_decoder.hpp>")

    def test_test_helpers_allowed_in_tests_only(self) -> None:
        self.write("tests/unit/model/y_test.cpp",
                   '#include "autoneg/mocks/ports.hpp"\n#include "autoneg/testing/edit_fixtures.hpp"\n')
        self.assertEqual(self.run_check(), [])
        self.write("src/model/bad.cpp", '#include "autoneg/mocks/ports.hpp"\n')
        self.assertViolation(self.run_check(), "[Include]", "src/model/bad.cpp:1")

    def test_local_quoted_include_is_allowed(self) -> None:
        self.write("src/model/detail.hpp", "#pragma once\n")
        self.write("src/model/a.cpp", '#include "detail.hpp"\n')
        self.assertEqual(self.run_check(), [])

    def test_vulkan_is_nowhere_allowed(self) -> None:
        self.write("src/adapters/opencv/gpu.cpp", "#include <vulkan/vulkan.h>\n")
        self.assertViolation(self.run_check(), "[Fremd-Header]", "vulkan")


if __name__ == "__main__":
    unittest.main()
