import copy
import hashlib
import io
import json
import os
import stat
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import tools.download_tool as downloader


class FakeResponse:
    def __init__(self, data, content_length=None):
        self._stream = io.BytesIO(data)
        self.headers = {}
        if content_length is not None:
            self.headers["Content-Length"] = str(content_length)

    def read(self, size=-1):
        return self._stream.read(size)

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False


def binary_record(data=b"verified payload", name="fixture.bin"):
    return {
        "asset_id": "fixture",
        "asset_name": name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "asset_url": "https://example.invalid/fixture",
        "kind": "executable",
    }


def archive_bytes(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_STORED) as archive:
        for name, data, mode in entries:
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = mode << 16
            archive.writestr(info, data)
    return stream.getvalue()


def archive_record(data, entries, executable_members=()):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        members = [
            {
                "name": info.filename,
                "mode": oct((info.external_attr >> 16) & 0xFFFF),
                "uncompressed_size": info.file_size,
            }
            for info in infos
        ]
        modes = sorted({member["mode"] for member in members})
        policy = {
            "member_count": len(infos),
            "uncompressed_size": sum(info.file_size for info in infos),
            "compressed_size": sum(info.compress_size for info in infos),
            "allowed_modes": modes,
            "members": members,
            "executable_members": list(executable_members),
        }
    return {
        "asset_id": "fixture-archive",
        "asset_name": "fixture.zip",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "asset_url": "https://example.invalid/fixture.zip",
        "kind": "archive",
        "archive": policy,
    }


class DownloadIntegrityTests(unittest.TestCase):
    def test_manifest_has_all_pinned_public_artifacts_and_headers(self):
        manifest = downloader.load_manifest()
        self.assertEqual(manifest["generated_from"]["ultralib_commit"], "e24c836796df4bf520ff8b11a5c9d2cea3a66cbd")
        self.assertEqual(len(manifest["artifacts"]), 28)
        self.assertEqual(len(manifest["headers"]), 6)
        self.assertIn("generated-verified", {header["policy"] for header in manifest["headers"]})
        self.assertEqual(
            downloader.select_artifact("dtk", "v1.6.2", system="darwin", machine="arm64")["asset_name"],
            "dtk-macos-arm64",
        )

    def test_malformed_manifest_paths_and_types_fail_closed(self):
        cases = [
            ("root must be an object", lambda _manifest: []),
            (
                "asset id cannot escape cache",
                lambda manifest: manifest["artifacts"][0].__setitem__("asset_id", "../../escape"),
            ),
            (
                "header path must stay relative",
                lambda manifest: manifest["headers"][0].__setitem__("path", "../escape.h"),
            ),
            (
                "header path must be a string",
                lambda manifest: manifest["headers"][0].__setitem__("path", ["escape.h"]),
            ),
            (
                "header URL must be HTTPS",
                lambda manifest: manifest["headers"][0].__setitem__("source_url", "file:///tmp/header.h"),
            ),
            (
                "artifact size must be an integer",
                lambda manifest: manifest["artifacts"][0].__setitem__("size", "large"),
            ),
            (
                "archive member path must be canonical",
                lambda manifest: manifest["artifacts"][0]["archive"]["members"][0].__setitem__(
                    "name", "../escape"
                ),
            ),
        ]
        for label, mutate in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temp:
                manifest = copy.deepcopy(downloader.load_manifest())
                mutated = mutate(manifest)
                if mutated is not None:
                    manifest = mutated
                path = Path(temp) / "manifest.json"
                path.write_text(json.dumps(manifest))
                with self.assertRaises(downloader.ManifestError):
                    downloader.load_manifest(path)

    def test_linux_aarch64_selects_manifest_assets(self):
        expected = {
            "binutils": ("2.42-1", "linux-aarch64.zip"),
            "dtk": ("v1.6.2", "dtk-linux-aarch64"),
            "objdiff-cli": ("v3.0.0-beta.14", "objdiff-cli-linux-aarch64"),
            "orthrus": ("v0.2.0", "orthrus-linux-aarch64"),
        }
        for tool, (tag, asset_name) in expected.items():
            with self.subTest(tool=tool):
                self.assertEqual(
                    downloader.select_artifact(
                        tool,
                        tag,
                        system="linux",
                        machine="aarch64",
                    )["asset_name"],
                    asset_name,
                )
        self.assertEqual(
            downloader.select_artifact(
                "dtk",
                "v1.6.2",
                system="darwin",
                machine="arm64",
            )["asset_name"],
            "dtk-macos-arm64",
        )

    def test_tag_is_not_a_url_selector(self):
        with self.assertRaises(downloader.DownloadError):
            downloader.select_artifact("dtk", "main", system="darwin", machine="arm64")

    def test_unsupported_darwin_orthrus_fails_before_network(self):
        with mock.patch.object(downloader.urllib.request, "urlopen") as urlopen:
            with self.assertRaises(downloader.UnsupportedArtifact):
                downloader.download_artifact(
                    "orthrus",
                    Path("unused"),
                    "v0.2.0",
                    system="darwin",
                    machine="arm64",
                )
            urlopen.assert_not_called()

    def test_verified_cache_and_offline_zero_network(self):
        data = b"cache bytes"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp) / "cache"
            cache.mkdir()
            cache_path = downloader._cache_path(cache, record)
            cache_path.write_bytes(data)
            with mock.patch.object(downloader.urllib.request, "urlopen") as urlopen:
                found = downloader._obtain_verified_source(
                    record, cache_dir=cache, offline=True
                )
            self.assertEqual(found, cache_path)
            urlopen.assert_not_called()

    def test_invalid_cache_is_rejected_offline_and_explicit_source_is_verified(self):
        data = b"known source"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp) / "cache"
            cache.mkdir()
            downloader._cache_path(cache, record).write_bytes(b"tampered")
            with self.assertRaises(downloader.IntegrityError):
                downloader._obtain_verified_source(record, cache_dir=cache, offline=True)
            source = Path(temp) / "source"
            source.write_bytes(data)
            self.assertEqual(
                downloader._obtain_verified_source(
                    record, cache_dir=cache, offline=True, explicit_source=source
                ),
                source,
            )

    def test_stream_digest_size_and_atomic_output(self):
        data = b"streamed bytes"
        record = binary_record(data)
        with self.assertRaises(downloader.IntegrityError):
            downloader._copy_stream(io.BytesIO(b"short"), io.BytesIO(), expected_size=10)
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp) / "cache"
            bad = FakeResponse(data[:-1], len(data) - 1)
            with mock.patch.object(downloader.urllib.request, "urlopen", return_value=bad):
                with self.assertRaises(downloader.IntegrityError):
                    downloader._download_to_cache(record, downloader._cache_path(cache, record))
            self.assertFalse(downloader._cache_path(cache, record).exists())

            good = FakeResponse(data, len(data))
            with mock.patch.object(downloader.urllib.request, "urlopen", return_value=good):
                cached = downloader._download_to_cache(record, downloader._cache_path(cache, record))
            self.assertEqual(cached.read_bytes(), data)

            source = Path(temp) / "source"
            source.write_bytes(b"wrong")
            output = Path(temp) / "output"
            output.write_bytes(b"old verified output")
            with self.assertRaises(downloader.IntegrityError):
                downloader._materialize_executable(source, output, record)
            self.assertEqual(output.read_bytes(), b"old verified output")

    def test_temporary_destination_substitution_is_rejected_without_target_write(self):
        data = b"descriptor-backed payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cache = root / "cache"
            cache.mkdir()
            victim = root / "victim"
            victim.write_bytes(b"keep")
            original_temporary_path = downloader._temporary_path_at

            def substitute_temporary(parent_fd, prefix, **kwargs):
                descriptor, name = original_temporary_path(parent_fd, prefix, **kwargs)
                path = cache / name
                path.unlink()
                path.symlink_to(victim)
                return descriptor, name

            cache_path = downloader._cache_path(cache, record)
            response = FakeResponse(data, len(data))
            with mock.patch.object(
                downloader, "_temporary_path_at", side_effect=substitute_temporary
            ):
                with mock.patch.object(downloader.urllib.request, "urlopen", return_value=response):
                    with self.assertRaises(downloader.IntegrityError):
                        downloader._download_to_cache(record, cache_path)
            self.assertEqual(victim.read_bytes(), b"keep")
            self.assertFalse(cache_path.exists())
            self.assertEqual(list(cache.iterdir()), [])

    def test_temporary_destination_substitution_after_assertion_is_rejected(self):
        data = b"post-assertion payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cache = root / "cache"
            cache.mkdir()
            victim = root / "victim"
            victim.write_bytes(b"keep")
            original_temporary_path = downloader._temporary_path_at
            captured = {}

            def capture_temporary(parent_fd, prefix, **kwargs):
                descriptor, name = original_temporary_path(parent_fd, prefix, **kwargs)
                captured["name"] = name
                return descriptor, name

            real_rename = os.rename

            def substitute_after_assertion(
                name, destination, *, src_dir_fd=None, dst_dir_fd=None
            ):
                self.assertEqual(name, captured["name"])
                path = cache / name
                path.unlink()
                path.symlink_to(victim)
                return real_rename(
                    name,
                    destination,
                    src_dir_fd=src_dir_fd,
                    dst_dir_fd=dst_dir_fd,
                )

            cache_path = downloader._cache_path(cache, record)
            response = FakeResponse(data, len(data))
            with mock.patch.object(
                downloader, "_temporary_path_at", side_effect=capture_temporary
            ):
                with mock.patch.object(
                    downloader.os, "rename", side_effect=substitute_after_assertion
                ):
                    with mock.patch.object(downloader.urllib.request, "urlopen", return_value=response):
                        with self.assertRaises(downloader.IntegrityError):
                            downloader._download_to_cache(record, cache_path)
            self.assertEqual(victim.read_bytes(), b"keep")
            self.assertFalse(cache_path.exists())
            self.assertEqual(list(cache.iterdir()), [])

    def test_cache_parent_substitution_stays_anchored(self):
        data = b"cache parent payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cache = root / "cache"
            moved = root / "cache-original"
            victim = root / "victim"
            cache.mkdir()
            victim.mkdir()
            cache_path = downloader._cache_path(cache, record)
            original_open_directory = downloader._open_directory_path

            def substitute_parent(path, *, create):
                descriptor = original_open_directory(path, create=create)
                if Path(path) == cache:
                    cache.rename(moved)
                    cache.symlink_to(victim, target_is_directory=True)
                return descriptor

            response = FakeResponse(data, len(data))
            with mock.patch.object(
                downloader, "_open_directory_path", side_effect=substitute_parent
            ):
                with mock.patch.object(downloader.urllib.request, "urlopen", return_value=response):
                    downloader._download_to_cache(record, cache_path)
            self.assertEqual((moved / cache_path.name).read_bytes(), data)
            self.assertEqual(list(victim.iterdir()), [])

    def test_executable_parent_substitution_stays_anchored(self):
        data = b"executable parent payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            source.write_bytes(data)
            output_parent = root / "output"
            moved = root / "output-original"
            victim = root / "victim"
            output_parent.mkdir()
            victim.mkdir()
            output = output_parent / "tool"
            original_open_directory = downloader._open_directory_path

            def substitute_parent(path, *, create):
                descriptor = original_open_directory(path, create=create)
                if Path(path) == output_parent:
                    output_parent.rename(moved)
                    output_parent.symlink_to(victim, target_is_directory=True)
                return descriptor

            with mock.patch.object(
                downloader, "_open_directory_path", side_effect=substitute_parent
            ):
                downloader._materialize_executable(source, output, record)
            self.assertEqual((moved / output.name).read_bytes(), data)
            self.assertEqual(list(victim.iterdir()), [])

    def test_archive_verified_source_parent_substitution_fails_closed(self):
        data = archive_bytes([("tool", b"tool", 0o100644)])
        record = archive_record(data, [("tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.zip"
            source.write_bytes(data)
            output_parent = root / "output"
            moved = root / "output-original"
            victim = root / "victim"
            output_parent.mkdir()
            victim.mkdir()
            output = output_parent / "tools"
            original_open_directory = downloader._open_directory_path

            def substitute_parent(path, *, create):
                descriptor = original_open_directory(path, create=create)
                if Path(path) == output_parent:
                    output_parent.rename(moved)
                    output_parent.symlink_to(victim, target_is_directory=True)
                return descriptor

            with mock.patch.object(
                downloader, "_open_directory_path", side_effect=substitute_parent
            ):
                with self.assertRaises(downloader.IntegrityError):
                    downloader._materialize_archive(source, output, record)
            self.assertEqual(list(victim.iterdir()), [])
            self.assertFalse((moved / output.name).exists())

    def test_generated_header_root_substitution_stays_anchored(self):
        source_data = b"unsigned char param:8;\n"
        final_data = b"unsigned int\tparam:8;\n"
        header = {
            "path": "include/compiler/gcc/stdlib.h",
            "policy": "generated-verified",
            "source_url": "https://example.invalid/stdlib.h",
            "source_size": len(source_data),
            "source_sha256": hashlib.sha256(source_data).hexdigest(),
            "final_size": len(final_data),
            "final_sha256": hashlib.sha256(final_data).hexdigest(),
            "gbi_patch_applied": True,
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / "project"
            moved = root / "project-original"
            victim = root / "victim"
            project.mkdir()
            victim.mkdir()
            explicit = root / "source.h"
            explicit.write_bytes(source_data)
            original_open_directory = downloader._open_directory_path

            def substitute_root(path, *, create):
                descriptor = original_open_directory(path, create=create)
                if Path(path) == project:
                    project.rename(moved)
                    project.symlink_to(victim, target_is_directory=True)
                return descriptor

            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [header]}):
                with mock.patch.object(
                    downloader, "_open_directory_path", side_effect=substitute_root
                ):
                    downloader.ensure_headers(
                        project,
                        offline=True,
                        explicit_paths={header["path"]: explicit},
                    )
            self.assertEqual(
                (moved / header["path"]).read_bytes(),
                final_data,
            )
            self.assertEqual(list(victim.iterdir()), [])

    def test_tracked_preserve_root_substitution_stays_anchored(self):
        header = {
            "path": "include/PR/gbi.h",
            "policy": "tracked-preserve",
            "source_url": "https://example.invalid/gbi.h",
            "source_size": 1,
            "source_sha256": hashlib.sha256(b"x").hexdigest(),
            "final_size": 1,
            "final_sha256": hashlib.sha256(b"x").hexdigest(),
            "gbi_patch_applied": False,
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / "project"
            moved = root / "project-original"
            victim = root / "victim"
            tracked = project / header["path"]
            tracked.parent.mkdir(parents=True)
            tracked.write_bytes(b"custom PC header")
            victim.mkdir()
            original_open_directory = downloader._open_directory_path

            def substitute_root(path, *, create):
                descriptor = original_open_directory(path, create=create)
                if Path(path) == project:
                    project.rename(moved)
                    project.symlink_to(victim, target_is_directory=True)
                return descriptor

            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [header]}):
                with mock.patch.object(
                    downloader, "_open_directory_path", side_effect=substitute_root
                ):
                    downloader.ensure_headers(project, offline=True)
            self.assertEqual((moved / header["path"]).read_bytes(), b"custom PC header")
            self.assertEqual(list(victim.iterdir()), [])

    def test_directory_alias_allows_only_exact_system_mapping(self):
        with tempfile.TemporaryDirectory(dir="/tmp") as temp:
            exact = Path(temp)
            descriptor = downloader._open_directory_path(exact, create=False)
            os.close(descriptor)

            target = exact / "target"
            target.mkdir()
            nested = exact / "nested"
            nested.symlink_to(target, target_is_directory=True)
            with self.assertRaises(downloader.IntegrityError):
                downloader._open_directory_path(nested / "child", create=True)
            self.assertFalse((target / "child").exists())

    def test_valid_archive_extracts_only_allowlisted_members_and_modes(self):
        data = archive_bytes(
            [("bin/", b"", 0o40755), ("bin/tool", b"tool", 0o100644)]
        )
        record = archive_record(data, [("bin/", b"", 0o40755), ("bin/tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "tools"
            source = Path(temp) / "fixture.zip"
            source.write_bytes(data)
            downloader._materialize_archive(source, output, record)
            self.assertEqual((output / "bin/tool").read_bytes(), b"tool")
            self.assertEqual(stat.S_IMODE((output / "bin/tool").stat().st_mode), 0o644)

    def test_archive_marks_only_explicit_executable_members(self):
        data = archive_bytes([("tool", b"tool", 0o100644), ("note", b"note", 0o100644)])
        record = archive_record(data, [("tool", b"tool", 0o100644), ("note", b"note", 0o100644)], ["tool"])
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "fixture.zip"
            output = Path(temp) / "tools"
            source.write_bytes(data)
            downloader._materialize_archive(source, output, record)
            self.assertEqual(stat.S_IMODE((output / "tool").stat().st_mode), 0o755)
            self.assertEqual(stat.S_IMODE((output / "note").stat().st_mode), 0o644)

    def test_archive_member_symlink_substitution_is_rejected(self):
        data = archive_bytes([("tool", b"tool", 0o100644)])
        record = archive_record(data, [("tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.zip"
            source.write_bytes(data)
            output = root / "tools"
            output.mkdir()
            (output / "old-tool").write_bytes(b"keep")
            victim = root / "victim"
            victim.write_bytes(b"keep-victim")
            original_make_directory = downloader._make_directory_at

            def inject_member_symlink(parent_fd, prefix):
                name, descriptor = original_make_directory(parent_fd, prefix)
                if prefix == f".{output.name}.new-":
                    (output.parent / name / "tool").symlink_to(victim)
                return name, descriptor

            with mock.patch.object(
                downloader, "_make_directory_at", side_effect=inject_member_symlink
            ):
                with self.assertRaises(downloader.IntegrityError):
                    downloader._materialize_archive(source, output, record)
            self.assertEqual((output / "old-tool").read_bytes(), b"keep")
            self.assertEqual(victim.read_bytes(), b"keep-victim")

    def test_archive_parent_symlink_substitution_cannot_escape_staging(self):
        data = archive_bytes([("bin/tool", b"tool", 0o100644)])
        record = archive_record(data, [("bin/tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.zip"
            source.write_bytes(data)
            output = root / "tools"
            output.mkdir()
            (output / "old-tool").write_bytes(b"keep")
            victim = root / "victim-dir"
            victim.mkdir()
            original_make_directory = downloader._make_directory_at

            def inject_parent_symlink(parent_fd, prefix):
                name, descriptor = original_make_directory(parent_fd, prefix)
                if prefix == f".{output.name}.new-":
                    (output.parent / name / "bin").symlink_to(victim, target_is_directory=True)
                return name, descriptor

            with mock.patch.object(
                downloader, "_make_directory_at", side_effect=inject_parent_symlink
            ):
                with self.assertRaises(downloader.IntegrityError):
                    downloader._materialize_archive(source, output, record)
            self.assertEqual((output / "old-tool").read_bytes(), b"keep")
            self.assertEqual(list(victim.iterdir()), [])

    def test_archive_parent_substitution_after_open_stays_unpublished(self):
        data = archive_bytes([("bin/tool", b"tool", 0o100644)])
        record = archive_record(data, [("bin/tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.zip"
            source.write_bytes(data)
            output = root / "tools"
            output.mkdir()
            (output / "old-tool").write_bytes(b"keep")
            victim = root / "victim-dir"
            victim.mkdir()
            original_make_directory = downloader._make_directory_at
            captured = {}

            def capture_staged(parent_fd, prefix):
                name, descriptor = original_make_directory(parent_fd, prefix)
                if prefix == f".{output.name}.new-":
                    captured["staged"] = output.parent / name
                return name, descriptor

            real_open = os.open
            swapped = False

            def substitute_after_parent_open(path, flags, mode=0o777, *, dir_fd=None):
                nonlocal swapped
                if path == "tool" and dir_fd is not None and not swapped:
                    parent = captured["staged"] / "bin"
                    parent.rmdir()
                    parent.symlink_to(victim, target_is_directory=True)
                    swapped = True
                if dir_fd is None:
                    return real_open(path, flags, mode)
                return real_open(path, flags, mode, dir_fd=dir_fd)

            with mock.patch.object(downloader, "_make_directory_at", side_effect=capture_staged):
                with mock.patch.object(downloader, "_archive_dirfd_supported", return_value=True):
                    with mock.patch.object(downloader.os, "open", side_effect=substitute_after_parent_open):
                        with self.assertRaises(downloader.IntegrityError):
                            downloader._materialize_archive(source, output, record)
            self.assertTrue(swapped)
            self.assertEqual((output / "old-tool").read_bytes(), b"keep")
            self.assertEqual(list(victim.iterdir()), [])

    def test_archive_unsupported_dirfd_fails_before_stage_or_member_creation(self):
        data = archive_bytes([("tool", b"tool", 0o100644)])
        record = archive_record(data, [("tool", b"tool", 0o100644)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.zip"
            source.write_bytes(data)
            output = root / "tools"
            with mock.patch.object(downloader, "_archive_dirfd_supported", return_value=False):
                with mock.patch.object(
                    downloader.tempfile,
                    "mkdtemp",
                    side_effect=AssertionError("unsupported archive must not create a stage"),
                ):
                    with self.assertRaises(downloader.IntegrityError):
                        downloader._materialize_archive(source, output, record)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.iterdir()), [source])

    def test_unsupported_dirfd_fails_before_download_or_executable_mutation(self):
        data = b"unsupported platform payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cache_path = root / "cache" / "artifact"
            source = root / "source"
            source.write_bytes(data)
            output = root / "output" / "tool"
            with mock.patch.object(downloader, "_archive_dirfd_supported", return_value=False):
                with mock.patch.object(
                    downloader.urllib.request,
                    "urlopen",
                    side_effect=AssertionError("unsupported download must not access network"),
                ):
                    with self.assertRaises(downloader.IntegrityError):
                        downloader._download_to_cache(record, cache_path)
                with self.assertRaises(downloader.IntegrityError):
                    downloader._materialize_executable(source, output, record)
            self.assertFalse(cache_path.parent.exists())
            self.assertFalse(output.parent.exists())

    def test_recursive_validation_rejects_directory_substitution_before_scan(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            staged = root / "staged"
            nested = staged / "nested"
            nested.mkdir(parents=True)
            (nested / "member").write_bytes(b"keep")
            victim = root / "victim"
            victim.mkdir()
            (victim / "outside").write_bytes(b"outside")
            root_fd = downloader._open_archive_directory_fd(staged, ())
            real_open = os.open
            swapped = False

            def substitute_before_recursive_open(path, flags, mode=0o777, *, dir_fd=None):
                nonlocal swapped
                if path == "nested" and not swapped:
                    (nested / "member").unlink()
                    nested.rmdir()
                    nested.symlink_to(victim, target_is_directory=True)
                    swapped = True
                if dir_fd is None:
                    return real_open(path, flags, mode)
                return real_open(path, flags, mode, dir_fd=dir_fd)

            try:
                with mock.patch.object(downloader, "_archive_dirfd_supported", return_value=True):
                    with mock.patch.object(
                        downloader.os,
                        "open",
                        side_effect=substitute_before_recursive_open,
                    ):
                        with self.assertRaises(downloader.IntegrityError):
                            downloader._validate_staged_tree(staged, root_fd=root_fd)
            finally:
                os.close(root_fd)
            self.assertTrue(swapped)
            self.assertEqual((victim / "outside").read_bytes(), b"outside")

    def test_generated_header_unsupported_dirfd_fails_before_publication(self):
        source = b"unsigned char param:8;\n"
        final = b"unsigned int\tparam:8;\n"
        header = {
            "path": "include/compiler/gcc/stdlib.h",
            "policy": "generated-verified",
            "source_url": "https://example.invalid/stdlib.h",
            "source_size": len(source),
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "final_size": len(final),
            "final_sha256": hashlib.sha256(final).hexdigest(),
            "gbi_patch_applied": True,
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            explicit = root / "source.h"
            explicit.write_bytes(source)
            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [header]}):
                with mock.patch.object(downloader, "_archive_dirfd_supported", return_value=False):
                    with self.assertRaises(downloader.IntegrityError):
                        downloader.ensure_headers(
                            root,
                            offline=True,
                            explicit_paths={header["path"]: explicit},
                        )
            self.assertFalse((root / header["path"]).exists())
            self.assertFalse((root / "include").exists())

    def test_generated_header_parent_symlink_is_rejected_without_publication(self):
        source = b"unsigned char param:8;\n"
        header = {
            "path": "include/compiler/gcc/stdlib.h",
            "policy": "generated-verified",
            "source_url": "https://example.invalid/stdlib.h",
            "source_size": len(source),
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "final_size": len(source),
            "final_sha256": hashlib.sha256(source).hexdigest(),
            "gbi_patch_applied": False,
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            victim = root / "victim"
            victim.mkdir()
            (root / "include").symlink_to(victim, target_is_directory=True)
            explicit = root / "source.h"
            explicit.write_bytes(source)
            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [header]}):
                with self.assertRaises(downloader.IntegrityError):
                    downloader.ensure_headers(
                        root,
                        offline=True,
                        explicit_paths={header["path"]: explicit},
                    )
            self.assertEqual(list(victim.iterdir()), [])

    def test_archive_rejects_source_mutation_and_preserves_existing_output(self):
        entries = [("bin/", b"", 0o40755), ("bin/tool", b"tool", 0o100644)]
        original_data = archive_bytes(entries)
        mutated_data = archive_bytes(
            [("bin/", b"", 0o40755), ("bin/tool", b"evil", 0o100644)]
        )
        self.assertEqual(len(mutated_data), len(original_data))
        record = archive_record(original_data, entries)

        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "fixture.zip"
            source.write_bytes(original_data)
            output = Path(temp) / "tools"
            output.mkdir()
            (output / "old-tool").write_bytes(b"keep")

            original_verify = downloader._verify_file

            def verify_then_mutate(path, current_record):
                original_verify(path, current_record)
                path.write_bytes(mutated_data)

            with mock.patch.object(
                downloader, "_verify_file", side_effect=verify_then_mutate
            ):
                verified = downloader._obtain_verified_source(
                    record,
                    cache_dir=Path(temp) / "cache",
                    offline=True,
                    explicit_source=source,
                )
                with self.assertRaises(downloader.IntegrityError):
                    downloader._materialize_archive(verified, output, record)

            self.assertEqual((output / "old-tool").read_bytes(), b"keep")
            self.assertFalse((output / "bin/tool").exists())

    def test_archive_rejects_unsafe_names_unexpected_members_and_special_modes(self):
        cases = [
            ("../escape", 0o100644),
            ("..\\escape", 0o100644),
            ("/escape", 0o100644),
            ("C:/escape", 0o100644),
            ("symlink", stat.S_IFLNK | 0o777),
            ("special", stat.S_IFCHR | 0o600),
        ]
        for name, mode in cases:
            with self.subTest(name=name):
                data = archive_bytes([(name, b"x", mode)])
                record = archive_record(data, [(name, b"x", mode)])
                with zipfile.ZipFile(io.BytesIO(data)) as archive:
                    with self.assertRaises(downloader.IntegrityError):
                        downloader._validate_archive(archive, record)

        data = archive_bytes([("expected", b"x", 0o100644)])
        record = archive_record(data, [("expected", b"x", 0o100644)])
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            with self.assertRaises(downloader.IntegrityError):
                downloader._validate_archive(archive, {**record, "archive": {**record["archive"], "members": [{"name": "other", "mode": "0o100644", "uncompressed_size": 1}]}})

    def test_archive_rejects_duplicate_and_oversize_members(self):
        duplicate_data = archive_bytes([("same", b"a", 0o100644), ("same", b"b", 0o100644)])
        duplicate_record = archive_record(duplicate_data, [("same", b"a", 0o100644), ("same", b"b", 0o100644)])
        with zipfile.ZipFile(io.BytesIO(duplicate_data)) as archive:
            with self.assertRaises(downloader.IntegrityError):
                downloader._validate_archive(archive, duplicate_record)

        info = zipfile.ZipInfo("large")
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        info.file_size = downloader.MAX_MEMBER_BYTES + 1

        class FakeZip:
            def infolist(self):
                return [info]

        record = {
            "archive": {
                "member_count": 1,
                "uncompressed_size": info.file_size,
                "compressed_size": 0,
                "allowed_modes": ["0o100644"],
                "members": [{"name": "large", "mode": "0o100644", "uncompressed_size": info.file_size}],
            }
        }
        with self.assertRaises(downloader.IntegrityError):
            downloader._validate_archive(FakeZip(), record)

    def test_header_policies_preserve_tracked_and_verify_generated_atomic(self):
        tracked = {
            "path": "include/PR/gbi.h",
            "policy": "tracked-preserve",
            "source_url": "https://example.invalid/gbi.h",
            "source_size": 1,
            "source_sha256": hashlib.sha256(b"x").hexdigest(),
            "final_size": 1,
            "final_sha256": hashlib.sha256(b"x").hexdigest(),
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / tracked["path"]
            path.parent.mkdir(parents=True)
            path.write_bytes(b"custom PC header")
            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [tracked]}):
                downloader.ensure_headers(root, offline=True)
            self.assertEqual(path.read_bytes(), b"custom PC header")

            path.unlink()
            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [tracked]}):
                with self.assertRaises(downloader.IntegrityError):
                    downloader.ensure_headers(root, offline=True)

    def test_generated_header_transform_uses_verified_explicit_source(self):
        source = b"unsigned char param:8;\n"
        final = b"unsigned int\tparam:8;\n"
        header = {
            "path": "include/compiler/gcc/stdlib.h",
            "policy": "generated-verified",
            "source_url": "https://example.invalid/stdlib.h",
            "source_size": len(source),
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "final_size": len(final),
            "final_sha256": hashlib.sha256(final).hexdigest(),
            "gbi_patch_applied": True,
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            explicit = root / "source.h"
            explicit.write_bytes(source)
            with mock.patch.object(downloader, "load_manifest", return_value={"headers": [header]}):
                downloader.ensure_headers(
                    root,
                    offline=True,
                    explicit_paths={header["path"]: explicit},
                )
            self.assertEqual((root / header["path"]).read_bytes(), final)

    def test_auth_is_not_read_or_logged(self):
        data = b"payload"
        record = binary_record(data)
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch.dict(os.environ, {"GITHUB_TOKEN": "secret-token"}, clear=False):
                with mock.patch.object(
                    downloader.urllib.request,
                    "urlopen",
                    side_effect=OSError("network disabled"),
                ) as urlopen:
                    with self.assertRaises(downloader.DownloadError) as raised:
                        downloader._download_to_cache(record, Path(temp) / "cache")
            self.assertNotIn("secret-token", str(raised.exception))
            request = urlopen.call_args.args[0]
            self.assertNotIn("secret-token", request.full_url)


if __name__ == "__main__":
    unittest.main()
