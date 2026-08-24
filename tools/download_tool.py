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
import shutil
import stat
import tempfile
import urllib.error
import urllib.request
import zipfile
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

    if manifest.get("schema_version") != 1:
        raise ManifestError("unsupported download manifest schema")
    generated = manifest.get("generated_from", {})
    for key in ("pc_commit", "decomp_commit", "ultralib_commit"):
        if not isinstance(generated.get(key), str) or not generated[key]:
            raise ManifestError(f"manifest generated_from.{key} is missing")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 28:
        raise ManifestError("manifest must contain the current 28 public artifacts")
    seen_assets = set()
    for artifact in artifacts:
        required = ("tool", "asset_id", "size", "sha256", "kind", "release_tag", "asset_name", "asset_url")
        if any(key not in artifact for key in required):
            raise ManifestError("artifact entry is missing a required field")
        asset_key = (artifact["tool"], artifact["asset_name"])
        if asset_key in seen_assets:
            raise ManifestError(f"duplicate manifest artifact {asset_key}")
        seen_assets.add(asset_key)
        if not isinstance(artifact["size"], int) or artifact["size"] <= 0:
            raise ManifestError(f"invalid size for {asset_key}")
        if not re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]):
            raise ManifestError(f"invalid SHA-256 for {asset_key}")
        if artifact["kind"] not in ("archive", "executable"):
            raise ManifestError(f"unsupported artifact kind for {asset_key}")
        if artifact["kind"] == "archive":
            archive = artifact.get("archive")
            if not isinstance(archive, dict):
                raise ManifestError(f"archive policy missing for {asset_key}")
            members = archive.get("members")
            if not isinstance(members, list) or len(members) != archive.get("member_count"):
                raise ManifestError(f"archive allowlist incomplete for {asset_key}")
            names = [member.get("name") for member in members]
            if any(not isinstance(name, str) for name in names) or len(names) != len(set(names)):
                raise ManifestError(f"archive member names are not unique for {asset_key}")
            if not isinstance(archive.get("allowed_modes"), list):
                raise ManifestError(f"archive mode policy missing for {asset_key}")

    headers = manifest.get("headers")
    if not isinstance(headers, list) or len(headers) != 6:
        raise ManifestError("manifest must contain the six header policies")
    header_paths = set()
    for header in headers:
        for key in ("path", "source_url", "source_size", "source_sha256", "final_size", "final_sha256", "policy"):
            if key not in header:
                raise ManifestError(f"header policy missing {key}")
        if header["path"] in header_paths:
            raise ManifestError(f"duplicate header policy {header['path']}")
        header_paths.add(header["path"])
        if header["policy"] not in ("tracked-preserve", "generated-verified"):
            raise ManifestError(f"unsupported header policy {header['policy']}")
        if not re.fullmatch(r"[0-9a-f]{64}", header["source_sha256"]):
            raise ManifestError(f"invalid header source SHA-256 for {header['path']}")
        if not re.fullmatch(r"[0-9a-f]{64}", header["final_sha256"]):
            raise ManifestError(f"invalid header final SHA-256 for {header['path']}")
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


def _temporary_path(parent: Path, prefix: str) -> Path:
    descriptor, name = tempfile.mkstemp(prefix=prefix, dir=parent)
    os.close(descriptor)
    return Path(name)


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
    temporary = _temporary_path(cache_path.parent, ".download-")
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
            with temporary.open("wb") as output:
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
        os.replace(temporary, cache_path)
    except (urllib.error.URLError, OSError) as exc:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise DownloadError(f"network fetch failed for {record['asset_name']}") from exc
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
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


def _atomic_replace_file(staged: Path, output: Path) -> None:
    if output.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {output}")
    os.replace(staged, output)


def _atomic_replace_directory(staged: Path, output: Path) -> None:
    if output.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {output}")
    backup: Optional[Path] = None
    if output.exists():
        if not output.is_dir():
            raise IntegrityError(f"archive output is not a directory: {output}")
        backup = Path(tempfile.mkdtemp(prefix=f".{output.name}.old-", dir=output.parent))
        backup.rmdir()
        os.replace(output, backup)
    try:
        os.replace(staged, output)
    except Exception:
        if backup is not None and not output.exists():
            os.replace(backup, output)
        raise
    if backup is not None:
        shutil.rmtree(backup)


def _materialize_executable(source: Path, output: Path, record: Mapping[str, object]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {output}")
    temporary = _temporary_path(output.parent, f".{output.name}.")
    try:
        with source.open("rb") as input_stream, temporary.open("wb") as output_stream:
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
        os.chmod(temporary, 0o755)
        _atomic_replace_file(temporary, output)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise


def _materialize_archive(source: Path, output: Path, record: Mapping[str, object]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {output}")
    staged = Path(tempfile.mkdtemp(prefix=f".{output.name}.new-", dir=output.parent))
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
                        target.mkdir(parents=True, exist_ok=True)
                        os.chmod(target, mode_by_name[normalized] & 0o777)
                        continue
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info, "r") as input_stream, target.open("wb") as output_stream:
                        _copy_stream(
                            input_stream,
                            output_stream,
                            expected_size=info.file_size,
                        )
                        output_stream.flush()
                        os.fsync(output_stream.fileno())
                    if normalized in executable_members:
                        os.chmod(target, 0o755)
                    else:
                        os.chmod(target, mode_by_name[normalized] & 0o666)
        _atomic_replace_directory(staged, output)
        staged = None  # type: ignore[assignment]
    finally:
        if staged is not None and staged.exists():
            shutil.rmtree(staged)


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


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise IntegrityError(f"refusing to replace symlink output {path}")
    temporary = _temporary_path(path.parent, f".{path.name}.")
    try:
        with temporary.open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise


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

        if local_path.exists():
            if local_path.is_symlink() or not local_path.is_file():
                raise IntegrityError(f"generated header is not a regular file: {local_path}")
            record = {
                "size": int(header["final_size"]),
                "sha256": header["final_sha256"],
            }
            _verify_file(local_path, record)
            continue

        source_path = explicit_paths.get(str(header["path"]))
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
        _atomic_write_bytes(local_path, final_data)


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
