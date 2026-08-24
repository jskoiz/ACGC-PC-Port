"""Deterministic repository-hygiene checks for local disc-image paths."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
DISC_PATH_RE = re.compile(r"(^|/)[^/]+\.(?:iso|gcm|ciso)$", re.IGNORECASE)

DIRECT_RULES = (
    "/*.[iI][sS][oO]",
    "/rom/*.[iI][sS][oO]",
    "/pc/rom/*.[iI][sS][oO]",
    "/orig/*.[iI][sS][oO]",
    "/*.[gG][cC][mM]",
    "/rom/*.[gG][cC][mM]",
    "/pc/rom/*.[gG][cC][mM]",
    "/orig/*.[gG][cC][mM]",
    "/*.[cC][iI][sS][oO]",
    "/rom/*.[cC][iI][sS][oO]",
    "/pc/rom/*.[cC][iI][sS][oO]",
    "/orig/*.[cC][iI][sS][oO]",
)
NESTED_RULES = (
    "**/rom/**/*.[iI][sS][oO]",
    "**/rom/**/*.[gG][cC][mM]",
    "**/rom/**/*.[cC][iI][sS][oO]",
)

POSITIVE_PATHS = (
    "game.ISO",
    "rom/nested/game.GcM",
    "pc/rom/game.cIsO",
    "pc/build32/bin/rom/YourGame.ciso",
    "orig/game.gcm",
)
NEGATIVE_PATH = "pc/build32/bin/not-rom/game.iso"


def _check_ignore(repo_root, path, *, verbose=False):
    command = ["git", "check-ignore", "--no-index"]
    if verbose:
        command.append("--verbose")
    else:
        command.append("--quiet")
    command.extend(("--", path))
    return subprocess.run(
        command,
        cwd=repo_root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def _rule_only_fixture():
    fixture = tempfile.TemporaryDirectory(prefix="acgc-disc-ignore-")
    fixture_root = Path(fixture.name)
    subprocess.run(
        ["git", "init", "--quiet", str(fixture_root)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    (fixture_root / ".gitignore").write_text(
        "\n".join(DIRECT_RULES + NESTED_RULES) + "\n",
        encoding="utf-8",
    )
    return fixture


class DiscIgnoreTest(unittest.TestCase):
    def test_repository_contains_required_rules(self):
        rules = (REPO_ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
        for rule in DIRECT_RULES + NESTED_RULES:
            with self.subTest(rule=rule):
                self.assertIn(rule, rules)

    def test_documented_paths_are_ignored_by_repository(self):
        for path in POSITIVE_PATHS:
            with self.subTest(path=path):
                result = _check_ignore(REPO_ROOT, path)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_rule_only_matrix_rejects_non_rom_path(self):
        fixture = _rule_only_fixture()
        try:
            fixture_root = Path(fixture.name)
            for path in POSITIVE_PATHS:
                with self.subTest(path=path):
                    result = _check_ignore(fixture_root, path)
                    self.assertEqual(result.returncode, 0, result.stderr)

            result = _check_ignore(fixture_root, NEGATIVE_PATH)
            self.assertNotEqual(result.returncode, 0, result.stdout)
        finally:
            fixture.cleanup()

    def test_build32_control_is_not_claimed_by_nested_rule(self):
        result = _check_ignore(REPO_ROOT, NEGATIVE_PATH, verbose=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("build32/", result.stdout)
        self.assertNotIn("**/rom/**/*", result.stdout)

    def test_no_tracked_disc_images(self):
        result = subprocess.run(
            ["git", "ls-files", "--cached", "-z"],
            cwd=REPO_ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        tracked = [path for path in result.stdout.decode().split("\0") if path]
        disc_paths = [path for path in tracked if DISC_PATH_RE.search(path)]
        self.assertEqual(disc_paths, [])


if __name__ == "__main__":
    unittest.main()
