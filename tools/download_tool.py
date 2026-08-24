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
import hashlib
import json
import ntpath
import os
import platform
import re
import stat
import tempfile
import urllib.error
import urllib.request
import zipfile
from collections.abc import Mapping as MappingABC
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Callable, Dict, Mapping, Optional, Sequence, Tuple


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

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 28:
        raise ManifestError("manifest must contain the current 28 public artifacts")
    seen_assets = set()
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
        seen_assets.add(asset_key)
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
        header_paths.add(header["path"])
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


def _temporary_path(parent: Path, prefix: str) -> Tuple[int, Path]:
    """Create a private temporary file and retain its descriptor ownership."""
    descriptor, name = tempfile.mkstemp(prefix=prefix, dir=parent)
    return descriptor, Path(name)


def _close_descriptor(descriptor: Optional[int]) -> None:
    if descriptor is None:
        return
    try:
        os.close(descriptor)
    except OSError:
        pass


def _remove_temporary(descriptor: Optional[int], path: Path) -> None:
    _close_descriptor(descriptor)
    try:
        path.unlink()
    except OSError:
        pass


def _assert_temporary_path(descriptor: int, path: Path) -> None:
    """Reject replacement of the mkstemp name before it is renamed."""
    try:
        path_stat = os.stat(path, follow_symlinks=False)
        descriptor_stat = os.fstat(descriptor)
    except OSError as exc:
        raise IntegrityError(f"temporary destination is unavailable: {path}") from exc
    if (path_stat.st_dev, path_stat.st_ino) != (descriptor_stat.st_dev, descriptor_stat.st_ino):
        raise IntegrityError(f"temporary destination was replaced: {path}")


def _commit_temporary(descriptor: int, path: Path, destination: Path) -> None:
    """Verify and atomically rename a descriptor-backed temporary file."""
    _assert_temporary_path(descriptor, path)
    # Keep the descriptor open through all writes, flushes, fsyncs, and the
    # POSIX rename. This lets the destination inode be checked after rename.
    if os.name == "posix":
        try:
            os.replace(path, destination)
            try:
                _assert_temporary_path(descriptor, destination)
            except IntegrityError:
                if destination.is_symlink():
                    try:
                        destination.unlink()
                    except OSError:
                        pass
                raise
        finally:
            _close_descriptor(descriptor)
        return

    # Windows does not permit replacing an open temporary file. The descriptor
    # and pathname are still checked together before the required close/rename.
    os.close(descriptor)
    os.replace(path, destination)


def _chmod_open_file(descriptor: int, path: Path, mode: int) -> None:
    """Set mode through the open descriptor, with a checked Windows fallback."""
    if hasattr(os, "fchmod"):
        os.fchmod(descriptor, mode)
        return
    _assert_temporary_path(descriptor, path)
    os.chmod(path, mode)


def _hash_file(path: Path, expected_size: int) -> str:
    digest = hashlib.sha256()
    count = 0
    try:
        with path.open("rb") as stream:
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
    if count != expected_size:
        raise IntegrityError(f"{path} is truncated: expected {expected_size}, got {count}")
    return digest.hexdigest()


def _verify_file(path: Path, record: Mapping[str, object]) -> None:
    if path.is_symlink() or not path.is_file():
        raise IntegrityError(f"verified input is not a regular file: {path}")
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
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = _temporary_path(cache_path.parent, ".download-")
    try:
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
        _commit_temporary(descriptor, temporary, cache_path)
        descriptor = None
    except (urllib.error.URLError, OSError) as exc:
        _remove_temporary(descriptor, temporary)
        descriptor = None
        raise DownloadError(f"network fetch failed for {record['asset_name']}") from exc
    except Exception:
        _remove_temporary(descriptor, temporary)
        descriptor = None
        raise
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
    if not _archive_dirfd_supported():
        raise IntegrityError(
            "archive/header publication requires descriptor-relative no-follow primitives"
        )


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
        current_fd = os.dup(root_fd) if root_fd is not None else os.open(staged, flags)
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


def _validate_staged_tree(staged: Path, *, root_fd: Optional[int] = None) -> None:
    """Reject any symlink or special entry using descriptor-anchored traversal."""
    _require_archive_dirfd_support()
    directory_fd = _open_archive_directory_fd(staged, (), root_fd=root_fd, create=False)
    try:
        _assert_directory_identity(staged, directory_fd)
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        file_flags = os.O_RDONLY | os.O_NOFOLLOW

        def visit(current_fd: int, directory: Path) -> None:
            try:
                entries = list(os.scandir(current_fd))
            except OSError as exc:
                raise IntegrityError(f"staged archive directory is unavailable: {directory}") from exc
            for entry in entries:
                entry_path = directory / entry.name
                try:
                    if entry.is_symlink():
                        raise IntegrityError(f"staged archive path was substituted: {entry_path}")
                    if entry.is_dir(follow_symlinks=False):
                        child_fd = os.open(entry.name, flags, dir_fd=current_fd)
                        try:
                            if not stat.S_ISDIR(os.fstat(child_fd).st_mode):
                                raise IntegrityError(f"staged archive path was substituted: {entry_path}")
                            visit(child_fd, entry_path)
                        finally:
                            _close_descriptor(child_fd)
                    elif entry.is_file(follow_symlinks=False):
                        file_fd = os.open(entry.name, file_flags, dir_fd=current_fd)
                        try:
                            if not stat.S_ISREG(os.fstat(file_fd).st_mode):
                                raise IntegrityError(f"staged archive contains a special entry: {entry_path}")
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
    finally:
        _close_descriptor(directory_fd)


def _open_directory_path(path: Path, *, create: bool) -> int:
    """Open a path by descriptor-relative no-follow steps from a stable root."""
    _require_archive_dirfd_support()
    lexical = Path(os.path.abspath(os.fspath(path)))
    resolved = Path(os.path.realpath(os.fspath(lexical)))
    if lexical != resolved:
        # macOS exposes /var and /tmp as host-owned aliases.  Allow only those
        # fixed aliases; a project-owned symlink in the parent chain fails
        # closed instead of being resolved into a publication target.
        aliases = ((Path("/var"), Path("/private/var")), (Path("/tmp"), Path("/private/tmp")))
        if not any(
            lexical == alias or alias in lexical.parents
            for alias, _target in aliases
        ) or not any(
            resolved == target or target in resolved.parents
            for _alias, target in aliases
        ):
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
            if entry.is_dir(follow_symlinks=False):
                child_fd = os.open(entry.name, directory_flags, dir_fd=directory_fd)
                try:
                    _remove_directory_fd(child_fd)
                finally:
                    _close_descriptor(child_fd)
                os.rmdir(entry.name, dir_fd=directory_fd)
            elif entry.is_symlink() or entry.is_file(follow_symlinks=False):
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


def _atomic_replace_directory(
    staged: Path,
    output: Path,
    *,
    parent_fd: int,
    staged_fd: int,
) -> None:
    """Atomically replace a directory using one stable descriptor-anchored parent."""
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
    if output_stat is not None:
        backup_name, backup_fd = _make_directory_at(parent_fd, f".{output.name}.old-")
        _close_descriptor(backup_fd)
        os.rmdir(backup_name, dir_fd=parent_fd)
        os.rename(output.name, backup_name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
    try:
        os.rename(staged.name, output.name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        try:
            _assert_child_identity(parent_fd, output.name, staged_fd)
        except IntegrityError:
            try:
                output_stat = os.stat(output.name, dir_fd=parent_fd, follow_symlinks=False)
                if stat.S_ISLNK(output_stat.st_mode):
                    os.unlink(output.name, dir_fd=parent_fd)
            except OSError:
                pass
            raise
        os.fsync(parent_fd)
    except Exception:
        if backup_name is not None:
            try:
                os.stat(output.name, dir_fd=parent_fd, follow_symlinks=False)
            except FileNotFoundError:
                os.rename(backup_name, output.name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        raise
    if backup_name is not None:
        _remove_directory_at(parent_fd, backup_name)


def _materialize_executable(source: Path, output: Path, record: Mapping[str, object]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {output}")
    descriptor, temporary = _temporary_path(output.parent, f".{output.name}.")
    try:
        with source.open("rb") as input_stream, os.fdopen(descriptor, "wb", closefd=False) as output_stream:
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
        _chmod_open_file(descriptor, temporary, 0o755)
        _commit_temporary(descriptor, temporary, output)
        descriptor = None
    except Exception:
        _remove_temporary(descriptor, temporary)
        raise


def _materialize_archive(source: Path, output: Path, record: Mapping[str, object]) -> None:
    _require_archive_dirfd_support()
    output_parent_fd = _open_directory_path(output.parent, create=True)
    staged_name, staged_fd = _make_directory_at(output_parent_fd, f".{output.name}.new-")
    staged = output.parent / staged_name
    published = False
    try:
        # Re-read and hash the source into a private seekable file before
        # opening it as a ZIP.  The initial cache/explicit-source check and
        # this copy must cover the same bytes; extraction must not reopen a
        # mutable caller-controlled path after verification.
        with tempfile.TemporaryFile(mode="w+b", dir=output.parent) as verified_source:
            with source.open("rb") as input_stream:
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
                    target = staged / normalized
                    if is_directory:
                        target, directory_fd = _ensure_archive_directories(
                            staged,
                            normalized.rstrip("/"),
                            root_fd=staged_fd,
                        )
                        try:
                            os.fchmod(directory_fd, mode_by_name[normalized] & 0o777)
                        finally:
                            _close_descriptor(directory_fd)
                        continue
                    with archive.open(info, "r") as input_stream, _open_archive_member(
                        staged,
                        normalized,
                        root_fd=staged_fd,
                    ) as output_stream:
                        _copy_stream(
                            input_stream,
                            output_stream,
                            expected_size=info.file_size,
                        )
                        output_stream.flush()
                        os.fsync(output_stream.fileno())
                        if normalized in executable_members:
                            _chmod_open_file(output_stream.fileno(), target, 0o755)
                        else:
                            _chmod_open_file(
                                output_stream.fileno(), target, mode_by_name[normalized] & 0o666
                            )
        _validate_staged_tree(staged, root_fd=staged_fd)
        _atomic_replace_directory(
            staged,
            output,
            parent_fd=output_parent_fd,
            staged_fd=staged_fd,
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


def _temporary_path_at(parent_fd: int, prefix: str) -> Tuple[int, str]:
    """Create a descriptor-backed temporary file below a stable parent fd."""
    _require_archive_dirfd_support()
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
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
        os.rename(
            temporary,
            destination,
            src_dir_fd=parent_fd,
            dst_dir_fd=parent_fd,
        )
        try:
            _assert_temporary_at(descriptor, parent_fd, destination)
        except IntegrityError:
            try:
                destination_stat = os.stat(destination, dir_fd=parent_fd, follow_symlinks=False)
                if stat.S_ISLNK(destination_stat.st_mode):
                    os.unlink(destination, dir_fd=parent_fd)
            except OSError:
                pass
            raise
        os.fsync(parent_fd)
    finally:
        _close_descriptor(descriptor)


def _verify_file_at(parent_fd: int, name: str, record: Mapping[str, object]) -> None:
    flags = os.O_RDONLY | os.O_NOFOLLOW
    try:
        descriptor = os.open(name, flags, dir_fd=parent_fd)
    except FileNotFoundError:
        raise
    except OSError as exc:
        raise IntegrityError(f"verified header is not a regular file: {name}") from exc
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise IntegrityError(f"verified header is not a regular file: {name}")
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
    with path.open("rb") as stream:
        data = stream.read(expected_size + 1)
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
    manifest = load_manifest()
    offline_mode = _offline_requested(offline)
    explicit_paths = explicit_paths or {}
    for header in manifest["headers"]:
        local_path = Path(root) / str(header["path"])
        policy = header["policy"]
        if policy == "tracked-preserve":
            if local_path.is_symlink() or not local_path.is_file():
                raise IntegrityError(f"tracked PC header is missing: {local_path}")
            continue

        _require_archive_dirfd_support()
        relative_path = str(header["path"])
        relative = Path(relative_path)
        record = {
            "size": int(header["final_size"]),
            "sha256": header["final_sha256"],
        }
        generated_root_fd = _open_archive_directory_fd(Path(root), (), create=False)
        try:
            try:
                existing_parent_fd = _open_archive_directory_fd(
                    Path(root),
                    relative.parts[:-1],
                    root_fd=generated_root_fd,
                    create=False,
                )
            except FileNotFoundError:
                existing_parent_fd = None
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
