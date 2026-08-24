#!/usr/bin/env python3
"""Pinned, fail-closed acquisition for the public ACGC build tools.

The project generator still invokes this module with the historical
"tool output --tag" interface.  The tag is only checked against the
committed manifest; it is never interpolated into a URL.  Bytes are streamed
to a verified cache, and archives are validated completely before any output
path is replaced.  Nothing downloaded by this module is executed.
"""

from __future__ import annotations

import argparse
try:
    import fcntl
except ImportError:  # pragma: no cover - descriptor support fails closed below.
    fcntl = None
import ctypes
import hashlib
import json
import ntpath
import os
import platform
import re
import secrets
import stat
import tempfile
import urllib.error
import urllib.request
import zipfile
from collections.abc import Mapping as MappingABC
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO, Callable, Dict, Mapping, Optional, Sequence, Tuple


CHUNK_SIZE = 1024 * 1024
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
MAX_MEMBER_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_BYTES = 1024 * 1024 * 1024
MANIFEST_PATH = Path(__file__).with_name("download_manifest.json")
DEFAULT_CACHE_DIR = Path.home() / ".cache" / "acgc-pc-port"


class DownloadError(RuntimeError):
    """Base class for deterministic acquisition failures."""


class ManifestError(DownloadError):
    """The committed manifest is missing, malformed, or incomplete."""


class IntegrityError(DownloadError):
    """A byte stream, cache entry, or archive violated its manifest."""


class UnsupportedArtifact(DownloadError):
    """The current host has no pinned public artifact for a requested tool."""


def _read_manifest_bytes(path: Path) -> bytes:
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_MANIFEST_BYTES + 1)
    except OSError as exc:
        raise ManifestError(f"cannot read manifest {path}: {exc}") from exc
    if len(data) > MAX_MANIFEST_BYTES:
        raise ManifestError("download manifest exceeds the bounded size limit")
    return data


def load_manifest(path: Optional[Path] = None) -> dict:
    manifest_path = Path(path) if path is not None else MANIFEST_PATH
    try:
        manifest = json.loads(_read_manifest_bytes(manifest_path))
    except json.JSONDecodeError as exc:
        raise ManifestError(f"invalid JSON in {manifest_path}: {exc}") from exc

    if not isinstance(manifest, MappingABC):
        raise ManifestError("download manifest root must be an object")
    if manifest.get("schema_version") != 1:
        raise ManifestError("unsupported download manifest schema")
    generated = manifest.get("generated_from", {})
    if not isinstance(generated, MappingABC):
        raise ManifestError("manifest generated_from must be an object")
    for key in ("pc_commit", "decomp_commit", "ultralib_commit"):
        if not isinstance(generated.get(key), str) or not generated[key]:
            raise ManifestError(f"manifest generated_from.{key} is missing")
    snapshot = generated.get("acquisition_snapshot")
    if not isinstance(snapshot, MappingABC):
        raise ManifestError("manifest generated_from.acquisition_snapshot is missing")
    for key in ("source_pc_commit", "integration_base_pc_commit", "candidate_commit_at_snapshot"):
        if not isinstance(snapshot.get(key), str) or not re.fullmatch(r"[0-9a-f]{40}", snapshot[key]):
            raise ManifestError(f"manifest acquisition snapshot {key} is invalid")
    if snapshot["source_pc_commit"] != generated["pc_commit"]:
        raise ManifestError("manifest acquisition snapshot does not match generated_from.pc_commit")
    if snapshot.get("regenerated") is not False:
        raise ManifestError("manifest acquisition snapshot must not claim regeneration")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 28:
        raise ManifestError("manifest must contain the current 28 public artifacts")
    seen_assets = set()
    seen_assets_folded = set()
    for artifact in artifacts:
        if not isinstance(artifact, MappingABC):
            raise ManifestError("artifact entry must be an object")
        required = ("tool", "asset_id", "size", "sha256", "kind", "release_tag", "asset_name", "asset_url")
        if any(key not in artifact for key in required):
            raise ManifestError("artifact entry is missing a required field")
        if not isinstance(artifact["tool"], str) or not artifact["tool"]:
            raise ManifestError("artifact tool must be a non-empty string")
        if artifact["asset_id"] is not None and (
            not isinstance(artifact["asset_id"], (int, str))
            or isinstance(artifact["asset_id"], bool)
            or (isinstance(artifact["asset_id"], str) and not re.fullmatch(r"[A-Za-z0-9._-]+", artifact["asset_id"]))
        ):
            raise ManifestError(f"invalid asset id for {artifact['tool']}")
        if not isinstance(artifact["release_tag"], str) or not artifact["release_tag"]:
            raise ManifestError(f"invalid release tag for {artifact['tool']}")
        if not isinstance(artifact["asset_name"], str) or not re.fullmatch(r"[A-Za-z0-9._-]+", artifact["asset_name"]):
            raise ManifestError(f"invalid asset name for {artifact['tool']}")
        if not isinstance(artifact["asset_url"], str) or not artifact["asset_url"].startswith("https://"):
            raise ManifestError(f"artifact URL must use HTTPS for {artifact['asset_name']}")
        asset_key = (artifact["tool"], artifact["asset_name"])
        if asset_key in seen_assets:
            raise ManifestError(f"duplicate manifest artifact {asset_key}")
        folded_asset_key = (artifact["tool"], artifact["asset_name"].casefold())
        if folded_asset_key in seen_assets_folded:
            raise ManifestError(f"case-folded manifest artifact collision {asset_key}")
        seen_assets.add(asset_key)
        seen_assets_folded.add(folded_asset_key)
        if type(artifact["size"]) is not int or artifact["size"] <= 0:
            raise ManifestError(f"invalid size for {asset_key}")
        if not isinstance(artifact["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]):
            raise ManifestError(f"invalid SHA-256 for {asset_key}")
        if artifact["kind"] not in ("archive", "executable"):
            raise ManifestError(f"unsupported artifact kind for {asset_key}")
        if artifact["kind"] == "archive":
            archive = artifact.get("archive")
            if not isinstance(archive, MappingABC):
                raise ManifestError(f"archive policy missing for {asset_key}")
            members = archive.get("members")
            if (
                not isinstance(members, list)
                or type(archive.get("member_count")) is not int
                or archive["member_count"] < 0
                or len(members) != archive["member_count"]
            ):
                raise ManifestError(f"archive allowlist incomplete for {asset_key}")
            if type(archive.get("uncompressed_size")) is not int or archive["uncompressed_size"] < 0:
                raise ManifestError(f"invalid archive uncompressed size for {asset_key}")
            if type(archive.get("compressed_size")) is not int or archive["compressed_size"] < 0:
                raise ManifestError(f"invalid archive compressed size for {asset_key}")
            if not isinstance(archive.get("allowed_modes"), list):
                raise ManifestError(f"archive mode policy missing for {asset_key}")
            try:
                allowed_modes = [int(str(mode), 0) for mode in archive["allowed_modes"]]
            except (TypeError, ValueError) as exc:
                raise ManifestError(f"invalid archive mode policy for {asset_key}") from exc
            if not allowed_modes:
                raise ManifestError(f"archive mode policy missing for {asset_key}")
            names = []
            folded_names = set()
            for member in members:
                if not isinstance(member, MappingABC):
                    raise ManifestError(f"archive member must be an object for {asset_key}")
                name = member.get("name")
                if not isinstance(name, str):
                    raise ManifestError(f"archive member name must be a string for {asset_key}")
                try:
                    normalized, is_directory = _normalised_member_name(name)
                except (IntegrityError, TypeError) as exc:
                    raise ManifestError(f"invalid archive member name for {asset_key}") from exc
                if is_directory and not name.endswith("/"):
                    raise ManifestError(f"invalid archive member name for {asset_key}")
                if normalized + ("/" if is_directory else "") != name:
                    raise ManifestError(f"non-canonical archive member name for {asset_key}")
                folded_name = normalized.casefold()
                if folded_name in folded_names:
                    raise ManifestError(f"case-folded archive member collision for {asset_key}")
                folded_names.add(folded_name)
                if type(member.get("uncompressed_size")) is not int or member["uncompressed_size"] < 0:
                    raise ManifestError(f"invalid archive member size for {asset_key}")
                try:
                    member_mode = int(str(member.get("mode")), 0)
                except (TypeError, ValueError) as exc:
                    raise ManifestError(f"invalid archive member mode for {asset_key}") from exc
                if member_mode not in allowed_modes:
                    raise ManifestError(f"archive member mode is not allowlisted for {asset_key}")
                names.append(name)
            if len(names) != len(set(names)):
                raise ManifestError(f"archive member names are not unique for {asset_key}")
            executable_members = archive.get("executable_members")
            if not isinstance(executable_members, list) or any(
                not isinstance(name, str) or name not in names for name in executable_members
            ):
                raise ManifestError(f"archive executable allowlist is incomplete for {asset_key}")

    headers = manifest.get("headers")
    if not isinstance(headers, list) or len(headers) != 6:
        raise ManifestError("manifest must contain the six header policies")
    header_paths = set()
    header_paths_folded = set()
    for header in headers:
        if not isinstance(header, MappingABC):
            raise ManifestError("header policy must be an object")
        for key in ("path", "source_url", "source_size", "source_sha256", "final_size", "final_sha256", "policy"):
            if key not in header:
                raise ManifestError(f"header policy missing {key}")
        if not isinstance(header["path"], str):
            raise ManifestError("header path must be a string")
        try:
            normalized, is_directory = _normalised_member_name(header["path"])
        except (IntegrityError, TypeError) as exc:
            raise ManifestError(f"invalid header path {header.get('path')!r}") from exc
        if is_directory or normalized != header["path"]:
            raise ManifestError(f"header path must be a canonical relative file path: {header['path']!r}")
        if not isinstance(header["source_url"], str) or not header["source_url"].startswith("https://"):
            raise ManifestError(f"header URL must use HTTPS for {header['path']}")
        if header["path"] in header_paths:
            raise ManifestError(f"duplicate header policy {header['path']}")
        if header["path"].casefold() in header_paths_folded:
            raise ManifestError(f"case-folded header policy collision {header['path']}")
        header_paths.add(header["path"])
        header_paths_folded.add(header["path"].casefold())
        if header["policy"] not in ("tracked-preserve", "generated-verified"):
            raise ManifestError(f"unsupported header policy {header['policy']}")
        if type(header["source_size"]) is not int or header["source_size"] <= 0:
            raise ManifestError(f"invalid header source size for {header['path']}")
        if type(header["final_size"]) is not int or header["final_size"] <= 0:
            raise ManifestError(f"invalid header final size for {header['path']}")
        if not isinstance(header["source_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", header["source_sha256"]):
            raise ManifestError(f"invalid header source SHA-256 for {header['path']}")
        if not isinstance(header["final_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", header["final_sha256"]):
            raise ManifestError(f"invalid header final SHA-256 for {header['path']}")
        if not isinstance(header.get("gbi_patch_applied"), bool):
            raise ManifestError(f"invalid gbi patch policy for {header['path']}")
    return manifest


def _normalise_arch(machine: str, system: str) -> str:
    machine = machine.lower()
    if machine in ("amd64", "x86_64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64" if system == "linux" else "arm64"
    if machine in ("i386", "i486", "i586", "i686", "x86"):
        return "i686" if system == "linux" else "x86"
    if machine in ("armv7", "armv7l"):
        return "armv7l"
    return machine


def _asset_name(tool: str, system: Optional[str] = None, machine: Optional[str] = None) -> str:
    system = (system or platform.system()).lower()
    machine = machine or platform.machine()
    arch = _normalise_arch(machine, system)

    if tool == "compilers":
        return "compilers_20250812.zip"

    if tool == "orthrus" and system == "darwin":
        raise UnsupportedArtifact(
            "Orthrus v0.2.0 has no macOS release asset; refusing network access on Darwin"
        )
    if tool == "sjiswrap":
        # sjiswrap is a Windows helper invoked through the host's configured
        # wrapper, so it is intentionally downloadable from macOS/Linux too.
        return "sjiswrap-windows-x86.exe"
    if tool == "wibo":
        if system != "linux" or arch not in ("x86_64", "i686"):
            raise UnsupportedArtifact("wibo is pinned only for Linux x86 hosts")
        return "wibo"

    if system == "darwin":
        release_system = "macos"
        if tool == "binutils":
            return "macos-universal.zip"
        if arch not in ("arm64", "x86_64"):
            raise UnsupportedArtifact(f"no pinned {tool} artifact for Darwin {machine}")
        release_arch = arch
    elif system == "windows":
        release_system = "windows"
        if arch not in ("arm64", "x86", "x86_64"):
            raise UnsupportedArtifact(f"no pinned {tool} artifact for Windows {machine}")
        release_arch = arch
    elif system == "linux":
        release_system = "linux"
        if tool == "binutils" and arch not in ("aarch64", "armv7l", "i686", "x86_64"):
            raise UnsupportedArtifact(f"no pinned binutils artifact for Linux {machine}")
        if tool in ("dtk", "objdiff-cli") and arch not in ("aarch64", "armv7l", "i686", "x86_64"):
            raise UnsupportedArtifact(f"no pinned {tool} artifact for Linux {machine}")
        release_arch = arch
    else:
        raise UnsupportedArtifact(f"unsupported host platform {system}")

    if tool == "binutils":
        if release_system == "windows" and release_arch != "x86_64":
            raise UnsupportedArtifact("binutils is pinned only for Windows x86_64")
        return f"{release_system}-{release_arch}.zip"
    suffix = ".exe" if release_system == "windows" else ""
    return f"{tool}-{release_system}-{release_arch}{suffix}"


def select_artifact(
    tool: str,
    tag: str,
    *,
    system: Optional[str] = None,
    machine: Optional[str] = None,
    manifest: Optional[dict] = None,
) -> dict:
    manifest = manifest or load_manifest()
    asset_name = _asset_name(tool, system, machine)
    candidates = [
        artifact for artifact in manifest["artifacts"]
        if artifact["tool"] == tool and artifact["asset_name"] == asset_name
    ]
    if len(candidates) != 1:
        raise ManifestError(f"manifest has no unique pinned artifact for {tool}/{asset_name}")
    artifact = candidates[0]
    if artifact["release_tag"] != tag:
        raise DownloadError(
            f"tag {tag!r} is not the pinned {tool} release {artifact['release_tag']!r}; "
            "mutable tags are not accepted"
        )
    return artifact


def binutils_url(tag: str) -> str:
    return select_artifact("binutils", tag)["asset_url"]


def compilers_url(tag: str) -> str:
    return select_artifact("compilers", tag)["asset_url"]


def dtk_url(tag: str) -> str:
    return select_artifact("dtk", tag)["asset_url"]


def objdiff_cli_url(tag: str) -> str:
    return select_artifact("objdiff-cli", tag)["asset_url"]


def sjiswrap_url(tag: str) -> str:
    return select_artifact("sjiswrap", tag)["asset_url"]


def wibo_url(tag: str) -> str:
    return select_artifact("wibo", tag)["asset_url"]


def orthrus_url(tag: str) -> str:
    return select_artifact("orthrus", tag)["asset_url"]


TOOLS: Dict[str, Callable[[str], str]] = {
    "binutils": binutils_url,
    "compilers": compilers_url,
    "dtk": dtk_url,
    "objdiff-cli": objdiff_cli_url,
    "sjiswrap": sjiswrap_url,
    "wibo": wibo_url,
    "orthrus": orthrus_url,
}


def _cache_root(cache_dir: Optional[Path]) -> Path:
    if cache_dir is not None:
        return Path(cache_dir)
    configured = os.environ.get("ACGC_DOWNLOAD_CACHE")
    return Path(configured) if configured else DEFAULT_CACHE_DIR


def _offline_requested(offline: Optional[bool]) -> bool:
    if offline is not None:
        return offline
    return os.environ.get("ACGC_DOWNLOAD_OFFLINE", "").lower() in ("1", "true", "yes", "on")


def _cache_path(cache_dir: Path, record: Mapping[str, object]) -> Path:
    name = str(record["asset_name"]).replace("/", "_").replace("\\", "_")
    return cache_dir / f"{record.get('asset_id', 'header')}-{name}"


def _close_descriptor(descriptor: Optional[int]) -> None:
    if descriptor is None:
        return
    try:
        os.close(descriptor)
    except OSError:
        pass


def _chmod_open_file(descriptor: int, mode: int) -> None:
    """Set mode through an already-open descriptor."""
    os.fchmod(descriptor, mode)


def _hash_file(path: Path, expected_size: int) -> str:
    digest = hashlib.sha256()
    count = 0
    descriptor, parent_fd = _open_regular_file(path)
    try:
        with os.fdopen(os.dup(descriptor), "rb") as stream:
            while True:
                chunk = stream.read(CHUNK_SIZE)
                if not chunk:
                    break
                count += len(chunk)
                if count > expected_size:
                    raise IntegrityError(f"{path} is larger than its manifest size")
                digest.update(chunk)
    except OSError as exc:
        raise IntegrityError(f"cannot read {path}: {exc}") from exc
    finally:
        _close_descriptor(descriptor)
        _close_descriptor(parent_fd)
    if count != expected_size:
        raise IntegrityError(f"{path} is truncated: expected {expected_size}, got {count}")
    return digest.hexdigest()


def _verify_file(path: Path, record: Mapping[str, object]) -> None:
    expected_size = int(record["size"])
    digest = _hash_file(path, expected_size)
    if digest != record["sha256"]:
        raise IntegrityError(f"SHA-256 mismatch for {path}")


def _copy_stream(
    source: BinaryIO,
    destination: BinaryIO,
    *,
    expected_size: int,
    digest: Optional[hashlib._Hash] = None,
) -> int:
    count = 0
    while True:
        chunk = source.read(CHUNK_SIZE)
        if not chunk:
            break
        if not isinstance(chunk, (bytes, bytearray)):
            raise IntegrityError("download stream returned a non-byte value")
        count += len(chunk)
        if count > expected_size:
            raise IntegrityError(f"stream exceeds manifest size {expected_size}")
        destination.write(chunk)
        if digest is not None:
            digest.update(chunk)
    if count != expected_size:
        raise IntegrityError(f"stream truncated: expected {expected_size}, got {count}")
    return count


def _download_to_cache(record: Mapping[str, object], cache_path: Path) -> Path:
    if os.name == "nt":
        return _windows_publication_backend().download_to_cache(record, cache_path)
    _require_archive_dirfd_support()
    parent_fd = _open_directory_path(cache_path.parent, create=True)
    descriptor: Optional[int] = None
    temporary = ""
    try:
        descriptor, temporary = _temporary_path_at(parent_fd, ".download-")
        request = urllib.request.Request(
            str(record["asset_url"]),
            headers={"User-Agent": "ACGC-PC-Port verified downloader"},
        )
        with urllib.request.urlopen(request, timeout=60) as response:
            content_length = response.headers.get("Content-Length")
            if content_length is not None:
                try:
                    content_length_value = int(content_length)
                except (TypeError, ValueError) as exc:
                    raise IntegrityError("invalid Content-Length in artifact response") from exc
                if content_length_value != int(record["size"]):
                    raise IntegrityError(
                        f"Content-Length mismatch for {record['asset_name']}: {content_length}"
                    )
            digest = hashlib.sha256()
            with os.fdopen(descriptor, "wb", closefd=False) as output:
                _copy_stream(
                    response,
                    output,
                    expected_size=int(record["size"]),
                    digest=digest,
                )
                output.flush()
                os.fsync(output.fileno())
            if digest.hexdigest() != record["sha256"]:
                raise IntegrityError(f"SHA-256 mismatch for {record['asset_name']}")
        _commit_temporary_at(descriptor, parent_fd, temporary, cache_path.name)
        descriptor = None
    except (urllib.error.URLError, OSError) as exc:
        if temporary:
            _remove_temporary_at(descriptor, parent_fd, temporary)
        else:
            _close_descriptor(descriptor)
        descriptor = None
        raise DownloadError(f"network fetch failed for {record['asset_name']}") from exc
    except Exception:
        if temporary:
            _remove_temporary_at(descriptor, parent_fd, temporary)
        else:
            _close_descriptor(descriptor)
        descriptor = None
        raise
    finally:
        _close_descriptor(parent_fd)
    return cache_path


def _obtain_verified_source(
    record: Mapping[str, object],
    *,
    cache_dir: Optional[Path],
    offline: bool,
    explicit_source: Optional[Path] = None,
) -> Path:
    if explicit_source is not None:
        source = Path(explicit_source)
        _verify_file(source, record)
        return source

    cache = _cache_path(_cache_root(cache_dir), record)
    if cache.exists():
        try:
            _verify_file(cache, record)
            return cache
        except IntegrityError:
            if offline:
                raise
    if offline:
        raise IntegrityError(
            f"offline mode requires a verified cache or explicit path for {record['asset_name']}"
        )
    return _download_to_cache(record, cache)


def _normalised_member_name(name: str) -> Tuple[str, bool]:
    if not name or "\x00" in name or "\\" in name:
        raise IntegrityError(f"unsafe ZIP member name {name!r}")
    is_directory = name.endswith("/")
    bare = name[:-1] if is_directory else name
    if (
        not bare
        or name.startswith("/")
        or ntpath.splitdrive(name)[0]
        or any(part in ("", ".", "..") for part in bare.split("/"))
    ):
        raise IntegrityError(f"unsafe ZIP member name {name!r}")
    normalized = "/".join(PurePosixPath(bare).parts)
    expected = normalized + ("/" if is_directory else "")
    if expected != name:
        raise IntegrityError(f"non-canonical ZIP member name {name!r}")
    return normalized, is_directory


def _zip_mode(info: zipfile.ZipInfo) -> int:
    mode = (info.external_attr >> 16) & 0xFFFF
    if mode == 0:
        mode = 0o40755 if info.is_dir() else 0o100644
    return mode


def _validate_archive(zf: zipfile.ZipFile, artifact: Mapping[str, object]) -> Sequence[Tuple[zipfile.ZipInfo, str, bool]]:
    policy = artifact.get("archive")
    if not isinstance(policy, Mapping):
        raise IntegrityError("archive policy is missing")
    infos = zf.infolist()
    if len(infos) != int(policy["member_count"]):
        raise IntegrityError("ZIP member count does not match the pinned allowlist")

    members = policy["members"]
    expected = {str(member["name"]): member for member in members}
    allowed_modes = {int(str(mode), 0) for mode in policy["allowed_modes"]}
    seen = set()
    total_uncompressed = 0
    total_compressed = 0
    validated = []
    for info in infos:
        normalized, is_directory = _normalised_member_name(info.filename)
        exact_name = normalized + ("/" if is_directory else "")
        if exact_name in seen:
            raise IntegrityError(f"duplicate ZIP member {exact_name}")
        seen.add(exact_name)
        expected_member = expected.get(exact_name)
        if expected_member is None:
            raise IntegrityError(f"unexpected ZIP member {exact_name}")
        mode = _zip_mode(info)
        file_type = stat.S_IFMT(mode)
        if file_type not in (stat.S_IFREG, stat.S_IFDIR):
            raise IntegrityError(f"ZIP member has a special mode: {exact_name}")
        if mode not in allowed_modes or mode != int(str(expected_member["mode"]), 0):
            raise IntegrityError(f"ZIP member mode is not allowlisted: {exact_name}")
        if is_directory != (file_type == stat.S_IFDIR):
            raise IntegrityError(f"ZIP member directory type mismatch: {exact_name}")
        if info.file_size != int(expected_member["uncompressed_size"]):
            raise IntegrityError(f"ZIP member size mismatch: {exact_name}")
        if info.file_size > MAX_MEMBER_BYTES:
            raise IntegrityError(f"ZIP member exceeds the bounded size limit: {exact_name}")
        total_uncompressed += info.file_size
        total_compressed += info.compress_size
        if total_uncompressed > MAX_ARCHIVE_BYTES:
            raise IntegrityError("ZIP exceeds the bounded uncompressed size limit")
        validated.append((info, normalized, is_directory))

    if seen != set(expected):
        raise IntegrityError("ZIP is missing an allowlisted member")
    if total_uncompressed != int(policy["uncompressed_size"]):
        raise IntegrityError("ZIP uncompressed size does not match the manifest")
    if total_compressed != int(policy["compressed_size"]):
        raise IntegrityError("ZIP compressed size does not match the manifest")
    return validated


def _archive_dirfd_supported() -> bool:
    """Whether every descriptor-relative archive primitive is available."""
    supported = getattr(os, "supports_dir_fd", ())
    supported_fd = getattr(os, "supports_fd", ())
    return (
        os.name == "posix"
        and hasattr(os, "O_DIRECTORY")
        and hasattr(os, "O_NOFOLLOW")
        and hasattr(os, "fchmod")
        and os.open in supported
        and os.mkdir in supported
        and os.rename in supported
        and os.rmdir in supported
        and os.stat in supported
        and os.unlink in supported
        and os.scandir in supported_fd
    )


def _require_archive_dirfd_support() -> None:
    if os.name == "nt":
        _windows_publication_backend().require_supported()
        return
    if not _archive_dirfd_supported():
        raise IntegrityError(
            "archive/header publication requires descriptor-relative no-follow primitives"
        )


class _WindowsHandle:
    """A native or test-double handle whose identity outlives its pathname."""

    __slots__ = ("raw", "identity", "is_directory", "is_regular", "moved", "closed")

    def __init__(self, raw: Any, identity: object, is_directory: bool, is_regular: Optional[bool] = None) -> None:
        self.raw = raw
        self.identity = identity
        self.is_directory = is_directory
        self.is_regular = not is_directory if is_regular is None else is_regular
        self.moved = False
        self.closed = False


class _WinUnicodeString(ctypes.Structure):
    _fields_ = [
        ("Length", ctypes.c_uint16),
        ("MaximumLength", ctypes.c_uint16),
        ("Buffer", ctypes.POINTER(ctypes.c_uint16)),
    ]


class _WinObjectAttributes(ctypes.Structure):
    _fields_ = [
        ("Length", ctypes.c_uint32),
        ("RootDirectory", ctypes.c_void_p),
        ("ObjectName", ctypes.POINTER(_WinUnicodeString)),
        ("Attributes", ctypes.c_uint32),
        ("SecurityDescriptor", ctypes.c_void_p),
        ("SecurityQualityOfService", ctypes.c_void_p),
    ]


class _WinIoStatusBlock(ctypes.Structure):
    _fields_ = [("Status", ctypes.c_void_p), ("Information", ctypes.c_size_t)]


class _WinFileId128(ctypes.Structure):
    _fields_ = [("Identifier", ctypes.c_ubyte * 16)]


class _WinFileIdInfo(ctypes.Structure):
    _fields_ = [("VolumeSerialNumber", ctypes.c_uint64), ("FileId", _WinFileId128)]


class _WinFileAttributeTagInfo(ctypes.Structure):
    _fields_ = [("FileAttributes", ctypes.c_uint32), ("ReparseTag", ctypes.c_uint32)]


class _WinFileIdBothDirectoryInfoHead(ctypes.Structure):
    _fields_ = [
        ("NextEntryOffset", ctypes.c_uint32),
        ("FileIndex", ctypes.c_uint32),
        ("CreationTime", ctypes.c_int64),
        ("LastAccessTime", ctypes.c_int64),
        ("LastWriteTime", ctypes.c_int64),
        ("ChangeTime", ctypes.c_int64),
        ("EndOfFile", ctypes.c_int64),
        ("AllocationSize", ctypes.c_int64),
        ("FileAttributes", ctypes.c_uint32),
        ("FileNameLength", ctypes.c_uint32),
        ("EaSize", ctypes.c_uint32),
        ("ShortNameLength", ctypes.c_ubyte),
        ("Reserved", ctypes.c_ubyte),
        ("ShortName", ctypes.c_uint16 * 12),
        ("FileId", ctypes.c_int64),
    ]


class _WinFileRenameInfoEx(ctypes.Structure):
    _fields_ = [
        ("Flags", ctypes.c_uint32),
        ("RootDirectory", ctypes.c_void_p),
        ("FileNameLength", ctypes.c_uint32),
        ("FileName", ctypes.c_uint16 * 1),
    ]


class _WinFileDispositionInfoEx(ctypes.Structure):
    _fields_ = [("Flags", ctypes.c_uint32)]


class _CtypesWindowsApi:
    """Small, fail-closed NT handle API used by the Windows publication path.

    All child operations use an NT ``RootDirectory`` handle.  The public
    Windows pathname APIs are used only once to resolve the initial volume or
    share root; no publication operation accepts an absolute replacement path.
    """

    _FILE_INFO_ATTRIBUTE_TAG = 9
    _FILE_INFO_ID_BOTH_DIRECTORY = 10
    _FILE_INFO_ID_BOTH_DIRECTORY_RESTART = 11
    _FILE_INFO_ID = 18
    _FILE_RENAME_INFO_EX = 22
    _FILE_DISPOSITION_INFO_EX = 21
    _FILE_TYPE_DISK = 1
    _FILE_ATTRIBUTE_DIRECTORY = 0x10
    _FILE_ATTRIBUTE_REPARSE_POINT = 0x400
    _FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
    _FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
    _FILE_SHARE_READ = 0x1
    _FILE_SHARE_WRITE = 0x2
    _FILE_SHARE_DELETE = 0x4
    _GENERIC_READ = 0x80000000
    _GENERIC_WRITE = 0x40000000
    _DELETE = 0x00010000
    _FILE_READ_ATTRIBUTES = 0x80
    _FILE_WRITE_ATTRIBUTES = 0x100
    _FILE_LIST_DIRECTORY = 0x1
    _FILE_OPEN = 1
    _FILE_CREATE = 2
    _FILE_OPEN_IF = 3
    _FILE_DIRECTORY_FILE = 0x1
    _FILE_SYNCHRONOUS_IO_NONALERT = 0x20
    _FILE_NON_DIRECTORY_FILE = 0x40
    _FILE_OPEN_REPARSE_POINT_OPTION = 0x00200000
    _OBJ_CASE_INSENSITIVE = 0x40
    _FILE_RENAME_FLAG_REPLACE_IF_EXISTS = 0x1
    _FILE_RENAME_FLAG_FAIL_IF_EXISTS = 0x20
    _FILE_DISPOSITION_FLAG_DELETE = 0x1
    _FILE_DISPOSITION_FLAG_POSIX_SEMANTICS = 0x2
    _FILE_DISPOSITION_FLAG_IGNORE_READONLY = 0x10

    def __init__(self) -> None:
        if os.name != "nt":
            raise IntegrityError("native Windows handle backend is unavailable on this host")
        try:
            import ctypes.wintypes as wintypes
            import msvcrt

            self._wintypes = wintypes
            self._msvcrt = msvcrt
            self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self._ntdll = ctypes.WinDLL("ntdll")
            self._configure_functions()
        except (AttributeError, ImportError, OSError, TypeError) as exc:
            raise IntegrityError("required Windows handle-relative APIs are unavailable") from exc

    def _configure_functions(self) -> None:
        w = self._wintypes
        k = self._kernel32
        n = self._ntdll
        k.CreateFileW.argtypes = [
            w.LPCWSTR,
            w.DWORD,
            w.DWORD,
            ctypes.c_void_p,
            w.DWORD,
            w.DWORD,
            w.HANDLE,
        ]
        k.CreateFileW.restype = w.HANDLE
        k.CloseHandle.argtypes = [w.HANDLE]
        k.CloseHandle.restype = w.BOOL
        k.GetFileType.argtypes = [w.HANDLE]
        k.GetFileType.restype = w.DWORD
        k.GetFileInformationByHandleEx.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        k.GetFileInformationByHandleEx.restype = w.BOOL
        k.SetFileInformationByHandle.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        k.SetFileInformationByHandle.restype = w.BOOL
        k.FlushFileBuffers.argtypes = [w.HANDLE]
        k.FlushFileBuffers.restype = w.BOOL
        k.DuplicateHandle.argtypes = [w.HANDLE, w.HANDLE, w.HANDLE, ctypes.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD]
        k.DuplicateHandle.restype = w.BOOL
        k.GetCurrentProcess.argtypes = []
        k.GetCurrentProcess.restype = w.HANDLE
        k.SetFilePointerEx.argtypes = [w.HANDLE, ctypes.c_longlong, ctypes.POINTER(ctypes.c_longlong), w.DWORD]
        k.SetFilePointerEx.restype = w.BOOL
        k.GetFullPathNameW.argtypes = [w.LPCWSTR, w.DWORD, w.LPWSTR, ctypes.POINTER(w.LPWSTR)]
        k.GetFullPathNameW.restype = w.DWORD
        k.ReadFile.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        k.ReadFile.restype = w.BOOL
        k.WriteFile.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        k.WriteFile.restype = w.BOOL
        n.NtCreateFile.argtypes = [
            ctypes.POINTER(w.HANDLE),
            w.DWORD,
            ctypes.POINTER(_WinObjectAttributes),
            ctypes.POINTER(_WinIoStatusBlock),
            ctypes.c_void_p,
            w.DWORD,
            w.DWORD,
            w.DWORD,
            w.DWORD,
            ctypes.c_void_p,
            w.DWORD,
        ]
        n.NtCreateFile.restype = ctypes.c_long
        n.RtlNtStatusToDosError.argtypes = [ctypes.c_long]
        n.RtlNtStatusToDosError.restype = w.ULONG
        self._required = (
            k.CreateFileW,
            k.CloseHandle,
            k.GetFileType,
            k.GetFileInformationByHandleEx,
            k.SetFileInformationByHandle,
            k.FlushFileBuffers,
            k.DuplicateHandle,
            k.GetCurrentProcess,
            k.SetFilePointerEx,
            k.GetFullPathNameW,
            k.ReadFile,
            k.WriteFile,
            n.NtCreateFile,
            n.RtlNtStatusToDosError,
        )

    def require_supported(self) -> None:
        if not self._required:
            raise IntegrityError("required Windows handle-relative APIs are unavailable")

    @staticmethod
    def _raw_value(raw: Any) -> int:
        value = getattr(raw, "value", raw)
        if value is None or int(value) in (0, -1):
            raise IntegrityError("Windows returned an invalid handle")
        return int(value)

    def _last_error(self, message: str) -> OSError:
        code = int(ctypes.get_last_error())
        if code in (2, 3):
            return FileNotFoundError(code, message)
        if code in (80, 183):
            return FileExistsError(code, message)
        return OSError(code, message)

    def _nt_error(self, status: int, message: str) -> None:
        status_value = getattr(status, "value", status)
        unsigned = int(status_value) & 0xFFFFFFFF
        if unsigned & 0x80000000:
            code = int(self._ntdll.RtlNtStatusToDosError(ctypes.c_long(status_value)))
            if code in (2, 3):
                raise FileNotFoundError(code, message)
            if code in (80, 183):
                raise FileExistsError(code, message)
            raise OSError(code, message)

    @staticmethod
    def _validate_component(name: str) -> None:
        if (
            not name
            or name in (".", "..")
            or any(char in name for char in '\x00<>:"/\\|?*')
            or name.endswith((".", " "))
            or len(name) > 255
        ):
            raise IntegrityError(f"unsafe Windows path component: {name!r}")
        stem = name.split(".", 1)[0].upper()
        if stem in {"CON", "PRN", "AUX", "NUL"} or (
            len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[3] in "123456789"
        ):
            raise IntegrityError(f"reserved Windows path component: {name!r}")

    def _canonical_components(self, path: Path) -> Tuple[str, Sequence[str]]:
        value = os.fspath(path)
        if isinstance(value, bytes):
            raise IntegrityError("Windows paths must be Unicode")
        buffer = ctypes.create_unicode_buffer(32768)
        file_part = self._wintypes.LPWSTR()
        length = int(self._kernel32.GetFullPathNameW(value, len(buffer), buffer, ctypes.byref(file_part)))
        if length == 0 or length >= len(buffer):
            raise IntegrityError(f"cannot canonicalize Windows path: {path}")
        canonical = buffer.value.replace("/", "\\")
        if canonical.startswith("\\\\.\\"):
            raise IntegrityError("device paths are not accepted by the Windows downloader")
        if canonical.startswith("\\\\?\\UNC\\"):
            canonical = "\\\\" + canonical[8:]
        elif canonical.startswith("\\\\?\\"):
            canonical = canonical[4:]
        if len(canonical) >= 3 and canonical[1:3] == ":\\":
            root = "\\\\?\\" + canonical[:3]
            rest = canonical[3:]
        elif canonical.startswith("\\\\"):
            pieces = canonical[2:].split("\\")
            if len(pieces) < 2 or not pieces[0] or not pieces[1]:
                raise IntegrityError("Windows UNC path has no complete share root")
            root = "\\\\?\\UNC\\" + pieces[0] + "\\" + pieces[1] + "\\"
            rest = "\\".join(pieces[2:])
        else:
            raise IntegrityError("Windows path is not absolute after canonicalization")
        components = [part for part in rest.split("\\") if part]
        for component in components:
            self._validate_component(component)
        return root, components

    def _inspect(self, raw: Any) -> _WindowsHandle:
        raw_value = self._raw_value(raw)
        handle = self._wintypes.HANDLE(raw_value)
        if int(self._kernel32.GetFileType(handle)) != self._FILE_TYPE_DISK:
            raise IntegrityError("Windows publication handle is not a disk handle")
        tag = _WinFileAttributeTagInfo()
        if not self._kernel32.GetFileInformationByHandleEx(
            handle,
            self._FILE_INFO_ATTRIBUTE_TAG,
            ctypes.byref(tag),
            ctypes.sizeof(tag),
        ):
            raise self._last_error("cannot inspect Windows file attributes")
        if tag.FileAttributes & self._FILE_ATTRIBUTE_REPARSE_POINT:
            raise IntegrityError("reparse points are not accepted by the Windows downloader")
        file_id = _WinFileIdInfo()
        if not self._kernel32.GetFileInformationByHandleEx(
            handle,
            self._FILE_INFO_ID,
            ctypes.byref(file_id),
            ctypes.sizeof(file_id),
        ):
            raise self._last_error("cannot inspect Windows file identity")
        identity = (int(file_id.VolumeSerialNumber), bytes(file_id.FileId.Identifier))
        is_directory = bool(tag.FileAttributes & self._FILE_ATTRIBUTE_DIRECTORY)
        return _WindowsHandle(raw_value, identity, is_directory, is_regular=not is_directory)

    def _open_root_handle(self, root: str) -> _WindowsHandle:
        access = self._GENERIC_READ | self._FILE_LIST_DIRECTORY | self._FILE_READ_ATTRIBUTES | self._FILE_WRITE_ATTRIBUTES | self._DELETE
        raw = self._kernel32.CreateFileW(
            root,
            access,
            self._FILE_SHARE_READ | self._FILE_SHARE_WRITE | self._FILE_SHARE_DELETE,
            None,
            self._FILE_OPEN,
            self._FILE_FLAG_BACKUP_SEMANTICS | self._FILE_FLAG_OPEN_REPARSE_POINT,
            None,
        )
        raw_value = getattr(raw, "value", raw)
        if raw_value in (None, 0, -1):
            raise self._last_error("cannot open Windows volume/share root")
        try:
            result = self._inspect(raw_value)
            if not result.is_directory:
                self.close(result)
                raise IntegrityError("Windows publication root is not a directory")
            return result
        except Exception:
            if not isinstance(locals().get("result"), _WindowsHandle):
                self._kernel32.CloseHandle(self._wintypes.HANDLE(raw_value))
            raise

    def open_directory_path(self, path: Path, *, create: bool = False) -> _WindowsHandle:
        root, components = self._canonical_components(path)
        current = self._open_root_handle(root)
        try:
            for component in components:
                child = self.open_child(current, component, directory=True, create=create)
                self.close(current)
                current = child
            return current
        except Exception:
            self.close(current)
            raise

    def open_parent_path(self, path: Path, *, create: bool = False) -> Tuple[_WindowsHandle, str]:
        root, components = self._canonical_components(path)
        if not components:
            raise IntegrityError(f"Windows publication path has no final component: {path}")
        current = self._open_root_handle(root)
        try:
            for component in components[:-1]:
                child = self.open_child(current, component, directory=True, create=create)
                self.close(current)
                current = child
            return current, components[-1]
        except Exception:
            self.close(current)
            raise

    def open_relative_directory(
        self,
        root: _WindowsHandle,
        components: Sequence[str],
        *,
        create: bool = False,
    ) -> _WindowsHandle:
        current = root
        owned = False
        try:
            for component in components:
                child = self.open_child(current, component, directory=True, create=create)
                if owned:
                    self.close(current)
                current = child
                owned = True
            return current
        except Exception:
            if owned:
                self.close(current)
            raise

    def open_child(
        self,
        parent: _WindowsHandle,
        name: str,
        *,
        directory: Optional[bool] = None,
        create: bool = False,
        exclusive: bool = False,
        writable: bool = True,
        deletable: bool = True,
    ) -> _WindowsHandle:
        self._validate_component(name)
        encoded = name.encode("utf-16-le")
        name_buffer = ctypes.create_string_buffer(encoded + b"\x00\x00")
        name_units = ctypes.cast(name_buffer, ctypes.POINTER(ctypes.c_uint16))
        unicode_name = _WinUnicodeString(
            len(encoded),
            len(encoded) + 2,
            name_units,
        )
        attributes = _WinObjectAttributes(
            ctypes.sizeof(_WinObjectAttributes),
            ctypes.c_void_p(int(parent.raw)),
            ctypes.pointer(unicode_name),
            self._OBJ_CASE_INSENSITIVE,
            None,
            None,
        )
        status_block = _WinIoStatusBlock()
        raw = self._wintypes.HANDLE()
        access = self._GENERIC_READ | self._FILE_READ_ATTRIBUTES
        if writable or directory is True:
            access |= self._GENERIC_WRITE | self._FILE_WRITE_ATTRIBUTES
        if directory is None:
            # A handle opened for type discovery may turn out to be a
            # directory that will immediately be enumerated during recovery.
            access |= self._FILE_LIST_DIRECTORY
        if deletable:
            access |= self._DELETE
        options = self._FILE_SYNCHRONOUS_IO_NONALERT | self._FILE_OPEN_REPARSE_POINT_OPTION
        if directory is True:
            access |= self._FILE_LIST_DIRECTORY
            options |= self._FILE_DIRECTORY_FILE
        elif directory is False:
            options |= self._FILE_NON_DIRECTORY_FILE
        disposition = self._FILE_CREATE if exclusive else (self._FILE_OPEN_IF if create else self._FILE_OPEN)
        status = self._ntdll.NtCreateFile(
            ctypes.byref(raw),
            access,
            ctypes.byref(attributes),
            ctypes.byref(status_block),
            None,
            0,
            self._FILE_SHARE_READ | self._FILE_SHARE_WRITE | self._FILE_SHARE_DELETE,
            disposition,
            options,
            None,
            0,
        )
        try:
            self._nt_error(status, f"cannot open Windows child {name}")
        except Exception:
            if getattr(raw, "value", None):
                self._kernel32.CloseHandle(raw)
            raise
        try:
            result = self._inspect(raw)
        except Exception:
            self._kernel32.CloseHandle(raw)
            raise
        if directory is not None and result.is_directory != directory:
            self.close(result)
            raise IntegrityError(f"Windows child has an unexpected type: {name}")
        return result

    def open_stream(self, handle: _WindowsHandle, mode: str) -> BinaryIO:
        duplicate = self._wintypes.HANDLE()
        current = self._kernel32.GetCurrentProcess()
        if not self._kernel32.DuplicateHandle(
            current,
            self._wintypes.HANDLE(handle.raw),
            current,
            ctypes.byref(duplicate),
            0,
            False,
            2,
        ):
            raise self._last_error("cannot duplicate Windows publication handle")
        flags = os.O_RDONLY if "r" in mode and "w" not in mode else os.O_RDWR
        try:
            if not self._kernel32.SetFilePointerEx(
                duplicate,
                ctypes.c_longlong(0),
                None,
                0,
            ):
                raise self._last_error("cannot seek Windows publication handle")
            fd = self._msvcrt.open_osfhandle(self._raw_value(duplicate), flags)
            return os.fdopen(fd, mode, closefd=True)
        except Exception:
            self._kernel32.CloseHandle(duplicate)
            raise

    def flush(self, handle: _WindowsHandle) -> None:
        if not self._kernel32.FlushFileBuffers(self._wintypes.HANDLE(handle.raw)):
            raise self._last_error("cannot flush Windows publication handle")

    def identity_of(self, handle: _WindowsHandle) -> object:
        return self._inspect(handle.raw).identity

    def close(self, handle: Optional[_WindowsHandle]) -> None:
        if handle is None or handle.closed:
            return
        handle.closed = True
        self._kernel32.CloseHandle(self._wintypes.HANDLE(handle.raw))

    def rename(
        self,
        source: _WindowsHandle,
        destination_parent: _WindowsHandle,
        destination_name: str,
        *,
        replace: bool = False,
    ) -> None:
        self._validate_component(destination_name)
        encoded = destination_name.encode("utf-16-le")
        size = ctypes.sizeof(_WinFileRenameInfoEx) - ctypes.sizeof(ctypes.c_uint16) + len(encoded) + 2
        buffer = ctypes.create_string_buffer(size)
        info = ctypes.cast(buffer, ctypes.POINTER(_WinFileRenameInfoEx)).contents
        info.Flags = self._FILE_RENAME_FLAG_REPLACE_IF_EXISTS if replace else self._FILE_RENAME_FLAG_FAIL_IF_EXISTS
        info.RootDirectory = ctypes.c_void_p(int(destination_parent.raw))
        info.FileNameLength = len(encoded)
        ctypes.memmove(ctypes.addressof(info) + _WinFileRenameInfoEx.FileName.offset, encoded, len(encoded))
        if not self._kernel32.SetFileInformationByHandle(
            self._wintypes.HANDLE(source.raw),
            self._FILE_RENAME_INFO_EX,
            ctypes.byref(info),
            size,
        ):
            raise self._last_error(f"cannot rename Windows publication entry {destination_name}")
        source.moved = True

    def _delete_handle(self, handle: _WindowsHandle) -> None:
        info = _WinFileDispositionInfoEx(
            self._FILE_DISPOSITION_FLAG_DELETE
            | self._FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
            | self._FILE_DISPOSITION_FLAG_IGNORE_READONLY
        )
        if not self._kernel32.SetFileInformationByHandle(
            self._wintypes.HANDLE(handle.raw),
            self._FILE_DISPOSITION_INFO_EX,
            ctypes.byref(info),
            ctypes.sizeof(info),
        ):
            raise self._last_error("cannot delete Windows publication recovery handle")

    def _list_children(self, directory: _WindowsHandle) -> Sequence[Tuple[str, _WindowsHandle]]:
        children = []
        buffer = ctypes.create_string_buffer(64 * 1024)
        info_class = self._FILE_INFO_ID_BOTH_DIRECTORY_RESTART
        while True:
            if not self._kernel32.GetFileInformationByHandleEx(
                self._wintypes.HANDLE(directory.raw),
                info_class,
                ctypes.byref(buffer),
                ctypes.sizeof(buffer),
            ):
                code = int(ctypes.get_last_error())
                if code in (18, 259):
                    break
                raise self._last_error("cannot enumerate Windows recovery directory")
            info_class = self._FILE_INFO_ID_BOTH_DIRECTORY
            offset = 0
            found = False
            while offset < ctypes.sizeof(buffer):
                head = _WinFileIdBothDirectoryInfoHead.from_buffer(buffer, offset)
                length = int(head.FileNameLength)
                start = offset + ctypes.sizeof(_WinFileIdBothDirectoryInfoHead)
                if length % 2 or start + length > ctypes.sizeof(buffer):
                    raise IntegrityError("malformed Windows recovery directory entry")
                name = bytes(buffer[start : start + length]).decode("utf-16-le")
                if name not in (".", ".."):
                    child = self.open_child(
                        directory,
                        name,
                        directory=None,
                        create=False,
                        writable=False,
                        deletable=True,
                    )
                    entry_id = int(head.FileId) & 0xFFFFFFFFFFFFFFFF
                    identity_bytes = child.identity[1]
                    expected_id = entry_id.to_bytes(8, "little")
                    if expected_id not in (identity_bytes[:8], identity_bytes[-8:]):
                        self.close(child)
                        raise IntegrityError("Windows recovery directory entry identity changed")
                    children.append((name, child))
                found = True
                next_offset = int(head.NextEntryOffset)
                if not next_offset:
                    break
                if next_offset < ctypes.sizeof(_WinFileIdBothDirectoryInfoHead) or offset + next_offset >= ctypes.sizeof(buffer):
                    raise IntegrityError("malformed Windows recovery directory entry offset")
                offset += next_offset
            if not found:
                break
        return children

    def delete_tree(self, directory: _WindowsHandle) -> None:
        for _name, child in self._list_children(directory):
            try:
                if child.is_directory:
                    self.delete_tree(child)
                self._delete_handle(child)
            finally:
                self.close(child)

    def delete(self, handle: _WindowsHandle) -> None:
        self._delete_handle(handle)


class _WindowsSource:
    __slots__ = ("backend", "parent", "handle", "name")

    def __init__(self, backend: "_WindowsPublicationBackend", parent: _WindowsHandle, handle: _WindowsHandle, name: str) -> None:
        self.backend = backend
        self.parent = parent
        self.handle = handle
        self.name = name

    def close(self) -> None:
        self.backend.api.close(self.handle)
        self.backend.api.close(self.parent)


class _WindowsPublicationBackend:
    """Capability-selected Windows publication transactions.

    The API object is deliberately injectable so adversarial tests exercise
    handle identity and rollback without changing ``os.name`` or invoking
    native Windows calls on the development host.
    """

    def __init__(self, api: Optional[Any] = None) -> None:
        self.api = api if api is not None else _CtypesWindowsApi()

    def require_supported(self) -> None:
        self.api.require_supported()

    def close(self, handle: Optional[_WindowsHandle]) -> None:
        self.api.close(handle)

    def _open_any(self, parent: _WindowsHandle, name: str) -> Optional[_WindowsHandle]:
        try:
            return self.api.open_child(
                parent,
                name,
                directory=None,
                create=False,
                writable=False,
                deletable=True,
            )
        except FileNotFoundError:
            return None

    def _new_name(self, parent: _WindowsHandle, prefix: str) -> str:
        for _ in range(100):
            name = f"{prefix}{secrets.token_hex(12)}"
            try:
                candidate = self._open_any(parent, name)
            except IntegrityError:
                raise
            if candidate is None:
                return name
            self.api.close(candidate)
        raise IntegrityError("could not choose an unused Windows publication name")

    @staticmethod
    def _assert_regular(handle: _WindowsHandle, label: str) -> None:
        if handle.is_directory:
            raise IntegrityError(f"{label} is a directory")
        if not handle.is_regular:
            raise IntegrityError(f"{label} is not a regular file")

    def _hash_handle(self, handle: _WindowsHandle, expected_size: int) -> str:
        digest = hashlib.sha256()
        count = 0
        with self.api.open_stream(handle, "rb") as stream:
            while True:
                chunk = stream.read(CHUNK_SIZE)
                if not chunk:
                    break
                count += len(chunk)
                if count > expected_size:
                    raise IntegrityError("Windows source exceeds its manifest size")
                digest.update(chunk)
        if count != expected_size:
            raise IntegrityError(f"Windows source is truncated: expected {expected_size}, got {count}")
        return digest.hexdigest()

    def _source_for_path(self, path: Path, record: Mapping[str, object]) -> _WindowsSource:
        parent, name = self.api.open_parent_path(path, create=False)
        try:
            handle = self.api.open_child(
                parent,
                name,
                directory=False,
                create=False,
                writable=False,
                deletable=False,
            )
            try:
                self._assert_regular(handle, f"Windows source {path}")
                if self._hash_handle(handle, int(record["size"])) != record["sha256"]:
                    raise IntegrityError(f"SHA-256 mismatch for {path}")
                return _WindowsSource(self, parent, handle, name)
            except Exception:
                self.api.close(handle)
                raise
        except Exception:
            self.api.close(parent)
            raise

    def obtain_verified_source(
        self,
        record: Mapping[str, object],
        *,
        cache_dir: Optional[Path],
        offline: bool,
        explicit_source: Optional[Path] = None,
    ) -> _WindowsSource:
        self.require_supported()
        if explicit_source is not None:
            return self._source_for_path(Path(explicit_source), record)
        cache_path = _cache_path(_cache_root(cache_dir), record)
        parent, name = self.api.open_parent_path(cache_path, create=True)
        try:
            existing = self._open_any(parent, name)
            if existing is not None:
                try:
                    self._assert_regular(existing, f"Windows cache {cache_path}")
                    if self._hash_handle(existing, int(record["size"])) == record["sha256"]:
                        source = _WindowsSource(self, parent, existing, name)
                        existing = None
                        return source
                except IntegrityError:
                    if offline:
                        raise
                finally:
                    if existing is not None and not existing.closed:
                        self.api.close(existing)
            if offline:
                raise IntegrityError(
                    f"offline mode requires a verified cache or explicit path for {record['asset_name']}"
                )
        except Exception:
            self.api.close(parent)
            raise
        self.api.close(parent)
        _downloaded = self.download_to_cache(record, cache_path)
        return self._source_for_path(cache_path, record)

    def download_to_cache(self, record: Mapping[str, object], cache_path: Path) -> Path:
        self.require_supported()
        parent, destination = self.api.open_parent_path(cache_path, create=True)
        temporary = self._new_name(parent, ".download-")
        temporary_handle: Optional[_WindowsHandle] = None
        try:
            temporary_handle = self.api.open_child(parent, temporary, directory=False, create=True, exclusive=True)
            try:
                digest = hashlib.sha256()
                request = urllib.request.Request(
                    str(record["asset_url"]),
                    headers={"User-Agent": "ACGC-PC-Port verified downloader"},
                )
                with urllib.request.urlopen(request, timeout=60) as response, self.api.open_stream(
                    temporary_handle, "wb"
                ) as output:
                    content_length = response.headers.get("Content-Length")
                    if content_length is not None:
                        try:
                            content_length_value = int(content_length)
                        except (TypeError, ValueError) as exc:
                            raise IntegrityError("invalid Content-Length in artifact response") from exc
                        if content_length_value != int(record["size"]):
                            raise IntegrityError("Content-Length does not match the manifest")
                    _copy_stream(
                        response,
                        output,
                        expected_size=int(record["size"]),
                        digest=digest,
                    )
                    output.flush()
                self.api.flush(temporary_handle)
                if digest.hexdigest() != record["sha256"]:
                    raise IntegrityError(f"SHA-256 mismatch for {record['asset_name']}")
                self.commit_file(parent, temporary, temporary_handle, destination)
                self.api.close(temporary_handle)
                temporary_handle = None
            except (urllib.error.URLError, OSError) as exc:
                raise DownloadError(f"network fetch failed for {record['asset_name']}") from exc
        except Exception:
            # An unrenamed temporary is safe to remove by its retained handle;
            # an identity-changing or renamed entry is left as recovery data.
            try:
                if temporary_handle is not None and not temporary_handle.moved:
                    self.api.delete(temporary_handle)
            except Exception:
                pass
            self.api.close(temporary_handle)
            raise
        finally:
            self.api.close(temporary_handle)
            self.api.close(parent)
        return cache_path

    def commit_file(
        self,
        parent: _WindowsHandle,
        temporary: str,
        temporary_handle: _WindowsHandle,
        destination: str,
    ) -> None:
        self.require_supported()
        if temporary_handle.moved:
            raise IntegrityError("Windows temporary was already published")
        existing = self._open_any(parent, destination)
        backup_name: Optional[str] = None
        backup_handle: Optional[_WindowsHandle] = None
        if existing is not None:
            try:
                self._assert_regular(existing, f"Windows publication destination {destination}")
                backup_name = self._new_name(parent, f".{destination}.old-")
                backup_handle = existing
                self.api.rename(existing, parent, backup_name, replace=False)
            except Exception:
                self.api.close(existing)
                raise
        try:
            self.api.rename(temporary_handle, parent, destination, replace=False)
            published = self._open_any(parent, destination)
            if published is None or published.identity != temporary_handle.identity:
                self.api.close(published)
                raise IntegrityError("Windows destination identity changed after publication")
            self.api.flush(parent)
            self.api.close(published)
        except Exception:
            if backup_handle is not None:
                current = self._open_any(parent, destination)
                try:
                    if current is None:
                        self.api.rename(backup_handle, parent, destination, replace=False)
                finally:
                    self.api.close(current)
            self.api.close(backup_handle)
            raise
        if backup_handle is not None:
            try:
                self.api.delete(backup_handle)
                self.api.flush(parent)
            finally:
                self.api.close(backup_handle)

    def _ensure_relative_directory(
        self,
        root: _WindowsHandle,
        components: Sequence[str],
        *,
        create: bool,
    ) -> _WindowsHandle:
        return self.api.open_relative_directory(root, components, create=create)

    def _copy_source_to_handle(
        self,
        source: _WindowsSource,
        destination: _WindowsHandle,
        expected_size: int,
        expected_sha256: str,
    ) -> None:
        digest = hashlib.sha256()
        with self.api.open_stream(source.handle, "rb") as input_stream, self.api.open_stream(
            destination, "wb"
        ) as output_stream:
            _copy_stream(input_stream, output_stream, expected_size=expected_size, digest=digest)
            output_stream.flush()
        self.api.flush(destination)
        if digest.hexdigest() != expected_sha256:
            raise IntegrityError("Windows source changed after verification")

    def materialize_executable(
        self,
        source: _WindowsSource,
        output: Path,
        record: Mapping[str, object],
    ) -> None:
        self.require_supported()
        parent, destination = self.api.open_parent_path(output, create=True)
        temporary_name = self._new_name(parent, f".{destination}.")
        temporary: Optional[_WindowsHandle] = None
        try:
            temporary = self.api.open_child(parent, temporary_name, directory=False, create=True, exclusive=True)
            self._copy_source_to_handle(
                source,
                temporary,
                int(record["size"]),
                str(record["sha256"]),
            )
            self.commit_file(parent, temporary_name, temporary, destination)
            self.api.close(temporary)
            temporary = None
        finally:
            if temporary is not None and not temporary.moved:
                try:
                    self.api.delete(temporary)
                except Exception:
                    pass
            self.api.close(temporary)
            self.api.close(parent)

    def _create_stage_directory(self, parent: _WindowsHandle, output_name: str) -> Tuple[str, _WindowsHandle]:
        name = self._new_name(parent, f".{output_name}.new-")
        return name, self.api.open_child(parent, name, directory=True, create=True, exclusive=True)

    def _open_archive_member(self, stage: _WindowsHandle, normalized: str) -> Tuple[_WindowsHandle, str]:
        parts = normalized.split("/")
        parent = self._ensure_relative_directory(stage, parts[:-1], create=True)
        return parent, parts[-1]

    def _remove_stage(self, parent: _WindowsHandle, name: str, stage: _WindowsHandle) -> None:
        if stage.moved:
            return
        try:
            self.api.delete_tree(stage)
            self.api.delete(stage)
        except Exception:
            return

    def commit_directory(
        self,
        parent: _WindowsHandle,
        stage_name: str,
        stage: _WindowsHandle,
        destination: str,
    ) -> None:
        self.require_supported()
        existing = self._open_any(parent, destination)
        backup_handle: Optional[_WindowsHandle] = None
        if existing is not None:
            try:
                if not existing.is_directory:
                    raise IntegrityError(f"Windows archive output is not a directory: {destination}")
                backup_name = self._new_name(parent, f".{destination}.old-")
                backup_handle = existing
                self.api.rename(existing, parent, backup_name, replace=False)
            except Exception:
                self.api.close(existing)
                raise
        try:
            self.api.rename(stage, parent, destination, replace=False)
            published = self._open_any(parent, destination)
            if published is None or published.identity != stage.identity or not published.is_directory:
                self.api.close(published)
                raise IntegrityError("Windows archive destination identity changed after publication")
            self.api.flush(parent)
            self.api.close(published)
        except Exception:
            if backup_handle is not None:
                current = self._open_any(parent, destination)
                try:
                    if current is None:
                        self.api.rename(backup_handle, parent, destination, replace=False)
                finally:
                    self.api.close(current)
            self.api.close(backup_handle)
            raise
        if backup_handle is not None:
            try:
                self.api.delete_tree(backup_handle)
                self.api.delete(backup_handle)
                self.api.flush(parent)
            finally:
                self.api.close(backup_handle)

    def materialize_archive(
        self,
        source: _WindowsSource,
        output: Path,
        record: Mapping[str, object],
    ) -> None:
        self.require_supported()
        parent, destination = self.api.open_parent_path(output, create=True)
        stage_name, stage = self._create_stage_directory(parent, destination)
        try:
            with self.api.open_stream(source.handle, "rb") as archive_stream, zipfile.ZipFile(archive_stream) as archive:
                validated = _validate_archive(archive, record)
                names = set()
                for info, normalized, is_directory in validated:
                    folded = normalized.casefold()
                    if folded in names:
                        raise IntegrityError("case-folded Windows archive names collide")
                    names.add(folded)
                    if is_directory:
                        directory = self._ensure_relative_directory(stage, normalized.rstrip("/").split("/"), create=True)
                        self.api.close(directory)
                        continue
                    member_parent, member_name = self._open_archive_member(stage, normalized)
                    owns_member_parent = member_parent is not stage
                    try:
                        member = self.api.open_child(member_parent, member_name, directory=False, create=True, exclusive=True)
                        try:
                            with archive.open(info, "r") as input_stream, self.api.open_stream(member, "wb") as output_stream:
                                _copy_stream(input_stream, output_stream, expected_size=info.file_size)
                                output_stream.flush()
                            self.api.flush(member)
                        finally:
                            self.api.close(member)
                    finally:
                        if owns_member_parent:
                            self.api.close(member_parent)
            self.commit_directory(parent, stage_name, stage, destination)
            self.api.close(stage)
            stage = None
        finally:
            self._remove_stage(parent, stage_name, stage) if stage is not None else None
            self.api.close(stage)
            self.api.close(parent)

    def read_bounded(self, source: _WindowsSource, expected_size: int) -> bytes:
        with self.api.open_stream(source.handle, "rb") as stream:
            data = stream.read(expected_size + 1)
        if len(data) != expected_size:
            raise IntegrityError("Windows header source is truncated")
        return data

    def write_bytes(
        self,
        root: Path,
        relative_path: str,
        data: bytes,
        *,
        root_handle: Optional[_WindowsHandle] = None,
    ) -> None:
        self.require_supported()
        owns_root_handle = root_handle is None
        if root_handle is None:
            root_handle = self.api.open_directory_path(root, create=False)
        parts = relative_path.split("/")
        if not parts or any(not part for part in parts):
            if owns_root_handle:
                self.api.close(root_handle)
            raise IntegrityError(f"generated header path is not canonical: {relative_path}")
        parent = self._ensure_relative_directory(root_handle, parts[:-1], create=True)
        owns_parent = parent is not root_handle
        temporary_name = self._new_name(parent, f".{parts[-1]}.")
        temporary: Optional[_WindowsHandle] = None
        try:
            temporary = self.api.open_child(parent, temporary_name, directory=False, create=True, exclusive=True)
            with self.api.open_stream(temporary, "wb") as stream:
                stream.write(data)
                stream.flush()
            self.api.flush(temporary)
            self.commit_file(parent, temporary_name, temporary, parts[-1])
            temporary = None
        finally:
            if temporary is not None and not temporary.moved:
                try:
                    self.api.delete(temporary)
                except Exception:
                    pass
            self.api.close(temporary)
            if owns_parent:
                self.api.close(parent)
            if owns_root_handle:
                self.api.close(root_handle)

    def ensure_headers(
        self,
        root: Path,
        *,
        offline: bool,
        cache_dir: Optional[Path],
        explicit_paths: Mapping[str, Path],
    ) -> None:
        self.require_supported()
        manifest = load_manifest()
        root_handle = self.api.open_directory_path(Path(root), create=False)
        try:
            for header in manifest["headers"]:
                relative_path = str(header["path"])
                parts = relative_path.split("/")
                parent = self._ensure_relative_directory(root_handle, parts[:-1], create=False)
                owns_parent = parent is not root_handle
                try:
                    existing = self._open_any(parent, parts[-1])
                    if header["policy"] == "tracked-preserve":
                        if existing is None:
                            raise IntegrityError(f"tracked PC header is missing: {root / relative_path}")
                        try:
                            self._assert_regular(existing, f"tracked PC header {relative_path}")
                        finally:
                            self.api.close(existing)
                        continue
                    record = {"size": int(header["final_size"]), "sha256": header["final_sha256"]}
                    if existing is not None:
                        try:
                            if self._hash_handle(existing, int(record["size"])) == record["sha256"]:
                                continue
                        finally:
                            self.api.close(existing)
                    source_record = _header_record(header)
                    source = self.obtain_verified_source(
                        source_record,
                        cache_dir=cache_dir,
                        offline=offline,
                        explicit_source=explicit_paths.get(relative_path),
                    )
                    try:
                        source_data = self.read_bounded(source, int(header["source_size"]))
                    finally:
                        source.close()
                    if hashlib.sha256(source_data).hexdigest() != header["source_sha256"]:
                        raise IntegrityError("header source changed after verification")
                    final_data = _transform_gbi(source_data) if header["gbi_patch_applied"] else source_data
                    if len(final_data) != int(header["final_size"]) or hashlib.sha256(final_data).hexdigest() != header["final_sha256"]:
                        raise IntegrityError(f"header transform digest mismatch for {header['path']}")
                    self.write_bytes(
                        Path(root),
                        relative_path,
                        final_data,
                        root_handle=root_handle,
                    )
                finally:
                    if owns_parent:
                        self.api.close(parent)
        finally:
            self.api.close(root_handle)


def _windows_publication_backend() -> _WindowsPublicationBackend:
    if os.name != "nt":
        raise IntegrityError("native Windows handle backend is unavailable on this host")
    return _WindowsPublicationBackend()


def _open_archive_directory_fd(
    staged: Path,
    parts: Sequence[str],
    *,
    root_fd: Optional[int] = None,
    create: bool = True,
) -> int:
    """Open/create a directory using only descriptor-relative no-follow steps."""
    _require_archive_dirfd_support()
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    current_fd: Optional[int] = None
    try:
        current_fd = (
            os.dup(root_fd)
            if root_fd is not None
            else _open_directory_path(staged, create=create)
        )
        for part in parts:
            try:
                next_fd = os.open(part, flags, dir_fd=current_fd)
            except FileNotFoundError:
                if not create:
                    raise
                try:
                    os.mkdir(part, 0o755, dir_fd=current_fd)
                except FileExistsError:
                    pass
                next_fd = os.open(part, flags, dir_fd=current_fd)
            _close_descriptor(current_fd)
            current_fd = next_fd
        result = current_fd
        current_fd = None
        return result
    except FileNotFoundError:
        raise
    except OSError as exc:
        raise IntegrityError("archive directory path was substituted") from exc
    finally:
        _close_descriptor(current_fd)


def _ensure_archive_directories(
    staged: Path,
    normalized: str,
    *,
    root_fd: Optional[int] = None,
) -> Tuple[Path, int]:
    """Create only non-symlink directories beneath the private staging root."""
    target = staged / normalized
    parts = target.relative_to(staged).parts
    directory_fd = _open_archive_directory_fd(staged, parts, root_fd=root_fd)
    return target, directory_fd


def _open_archive_member(
    staged: Path,
    normalized: str,
    *,
    root_fd: Optional[int] = None,
) -> BinaryIO:
    """Create a regular archive member without following path substitutions."""
    _require_archive_dirfd_support()
    target = staged / normalized
    parts = target.relative_to(staged).parts
    if not parts:
        raise IntegrityError(f"archive member path is empty: {normalized}")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    parent_fd = _open_archive_directory_fd(staged, parts[:-1], root_fd=root_fd)
    descriptor: Optional[int] = None
    try:
        descriptor = os.open(parts[-1], flags, 0o600, dir_fd=parent_fd)
        stream = os.fdopen(descriptor, "wb")
        descriptor = None
        return stream
    except OSError as exc:
        _close_descriptor(descriptor)
        raise IntegrityError(f"archive member path was substituted: {normalized}") from exc
    finally:
        _close_descriptor(parent_fd)


def _assert_directory_identity(path: Path, descriptor: int) -> None:
    try:
        path_stat = os.stat(path, follow_symlinks=False)
        descriptor_stat = os.fstat(descriptor)
    except OSError as exc:
        raise IntegrityError(f"staged archive directory is unavailable: {path}") from exc
    if (path_stat.st_dev, path_stat.st_ino) != (descriptor_stat.st_dev, descriptor_stat.st_ino):
        raise IntegrityError(f"staged archive root was substituted: {path}")


def _descriptor_matches_stat(descriptor: int, expected_stat: os.stat_result, path: Path) -> None:
    try:
        descriptor_stat = os.fstat(descriptor)
    except OSError as exc:
        raise IntegrityError(f"staged archive entry is unavailable: {path}") from exc
    if (descriptor_stat.st_dev, descriptor_stat.st_ino) != (
        expected_stat.st_dev,
        expected_stat.st_ino,
    ):
        raise IntegrityError(f"staged archive entry was substituted: {path}")


def _hash_open_descriptor(
    descriptor: int,
    expected_size: int,
    expected_sha256: str,
    path: Path,
) -> None:
    digest = hashlib.sha256()
    count = 0
    try:
        with os.fdopen(os.dup(descriptor), "rb") as stream:
            while True:
                chunk = stream.read(CHUNK_SIZE)
                if not chunk:
                    break
                count += len(chunk)
                if count > expected_size:
                    raise IntegrityError(f"staged archive file is larger than expected: {path}")
                digest.update(chunk)
    except OSError as exc:
        raise IntegrityError(f"staged archive file is unavailable: {path}") from exc
    if count != expected_size or digest.hexdigest() != expected_sha256:
        raise IntegrityError(f"staged archive file content was substituted: {path}")


def _validate_staged_tree(
    staged: Path,
    *,
    root_fd: Optional[int] = None,
    expected: Optional[Mapping[str, Mapping[str, object]]] = None,
) -> None:
    """Reject substitutions and, for archives, verify immutable expected content."""
    _require_archive_dirfd_support()
    directory_fd = _open_archive_directory_fd(staged, (), root_fd=root_fd, create=False)
    try:
        _assert_directory_identity(staged, directory_fd)
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        seen_entries = set()

        def visit(current_fd: int, directory: Path) -> None:
            try:
                entries = list(os.scandir(current_fd))
            except OSError as exc:
                raise IntegrityError(f"staged archive directory is unavailable: {directory}") from exc
            for entry in entries:
                entry_path = directory / entry.name
                try:
                    relative_name = entry_path.relative_to(staged).as_posix()
                    seen_entries.add(relative_name)
                    expected_entry = expected.get(relative_name) if expected is not None else None
                    entry_stat = entry.stat(follow_symlinks=False)
                    file_type = stat.S_IFMT(entry_stat.st_mode)
                    if file_type == stat.S_IFLNK:
                        raise IntegrityError(f"staged archive path was substituted: {entry_path}")
                    if file_type == stat.S_IFDIR:
                        if expected is not None and (
                            expected_entry is None or expected_entry["kind"] != "directory"
                        ):
                            raise IntegrityError(f"unexpected staged archive directory: {entry_path}")
                        child_fd = os.open(entry.name, flags, dir_fd=current_fd)
                        try:
                            _descriptor_matches_stat(child_fd, entry_stat, entry_path)
                            if not stat.S_ISDIR(os.fstat(child_fd).st_mode):
                                raise IntegrityError(f"staged archive path was substituted: {entry_path}")
                            if expected is not None and stat.S_IMODE(entry_stat.st_mode) != int(
                                expected_entry["mode"]
                            ):
                                raise IntegrityError(f"staged archive directory mode changed: {entry_path}")
                            visit(child_fd, entry_path)
                            _assert_child_identity(current_fd, entry.name, child_fd)
                        finally:
                            _close_descriptor(child_fd)
                    elif file_type == stat.S_IFREG:
                        if expected is not None and (
                            expected_entry is None or expected_entry["kind"] != "file"
                        ):
                            raise IntegrityError(f"unexpected staged archive file: {entry_path}")
                        file_fd = _open_regular_descriptor_at(
                            current_fd,
                            entry.name,
                            f"staged archive contains a special entry: {entry_path}",
                        )
                        try:
                            _descriptor_matches_stat(file_fd, entry_stat, entry_path)
                            if not stat.S_ISREG(os.fstat(file_fd).st_mode):
                                raise IntegrityError(f"staged archive contains a special entry: {entry_path}")
                            if expected is not None:
                                if stat.S_IMODE(entry_stat.st_mode) != int(expected_entry["mode"]):
                                    raise IntegrityError(f"staged archive file mode changed: {entry_path}")
                                _hash_open_descriptor(
                                    file_fd,
                                    int(expected_entry["size"]),
                                    str(expected_entry["sha256"]),
                                    entry_path,
                                )
                        finally:
                            _close_descriptor(file_fd)
                    else:
                        raise IntegrityError(f"staged archive contains a special entry: {entry_path}")
                except IntegrityError:
                    raise
                except OSError as exc:
                    raise IntegrityError(f"staged archive path was substituted: {entry_path}") from exc

        visit(directory_fd, staged)
        _assert_directory_identity(staged, directory_fd)
        if expected is not None:
            if seen_entries != set(expected):
                raise IntegrityError("staged archive tree differs from the immutable archive policy")
    finally:
        _close_descriptor(directory_fd)


def _open_directory_path(path: Path, *, create: bool) -> int:
    """Open a path by descriptor-relative no-follow steps from a stable root."""
    _require_archive_dirfd_support()
    lexical = Path(os.path.abspath(os.fspath(path)))
    resolved = Path(os.path.realpath(os.fspath(lexical)))
    if lexical != resolved:
        # macOS exposes /var and /tmp as exact host-owned aliases.  Allow only
        # the one-to-one alias mapping; a project-owned symlink in the parent
        # chain, including a nested redirection under /tmp or /var, fails
        # closed instead of being resolved into a publication target.
        aliases = ((Path("/var"), Path("/private/var")), (Path("/tmp"), Path("/private/tmp")))
        exact_alias = False
        for alias, target in aliases:
            if alias == lexical:
                exact_alias = resolved == target
                break
            if alias in lexical.parents:
                suffix = lexical.relative_to(alias)
                exact_alias = resolved == target / suffix
                break
        if not exact_alias:
            raise IntegrityError(f"directory path contains a symlink: {path}")
    absolute = resolved
    anchor = Path(absolute.anchor)
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    current_fd: Optional[int] = None
    try:
        current_fd = os.open(anchor, flags)
        for part in absolute.relative_to(anchor).parts:
            try:
                next_fd = os.open(part, flags, dir_fd=current_fd)
            except FileNotFoundError:
                if not create:
                    raise
                try:
                    os.mkdir(part, 0o755, dir_fd=current_fd)
                except FileExistsError:
                    pass
                next_fd = os.open(part, flags, dir_fd=current_fd)
            _close_descriptor(current_fd)
            current_fd = next_fd
        result = current_fd
        current_fd = None
        return result
    except FileNotFoundError:
        raise
    except OSError as exc:
        raise IntegrityError(f"directory path was substituted: {path}") from exc
    finally:
        _close_descriptor(current_fd)


def _open_regular_file(path: Path) -> Tuple[int, int]:
    """Open a regular file below a stable, descriptor-anchored parent."""
    try:
        parent_fd = _open_directory_path(path.parent, create=False)
    except FileNotFoundError as exc:
        raise IntegrityError(f"verified input is not a regular file: {path}") from exc
    descriptor: Optional[int] = None
    try:
        descriptor = _open_regular_descriptor_at(
            parent_fd,
            path.name,
            f"verified input is not a regular file: {path}",
        )
        return descriptor, parent_fd
    except FileNotFoundError as exc:
        _close_descriptor(descriptor)
        descriptor = None
        raise IntegrityError(f"verified input is not a regular file: {path}") from exc
    except IntegrityError:
        _close_descriptor(descriptor)
        descriptor = None
        raise
    except OSError as exc:
        _close_descriptor(descriptor)
        descriptor = None
        raise IntegrityError(f"verified input is not a regular file: {path}") from exc
    finally:
        if descriptor is None:
            _close_descriptor(parent_fd)


def _open_regular_descriptor_at(parent_fd: int, name: str, error: str) -> int:
    """Open a regular descriptor without blocking on a FIFO or device node."""
    nonblocking = getattr(os, "O_NONBLOCK", 0)
    if not nonblocking or fcntl is None:
        raise IntegrityError("regular-file validation cannot establish nonblocking open semantics")
    descriptor: Optional[int] = None
    try:
        descriptor = os.open(
            name,
            os.O_RDONLY | os.O_NOFOLLOW | nonblocking,
            dir_fd=parent_fd,
        )
        descriptor_stat = os.fstat(descriptor)
        if not stat.S_ISREG(descriptor_stat.st_mode):
            raise IntegrityError(error)
        try:
            current_flags = fcntl.fcntl(descriptor, fcntl.F_GETFL)
            fcntl.fcntl(descriptor, fcntl.F_SETFL, current_flags & ~nonblocking)
        except OSError as exc:
            raise IntegrityError(f"regular-file descriptor could not be restored to blocking mode: {name}") from exc
        return descriptor
    except FileNotFoundError:
        _close_descriptor(descriptor)
        raise
    except IntegrityError:
        _close_descriptor(descriptor)
        raise
    except OSError as exc:
        _close_descriptor(descriptor)
        raise IntegrityError(error) from exc


def _make_directory_at(parent_fd: int, prefix: str) -> Tuple[str, int]:
    """Create a private directory and retain its descriptor under parent_fd."""
    _require_archive_dirfd_support()
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    candidates = tempfile._get_candidate_names()
    for _ in range(100):
        name = f"{prefix}{next(candidates)}"
        try:
            os.mkdir(name, 0o700, dir_fd=parent_fd)
        except FileExistsError:
            continue
        try:
            return name, os.open(name, flags, dir_fd=parent_fd)
        except OSError as exc:
            try:
                os.rmdir(name, dir_fd=parent_fd)
            except OSError:
                pass
            raise IntegrityError(f"private directory path was substituted: {name}") from exc
    raise IntegrityError(f"could not create a private directory below descriptor {parent_fd}")


def _remove_directory_fd(directory_fd: int) -> None:
    """Remove a directory tree through descriptor-relative no-follow operations."""
    _require_archive_dirfd_support()
    directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        entries = list(os.scandir(directory_fd))
    except OSError as exc:
        raise IntegrityError("private directory became unavailable during cleanup") from exc
    for entry in entries:
        try:
            entry_stat = entry.stat(follow_symlinks=False)
            file_type = stat.S_IFMT(entry_stat.st_mode)
            if file_type == stat.S_IFDIR:
                child_fd = os.open(entry.name, directory_flags, dir_fd=directory_fd)
                try:
                    _descriptor_matches_stat(
                        child_fd,
                        entry_stat,
                        Path(entry.name),
                    )
                    _remove_directory_fd(child_fd)
                    _assert_child_identity(directory_fd, entry.name, child_fd)
                finally:
                    _close_descriptor(child_fd)
                os.rmdir(entry.name, dir_fd=directory_fd)
            elif file_type in (stat.S_IFLNK, stat.S_IFREG):
                current_stat = os.stat(entry.name, dir_fd=directory_fd, follow_symlinks=False)
                if (current_stat.st_dev, current_stat.st_ino) != (
                    entry_stat.st_dev,
                    entry_stat.st_ino,
                ):
                    raise IntegrityError(f"private directory entry was substituted: {entry.name}")
                os.unlink(entry.name, dir_fd=directory_fd)
            else:
                raise IntegrityError(f"private directory contains a special entry: {entry.name}")
        except IntegrityError:
            raise
        except OSError as exc:
            raise IntegrityError(f"private directory cleanup was substituted: {entry.name}") from exc


def _remove_directory_at(parent_fd: int, name: str) -> None:
    directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    directory_fd = os.open(name, directory_flags, dir_fd=parent_fd)
    try:
        _remove_directory_fd(directory_fd)
    finally:
        _close_descriptor(directory_fd)
    os.rmdir(name, dir_fd=parent_fd)


def _cleanup_staged_directory(parent_fd: int, name: str, directory_fd: int) -> None:
    """Clean an unpublished stage without deleting a directory after rename."""
    try:
        current = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
    except FileNotFoundError:
        # The descriptor may now refer to the published output after a
        # successful rename.  Never remove through that descriptor here.
        return
    except OSError:
        return
    descriptor_stat = os.fstat(directory_fd)
    if (current.st_dev, current.st_ino) != (descriptor_stat.st_dev, descriptor_stat.st_ino):
        if stat.S_ISLNK(current.st_mode):
            try:
                os.unlink(name, dir_fd=parent_fd)
            except OSError:
                pass
        return
    _remove_directory_fd(directory_fd)
    os.rmdir(name, dir_fd=parent_fd)


def _assert_child_identity(parent_fd: int, name: str, descriptor: int) -> None:
    try:
        path_stat = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        descriptor_stat = os.fstat(descriptor)
    except OSError as exc:
        raise IntegrityError(f"private directory path is unavailable: {name}") from exc
    if (path_stat.st_dev, path_stat.st_ino) != (descriptor_stat.st_dev, descriptor_stat.st_ino):
        raise IntegrityError(f"private directory path was substituted: {name}")


def _unique_entry_name(parent_fd: int, prefix: str) -> str:
    """Choose an absent entry name below a stable parent descriptor."""
    candidates = tempfile._get_candidate_names()
    for _ in range(100):
        name = f"{prefix}{next(candidates)}"
        try:
            os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        except FileNotFoundError:
            return name
        except OSError as exc:
            raise IntegrityError(f"publication backup path is unavailable: {name}") from exc
    raise IntegrityError(f"could not choose a private publication name below descriptor {parent_fd}")


def _open_existing_directory_at(
    parent_fd: int,
    name: str,
    expected_stat: os.stat_result,
) -> int:
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        descriptor = os.open(name, flags, dir_fd=parent_fd)
    except OSError as exc:
        raise IntegrityError(f"publication directory is unavailable: {name}") from exc
    try:
        _descriptor_matches_stat(descriptor, expected_stat, Path(name))
        if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
            raise IntegrityError(f"publication output is not a directory: {name}")
        return descriptor
    except Exception:
        _close_descriptor(descriptor)
        raise


def _open_existing_file_at(
    parent_fd: int,
    name: str,
    expected_stat: os.stat_result,
) -> int:
    descriptor = _open_regular_descriptor_at(
        parent_fd,
        name,
        f"publication output is not a regular file: {name}",
    )
    try:
        _descriptor_matches_stat(descriptor, expected_stat, Path(name))
        return descriptor
    except Exception:
        _close_descriptor(descriptor)
        raise


def _restore_backup_at(parent_fd: int, backup_name: str, destination: str) -> bool:
    """Restore a backup when the destination is absent or is our no-follow symlink artifact."""
    try:
        destination_stat = os.stat(destination, dir_fd=parent_fd, follow_symlinks=False)
    except FileNotFoundError:
        try:
            os.rename(
                backup_name,
                destination,
                src_dir_fd=parent_fd,
                dst_dir_fd=parent_fd,
            )
            return True
        except OSError:
            return False
    except OSError:
        return False
    if stat.S_ISLNK(destination_stat.st_mode):
        try:
            # Removing a symlink entry never follows or mutates its target;
            # this restores the old output while retaining no unknown object.
            os.unlink(destination, dir_fd=parent_fd)
            os.rename(
                backup_name,
                destination,
                src_dir_fd=parent_fd,
                dst_dir_fd=parent_fd,
            )
            return True
        except OSError:
            return False
    return False


def _remove_recovery_symlink_at(parent_fd: int, destination: str) -> None:
    """Remove only a symlink entry left by a failed no-follow publication."""
    try:
        destination_stat = os.stat(destination, dir_fd=parent_fd, follow_symlinks=False)
    except OSError:
        return
    if stat.S_ISLNK(destination_stat.st_mode):
        try:
            os.unlink(destination, dir_fd=parent_fd)
        except OSError:
            pass


def _remove_backup_file_at(parent_fd: int, backup_name: str, backup_fd: int) -> None:
    """Delete a file backup only while its pathname still names the pinned file."""
    try:
        _assert_child_identity(parent_fd, backup_name, backup_fd)
    except IntegrityError as exc:
        raise IntegrityError(
            f"publication backup identity changed; recovery artifact retained: {backup_name}"
        ) from exc
    try:
        os.unlink(backup_name, dir_fd=parent_fd)
    except FileNotFoundError:
        return
    except OSError as exc:
        raise IntegrityError(f"publication backup cleanup failed: {backup_name}") from exc


def _remove_backup_directory_at(parent_fd: int, backup_name: str, backup_fd: int) -> None:
    """Clean a directory backup through its retained descriptor before removal."""
    _remove_directory_fd(backup_fd)
    try:
        _assert_child_identity(parent_fd, backup_name, backup_fd)
    except IntegrityError as exc:
        raise IntegrityError(
            f"publication directory backup identity changed; recovery artifact retained: {backup_name}"
        ) from exc
    try:
        os.rmdir(backup_name, dir_fd=parent_fd)
    except FileNotFoundError:
        return
    except OSError as exc:
        raise IntegrityError(f"publication directory backup cleanup failed: {backup_name}") from exc


def _atomic_replace_directory(
    staged: Path,
    output: Path,
    *,
    parent_fd: int,
    staged_fd: int,
    expected: Optional[Mapping[str, Mapping[str, object]]] = None,
) -> None:
    """Atomically replace a directory using one stable descriptor-anchored parent."""
    if expected is not None:
        _validate_staged_tree(staged, root_fd=staged_fd, expected=expected)
    _assert_child_identity(parent_fd, staged.name, staged_fd)
    try:
        output_stat = os.stat(output.name, dir_fd=parent_fd, follow_symlinks=False)
    except FileNotFoundError:
        output_stat = None
    except OSError as exc:
        raise IntegrityError(f"archive output is unavailable: {output}") from exc
    if output_stat is not None:
        if stat.S_ISLNK(output_stat.st_mode):
            raise IntegrityError(f"refusing to replace symlink output {output}")
        if not stat.S_ISDIR(output_stat.st_mode):
            raise IntegrityError(f"archive output is not a directory: {output}")

    backup_name: Optional[str] = None
    backup_fd: Optional[int] = None
    if output_stat is not None:
        backup_name = _unique_entry_name(parent_fd, f".{output.name}.old-")
        try:
            backup_fd = _open_existing_directory_at(parent_fd, output.name, output_stat)
            os.rename(output.name, backup_name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        except Exception:
            _close_descriptor(backup_fd)
            raise
    try:
        os.rename(staged.name, output.name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        _assert_child_identity(parent_fd, output.name, staged_fd)
        os.fsync(parent_fd)
    except Exception:
        if backup_name is not None:
            _restore_backup_at(parent_fd, backup_name, output.name)
        else:
            _remove_recovery_symlink_at(parent_fd, output.name)
        raise
    if backup_name is not None and backup_fd is not None:
        try:
            _remove_backup_directory_at(parent_fd, backup_name, backup_fd)
        finally:
            _close_descriptor(backup_fd)


def _add_expected_archive_directory(
    expected: Dict[str, Dict[str, object]],
    name: str,
    mode: int = 0o755,
) -> None:
    parts = name.split("/") if name else []
    for index in range(1, len(parts) + 1):
        current = "/".join(parts[:index])
        existing = expected.get(current)
        if existing is not None and existing["kind"] != "directory":
            raise IntegrityError(f"archive tree has a file/directory collision: {current}")
        if existing is None:
            expected[current] = {"kind": "directory", "mode": 0o755}
    if parts:
        expected["/".join(parts)] = {"kind": "directory", "mode": mode & 0o777}


def _add_expected_archive_file(
    expected: Dict[str, Dict[str, object]],
    name: str,
    mode: int,
    size: int,
    executable: bool,
) -> None:
    parent = name.rsplit("/", 1)[0] if "/" in name else ""
    if parent:
        _add_expected_archive_directory(expected, parent)
    existing = expected.get(name)
    if existing is not None and existing["kind"] != "file":
        raise IntegrityError(f"archive tree has a file/directory collision: {name}")
    expected[name] = {
        "kind": "file",
        "mode": 0o755 if executable else mode & 0o666,
        "size": size,
        "sha256": "",
    }


def _materialize_executable(source: Path, output: Path, record: Mapping[str, object]) -> None:
    _require_archive_dirfd_support()
    output_parent_fd = _open_directory_path(output.parent, create=True)
    descriptor: Optional[int] = None
    temporary = ""
    source_descriptor: Optional[int] = None
    source_parent_fd: Optional[int] = None
    try:
        try:
            output_stat = os.stat(output.name, dir_fd=output_parent_fd, follow_symlinks=False)
        except FileNotFoundError:
            output_stat = None
        if output_stat is not None and stat.S_ISLNK(output_stat.st_mode):
            raise IntegrityError(f"refusing to replace symlink output {output}")

        descriptor, temporary = _temporary_path_at(output_parent_fd, f".{output.name}.")
        source_descriptor, source_parent_fd = _open_regular_file(source)
        with os.fdopen(os.dup(source_descriptor), "rb") as input_stream, os.fdopen(
            descriptor, "wb", closefd=False
        ) as output_stream:
            digest = hashlib.sha256()
            _copy_stream(
                input_stream,
                output_stream,
                expected_size=int(record["size"]),
                digest=digest,
            )
            if digest.hexdigest() != record["sha256"]:
                raise IntegrityError("source changed after verification")
            output_stream.flush()
            os.fsync(output_stream.fileno())
        os.fchmod(descriptor, 0o755)
        _commit_temporary_at(descriptor, output_parent_fd, temporary, output.name)
        descriptor = None
    except Exception:
        if temporary:
            _remove_temporary_at(descriptor, output_parent_fd, temporary)
        else:
            _close_descriptor(descriptor)
        raise
    finally:
        _close_descriptor(source_descriptor)
        _close_descriptor(source_parent_fd)
        _close_descriptor(output_parent_fd)


def _materialize_archive(source: Path, output: Path, record: Mapping[str, object]) -> None:
    _require_archive_dirfd_support()
    output_parent_fd = _open_directory_path(output.parent, create=True)
    staged_name, staged_fd = _make_directory_at(output_parent_fd, f".{output.name}.new-")
    staged = output.parent / staged_name
    published = False
    source_descriptor: Optional[int] = None
    source_parent_fd: Optional[int] = None
    expected_tree: Dict[str, Dict[str, object]] = {}
    try:
        # Re-read and hash the source into a private seekable file before
        # opening it as a ZIP.  The initial cache/explicit-source check and
        # this copy must cover the same bytes; extraction must not reopen a
        # mutable caller-controlled path after verification.
        verified_descriptor = _temporary_seekable_at(output_parent_fd, ".archive-source-")
        with os.fdopen(verified_descriptor, "w+b") as verified_source:
            source_descriptor, source_parent_fd = _open_regular_file(source)
            with os.fdopen(os.dup(source_descriptor), "rb") as input_stream:
                digest = hashlib.sha256()
                _copy_stream(
                    input_stream,
                    verified_source,
                    expected_size=int(record["size"]),
                    digest=digest,
                )
                if digest.hexdigest() != record["sha256"]:
                    raise IntegrityError("archive source changed after verification")
            verified_source.flush()
            os.fsync(verified_source.fileno())
            verified_source.seek(0)
            with zipfile.ZipFile(verified_source) as archive:
                validated = _validate_archive(archive, record)
                executable_members = set(record["archive"].get("executable_members", []))
                mode_by_name = {
                    normalized: _zip_mode(info)
                    for info, normalized, _ in validated
                }
                for info, normalized, is_directory in validated:
                    if is_directory:
                        _add_expected_archive_directory(
                            expected_tree,
                            normalized.rstrip("/"),
                            mode_by_name[normalized],
                        )
                        _, directory_fd = _ensure_archive_directories(
                            staged,
                            normalized.rstrip("/"),
                            root_fd=staged_fd,
                        )
                        try:
                            os.fchmod(directory_fd, mode_by_name[normalized] & 0o777)
                        finally:
                            _close_descriptor(directory_fd)
                        continue
                    _add_expected_archive_file(
                        expected_tree,
                        normalized,
                        mode_by_name[normalized],
                        info.file_size,
                        normalized in executable_members,
                    )
                    digest = hashlib.sha256()
                    with archive.open(info, "r") as input_stream, _open_archive_member(
                        staged,
                        normalized,
                        root_fd=staged_fd,
                    ) as output_stream:
                        _copy_stream(
                            input_stream,
                            output_stream,
                            expected_size=info.file_size,
                            digest=digest,
                        )
                        output_stream.flush()
                        os.fsync(output_stream.fileno())
                        if normalized in executable_members:
                            _chmod_open_file(output_stream.fileno(), 0o755)
                        else:
                            _chmod_open_file(output_stream.fileno(), mode_by_name[normalized] & 0o666)
                    expected_tree[normalized]["sha256"] = digest.hexdigest()
        _validate_staged_tree(staged, root_fd=staged_fd, expected=expected_tree)
        _atomic_replace_directory(
            staged,
            output,
            parent_fd=output_parent_fd,
            staged_fd=staged_fd,
            expected=expected_tree,
        )
        published = True
    finally:
        if not published:
            try:
                _cleanup_staged_directory(output_parent_fd, staged_name, staged_fd)
            except FileNotFoundError:
                pass
        _close_descriptor(staged_fd)
        _close_descriptor(output_parent_fd)
        _close_descriptor(source_descriptor)
        _close_descriptor(source_parent_fd)


def download_artifact(
    tool: str,
    output: Path,
    tag: str,
    *,
    offline: Optional[bool] = None,
    cache_dir: Optional[Path] = None,
    explicit_source: Optional[Path] = None,
    system: Optional[str] = None,
    machine: Optional[str] = None,
) -> dict:
    """Acquire one manifest artifact and atomically materialize it at output."""
    if os.name == "nt":
        backend = _windows_publication_backend()
        artifact = select_artifact(tool, tag, system=system, machine=machine)
        source = backend.obtain_verified_source(
            artifact,
            cache_dir=cache_dir,
            offline=_offline_requested(offline),
            explicit_source=explicit_source,
        )
        try:
            destination = Path(output)
            if artifact["kind"] == "archive":
                backend.materialize_archive(source, destination, artifact)
            else:
                backend.materialize_executable(source, destination, artifact)
        finally:
            source.close()
        return artifact
    _require_archive_dirfd_support()
    artifact = select_artifact(tool, tag, system=system, machine=machine)
    source = _obtain_verified_source(
        artifact,
        cache_dir=cache_dir,
        offline=_offline_requested(offline),
        explicit_source=explicit_source,
    )
    destination = Path(output)
    if artifact["kind"] == "archive":
        _materialize_archive(source, destination, artifact)
    else:
        _materialize_executable(source, destination, artifact)
    return artifact


def _header_record(header: Mapping[str, object]) -> dict:
    return {
        "asset_id": f"header-{header['source_sha256'][:16]}",
        "asset_name": str(header["path"]).replace("/", "_"),
        "size": int(header["source_size"]),
        "sha256": header["source_sha256"],
        "asset_url": header["source_url"],
    }


def _transform_gbi(data: bytes) -> bytes:
    transformed, count = re.subn(
        rb"unsigned char\s+param:8;",
        b"unsigned int\tparam:8;",
        data,
    )
    if count != 1:
        raise IntegrityError(f"deterministic gbi.h transform matched {count} declarations")
    return transformed


def _temporary_path_at(
    parent_fd: int,
    prefix: str,
    *,
    read_write: bool = False,
) -> Tuple[int, str]:
    """Create a descriptor-backed temporary file below a stable parent fd."""
    _require_archive_dirfd_support()
    access = os.O_RDWR if read_write else os.O_WRONLY
    flags = access | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    candidates = tempfile._get_candidate_names()
    for _ in range(100):
        name = f"{prefix}{next(candidates)}"
        try:
            return os.open(name, flags, 0o600, dir_fd=parent_fd), name
        except FileExistsError:
            continue
        except OSError as exc:
            raise IntegrityError(f"temporary header path was substituted: {name}") from exc
    raise IntegrityError(f"could not create a temporary header below descriptor {parent_fd}")


def _temporary_seekable_at(parent_fd: int, prefix: str) -> int:
    """Create and immediately unlink a private seekable file below parent_fd."""
    descriptor, name = _temporary_path_at(parent_fd, prefix, read_write=True)
    try:
        _assert_temporary_at(descriptor, parent_fd, name)
        os.unlink(name, dir_fd=parent_fd)
        return descriptor
    except Exception:
        _remove_temporary_at(descriptor, parent_fd, name)
        raise


def _assert_temporary_at(descriptor: int, parent_fd: int, name: str) -> None:
    try:
        path_stat = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        descriptor_stat = os.fstat(descriptor)
    except OSError as exc:
        raise IntegrityError(f"temporary header is unavailable: {name}") from exc
    if (path_stat.st_dev, path_stat.st_ino) != (descriptor_stat.st_dev, descriptor_stat.st_ino):
        raise IntegrityError(f"temporary header was replaced: {name}")


def _remove_temporary_at(descriptor: Optional[int], parent_fd: int, name: str) -> None:
    _close_descriptor(descriptor)
    try:
        os.unlink(name, dir_fd=parent_fd)
    except OSError:
        pass


def _commit_temporary_at(descriptor: int, parent_fd: int, temporary: str, destination: str) -> None:
    """Atomically publish a descriptor-backed file under one stable parent fd."""
    _assert_temporary_at(descriptor, parent_fd, temporary)
    try:
        destination_stat = os.stat(destination, dir_fd=parent_fd, follow_symlinks=False)
    except FileNotFoundError:
        destination_stat = None
    except OSError as exc:
        raise IntegrityError(f"publication destination is unavailable: {destination}") from exc
    if destination_stat is not None and stat.S_ISLNK(destination_stat.st_mode):
        raise IntegrityError(f"refusing to replace symlink output {destination}")
    backup_name: Optional[str] = None
    backup_fd: Optional[int] = None
    if destination_stat is not None:
        if not stat.S_ISREG(destination_stat.st_mode):
            raise IntegrityError(f"publication destination is not a regular file: {destination}")
        backup_name = _unique_entry_name(parent_fd, f".{destination}.old-")
        try:
            backup_fd = _open_existing_file_at(parent_fd, destination, destination_stat)
            os.rename(destination, backup_name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        except Exception:
            _close_descriptor(backup_fd)
            raise
    try:
        os.rename(
            temporary,
            destination,
            src_dir_fd=parent_fd,
            dst_dir_fd=parent_fd,
        )
        _assert_temporary_at(descriptor, parent_fd, destination)
        os.fsync(parent_fd)
    except Exception:
        if backup_name is not None:
            _restore_backup_at(parent_fd, backup_name, destination)
        else:
            _remove_recovery_symlink_at(parent_fd, destination)
        raise
    finally:
        _close_descriptor(descriptor)
    if backup_name is not None and backup_fd is not None:
        try:
            _remove_backup_file_at(parent_fd, backup_name, backup_fd)
        finally:
            _close_descriptor(backup_fd)


def _assert_regular_file_at(parent_fd: int, name: str) -> None:
    """Require a regular file below a stable parent without following links."""
    descriptor: Optional[int] = None
    try:
        descriptor = _open_regular_descriptor_at(
            parent_fd,
            name,
            f"tracked PC header is not a regular file: {name}",
        )
    except FileNotFoundError:
        raise
    except IntegrityError:
        raise
    except OSError as exc:
        raise IntegrityError(f"tracked PC header is not a regular file: {name}") from exc
    finally:
        _close_descriptor(descriptor)


def _verify_file_at(parent_fd: int, name: str, record: Mapping[str, object]) -> None:
    descriptor: Optional[int] = None
    try:
        descriptor = _open_regular_descriptor_at(
            parent_fd,
            name,
            f"verified header is not a regular file: {name}",
        )
    except FileNotFoundError:
        raise
    try:
        digest = hashlib.sha256()
        count = 0
        with os.fdopen(os.dup(descriptor), "rb") as stream:
            while True:
                chunk = stream.read(CHUNK_SIZE)
                if not chunk:
                    break
                count += len(chunk)
                if count > int(record["size"]):
                    raise IntegrityError(f"{name} is larger than its manifest size")
                digest.update(chunk)
        if count != int(record["size"]):
            raise IntegrityError(f"{name} has an unexpected size")
        if digest.hexdigest() != record["sha256"]:
            raise IntegrityError(f"SHA-256 mismatch for {name}")
    finally:
        _close_descriptor(descriptor)


def _atomic_write_bytes(
    root: Path,
    relative_path: str,
    data: bytes,
    *,
    root_fd: Optional[int] = None,
) -> None:
    """Publish generated bytes through a descriptor-relative no-follow transaction."""
    if os.name == "nt":
        _WindowsPublicationBackend().write_bytes(root, relative_path, data)
        return
    relative = Path(relative_path)
    if relative.is_absolute() or not relative.parts or any(part in ("", ".", "..") for part in relative.parts):
        raise IntegrityError(f"generated header path is not canonical: {relative_path}")
    parent_fd = _open_archive_directory_fd(
        root,
        relative.parts[:-1],
        root_fd=root_fd,
        create=True,
    )
    descriptor: Optional[int] = None
    temporary = ""
    try:
        descriptor, temporary = _temporary_path_at(parent_fd, f".{relative.name}.")
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        _commit_temporary_at(descriptor, parent_fd, temporary, relative.name)
        descriptor = None
    except Exception:
        if temporary:
            _remove_temporary_at(descriptor, parent_fd, temporary)
        else:
            _close_descriptor(descriptor)
        raise
    finally:
        _close_descriptor(parent_fd)


def _read_bounded(path: Path, expected_size: int) -> bytes:
    descriptor, parent_fd = _open_regular_file(path)
    try:
        with os.fdopen(os.dup(descriptor), "rb") as stream:
            data = stream.read(expected_size + 1)
    finally:
        _close_descriptor(descriptor)
        _close_descriptor(parent_fd)
    if len(data) != expected_size:
        raise IntegrityError(f"{path} is truncated")
    return data


def ensure_headers(
    root: Path = Path("."),
    *,
    offline: Optional[bool] = None,
    cache_dir: Optional[Path] = None,
    explicit_paths: Optional[Mapping[str, Path]] = None,
) -> None:
    """Preserve tracked PC headers and verify/fetch only generated stdlib.h."""
    if os.name == "nt":
        _WindowsPublicationBackend().ensure_headers(
            Path(root),
            offline=_offline_requested(offline),
            cache_dir=cache_dir,
            explicit_paths=explicit_paths or {},
        )
        return
    _require_archive_dirfd_support()
    manifest = load_manifest()
    offline_mode = _offline_requested(offline)
    explicit_paths = explicit_paths or {}
    try:
        generated_root_fd = _open_directory_path(Path(root), create=False)
    except FileNotFoundError as exc:
        raise IntegrityError(f"header root is unavailable: {root}") from exc
    try:
        for header in manifest["headers"]:
            relative_path = str(header["path"])
            relative = Path(relative_path)
            policy = header["policy"]
            try:
                existing_parent_fd = _open_archive_directory_fd(
                    Path(root),
                    relative.parts[:-1],
                    root_fd=generated_root_fd,
                    create=False,
                )
            except FileNotFoundError:
                existing_parent_fd = None

            if policy == "tracked-preserve":
                if existing_parent_fd is None:
                    raise IntegrityError(f"tracked PC header is missing: {Path(root) / relative_path}")
                try:
                    _assert_regular_file_at(existing_parent_fd, relative.name)
                except FileNotFoundError as exc:
                    raise IntegrityError(
                        f"tracked PC header is missing: {Path(root) / relative_path}"
                    ) from exc
                finally:
                    _close_descriptor(existing_parent_fd)
                continue

            record = {
                "size": int(header["final_size"]),
                "sha256": header["final_sha256"],
            }
            if existing_parent_fd is not None:
                try:
                    try:
                        _verify_file_at(existing_parent_fd, relative.name, record)
                    except FileNotFoundError:
                        pass
                    else:
                        continue
                finally:
                    _close_descriptor(existing_parent_fd)

            source_path = explicit_paths.get(relative_path)
            record = _header_record(header)
            source = _obtain_verified_source(
                record,
                cache_dir=cache_dir,
                offline=offline_mode,
                explicit_source=source_path,
            )
            source_data = _read_bounded(source, int(header["source_size"]))
            if hashlib.sha256(source_data).hexdigest() != header["source_sha256"]:
                raise IntegrityError("header source changed after verification")
            final_data = _transform_gbi(source_data) if header["gbi_patch_applied"] else source_data
            if (
                len(final_data) != int(header["final_size"])
                or hashlib.sha256(final_data).hexdigest() != header["final_sha256"]
            ):
                raise IntegrityError(f"header transform digest mismatch for {header['path']}")
            _atomic_write_bytes(
                Path(root),
                relative_path,
                final_data,
                root_fd=generated_root_fd,
            )
    finally:
        _close_descriptor(generated_root_fd)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", choices=sorted(TOOLS))
    parser.add_argument("output", type=Path)
    parser.add_argument("--tag", required=True, help="pinned release tag checked against the manifest")
    parser.add_argument("--offline", action="store_true", help="use only a verified cache or --source")
    parser.add_argument("--cache-dir", type=Path, help="verified artifact cache directory")
    parser.add_argument("--source", type=Path, help="explicit local artifact, still verified against the manifest")
    args = parser.parse_args()

    try:
        artifact = download_artifact(
            args.tool,
            args.output,
            args.tag,
            offline=True if args.offline else None,
            cache_dir=args.cache_dir,
            explicit_source=args.source,
        )
    except DownloadError as exc:
        raise SystemExit(f"verified download refused: {exc}") from exc
    print(
        f"Verified {artifact['tool']} {artifact['asset_name']} "
        f"({artifact['size']} bytes, sha256:{artifact['sha256']}) -> {args.output}"
    )


if __name__ == "__main__":
    main()
