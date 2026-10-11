#!/usr/bin/env python3
"""Package an unsigned device app for signing by Sideloadly or another signer."""

import argparse
import hashlib
from pathlib import Path
import plistlib
import stat
import struct
import zipfile


def package(app: Path, output: Path) -> None:
    if app.suffix != ".app" or not app.is_dir():
        raise ValueError("Input must be a built .app directory")
    with (app / "Info.plist").open("rb") as source:
        info = plistlib.load(source)
    if info.get("CFBundleSupportedPlatforms") != ["iPhoneOS"]:
        raise ValueError("Only an iPhoneOS device app can be packaged, not a simulator app")
    for key in ("CFBundleIdentifier", "CFBundleVersion", "CFBundleShortVersionString"):
        if not isinstance(info.get(key), str) or not info[key].strip():
            raise ValueError(f"Missing app metadata: {key}")
    executable = info.get("CFBundleExecutable", "")
    if not executable or Path(executable).name != executable:
        raise ValueError("Invalid CFBundleExecutable")
    binary = app / executable
    with binary.open("rb") as source:
        header = source.read(32)
        if len(header) != 32:
            raise ValueError("Missing Mach-O executable header")
        magic, cpu, _, filetype, count, size, _, _ = struct.unpack("<8I", header)
        if magic != 0xFEEDFACF or cpu != 0x0100000C or filetype != 2:
            raise ValueError("Expected an arm64 Mach-O device executable")
        commands = source.read(size)
    offset = 0
    device_platform = False
    for _ in range(count):
        if offset + 8 > len(commands):
            raise ValueError("Truncated Mach-O load commands")
        command, length = struct.unpack_from("<2I", commands, offset)
        if length < 8 or offset + length > len(commands):
            raise ValueError("Invalid Mach-O load command")
        if command == 0x32 and length >= 24:  # LC_BUILD_VERSION
            device_platform = struct.unpack_from("<I", commands, offset + 8)[0] == 2
        offset += length
    if not device_platform:
        raise ValueError("Executable must target the iOS device platform")
    if not binary.stat().st_mode & stat.S_IXUSR:
        raise ValueError("App executable has lost its executable permission")
    if output.suffix != ".ipa":
        raise ValueError("Output must use the .ipa extension")
    if output.resolve().is_relative_to(app.resolve()):
        raise ValueError("Output must be outside the input app")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in [app, *sorted(app.rglob("*"))]:
            name = (Path("Payload") / app.name / path.relative_to(app)).as_posix()
            if path.is_symlink():
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                entry.external_attr = path.lstat().st_mode << 16
                archive.writestr(entry, path.readlink().as_posix().encode())
            else:
                archive.write(path, name)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".ipa.sha256").write_text(f"{digest}  {output.name}\n")
    print(f"Packaged unsigned {output} ({output.stat().st_size} bytes)")
    print("This IPA must be signed before installation on an iPad.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        package(args.app, args.output)
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        parser.exit(1, f"{error}\n")
