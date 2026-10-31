"""Back up and deploy this build to the explicitly selected local 6RR game."""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent
SUPPORTED_EXE_SHA256 = "8ed9e25a5ab7185247bd905c7038d752f536fc51a0036e01a5e3c4512536cab1"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def contained(root, name):
    destination = (root / name).resolve()
    if not destination.is_relative_to(root.resolve()):
        raise RuntimeError(f"Unexpected path outside deployment directory: {name}")
    return destination


def restore(backup):
    manifest = json.loads((backup / "manifest.json").read_text(encoding="utf-8"))
    target = Path(manifest["target"])
    for item in manifest["files"]:
        if item["existed"]:
            saved = contained(backup, item["name"])
            if digest(saved) != item["original_sha256"]:
                raise RuntimeError(f"Backup checksum mismatch: {saved}")
    for item in manifest["files"]:
        destination = contained(target, item["name"])
        if item["existed"]:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(contained(backup, item["name"]), destination)
        elif destination.is_file() and digest(destination) == item["deployed_sha256"]:
            destination.unlink()
    print(f"Restored original loader files from {backup}")


def deploy(target, replace_input=False):
    target = target.resolve(strict=True)
    if digest(target / "wmn6r.exe") != SUPPORTED_EXE_SHA256:
        raise RuntimeError("The game executable differs from the inspected local 6RR build; inspect compatibility before deploying.")
    output = ROOT / "out"
    if not (output / "bngrw.dll").is_file():
        raise RuntimeError("Build out/bngrw.dll first.")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = target / "loader-backup" / stamp
    backup.mkdir(parents=True, exist_ok=False)
    manifest = {"target": str(target), "created": stamp, "files": [], "complete": False}
    for source in sorted(output.rglob('*')):
        if not source.is_file():
            continue
        relative = source.relative_to(output).as_posix()
        destination = contained(target, relative)
        if not replace_input and relative in {'keyconfig.toml', 'setting.lua.gz', 'gamecontrollerdb.txt'} and destination.exists():
            continue
        item = {"name": relative, "existed": destination.exists(), "deployed_sha256": digest(source)}
        if item["existed"]:
            item["original_sha256"] = digest(destination)
            saved = contained(backup, relative)
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(destination, saved)
            if digest(saved) != item["original_sha256"]:
                raise RuntimeError(f"Backup checksum mismatch: {relative}")
        manifest["files"].append(item)
    manifest_path = backup / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    restore_command = f'@echo off\r\npython "{ROOT / "deploy-loader.py"}" --restore "{backup}"\r\npause\r\n'
    (backup / "Restore-Original.cmd").write_text(restore_command, encoding="utf-8")
    try:
        for item in manifest["files"]:
            destination = contained(target, item['name'])
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(contained(output, item["name"]), destination)
            if digest(destination) != item["deployed_sha256"]:
                raise RuntimeError(f"Deployment checksum mismatch: {item['name']}")
    except Exception:
        restore(backup)
        raise
    manifest["complete"] = True
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"backup": str(backup), "target": str(target), "files": [x["name"] for x in manifest["files"]]}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--game", type=Path)
    action.add_argument("--restore", type=Path)
    parser.add_argument("--replace-input-config", action="store_true")
    options = parser.parse_args()
    if options.restore:
        restore(options.restore.resolve(strict=True))
    else:
        deploy(options.game, options.replace_input_config)
