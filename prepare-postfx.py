"""Fetch pinned official ReShade and shader sources for this user's local build.

Downloaded binaries/shaders stay in ignored out/ and .build-tools/ directories.
The installer is inspected and extracted, never executed.
"""

import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
DOWNLOADS = ROOT / '.build-tools/downloads'
OUTPUT = ROOT / 'out'
VERSION = '6.8.0'
INSTALLER_SHA256 = '207aea16205fbf952bc8fe1879966672454cf04002e7ad34237c7990a5b3c0b4'
DLL_SHA256 = 'b2945c29e7095491a901746b400e58db9b1592ab092bacf2a888ce37f02d08da'
SIGNER = '589690208A5E52FB96980C4A6698F50ACD47C49F'
BASE_COMMIT = 'fd0022170615ce0d8162d219bff07232fa6dd84f'
SWEETFX_COMMIT = '93ddf39b357f5da534ed6d34ba4ec8cc7dcfa361'


def fetch(url):
    return urllib.request.urlopen(
        urllib.request.Request(url, headers={'User-Agent': 'WanganArcadeLoader-local-build/1.0'}),
        timeout=45).read()


def main():
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(exist_ok=True)
    installer = DOWNLOADS / f'ReShade_Setup_{VERSION}.exe'
    source = f'https://reshade.me/downloads/ReShade_Setup_{VERSION}.exe'
    if not installer.is_file():
        installer.write_bytes(fetch(source))
    if hashlib.sha256(installer.read_bytes()).hexdigest() != INSTALLER_SHA256:
        raise RuntimeError('ReShade installer does not match the pinned official download.')
    command = (
        "$s=Get-AuthenticodeSignature -LiteralPath '" + str(installer).replace("'", "''") + "';"
        "[pscustomobject]@{Status=$s.Status.ToString();Thumbprint=$s.SignerCertificate.Thumbprint}|ConvertTo-Json -Compress"
    )
    shell = shutil.which('pwsh') or shutil.which('powershell')
    checked = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', command],
                             capture_output=True, text=True)
    if checked.returncode:
        raise RuntimeError('Cannot inspect ReShade publisher: ' + checked.stderr.strip())
    signature = json.loads(checked.stdout)
    if signature.get('Thumbprint') != SIGNER or signature.get('Status') in ['NotSigned', 'HashMismatch']:
        raise RuntimeError('ReShade publisher verification failed.')
    # ReShade publishes this certificate thumbprint itself. Its certificate can be
    # absent from the OS trust store; no certificate/trust-store change is made.
    # The exact installer AND DLL hashes are pinned independently of trust status.
    with zipfile.ZipFile(installer) as archive:
        dll = archive.read('ReShade64.dll')
    if hashlib.sha256(dll).hexdigest() != DLL_SHA256:
        raise RuntimeError('ReShade64.dll checksum mismatch.')
    (OUTPUT / 'dxgi.dll').write_bytes(dll)

    downloads = {}
    for name in ['ReShade.fxh', 'ReShadeUI.fxh']:
        downloads[f'reshade-shaders/Shaders/{name}'] = (
            f'https://raw.githubusercontent.com/crosire/reshade-shaders/{BASE_COMMIT}/Shaders/{name}')
    for name in ['SMAA.fx', 'SMAA.fxh', 'CAS.fx', 'FXAA.fx', 'FXAA.fxh']:
        downloads[f'reshade-shaders/Shaders/SweetFX/{name}'] = (
            f'https://raw.githubusercontent.com/CeeJayDK/SweetFX/{SWEETFX_COMMIT}/Shaders/SweetFX/{name}')
    for name in ['AreaTex.png', 'SearchTex.png']:
        downloads[f'reshade-shaders/Textures/SweetFX/{name}'] = (
            f'https://raw.githubusercontent.com/CeeJayDK/SweetFX/{SWEETFX_COMMIT}/Textures/SweetFX/{name}')
    downloads['reshade-shaders/SweetFX-LICENSE.txt'] = (
        f'https://raw.githubusercontent.com/CeeJayDK/SweetFX/{SWEETFX_COMMIT}/LICENSE')
    downloads['reshade-shaders/ReShade-shaders-README.md'] = (
        f'https://raw.githubusercontent.com/crosire/reshade-shaders/{BASE_COMMIT}/README.md')

    def download(item):
        relative, url = item
        payload = fetch(url)
        destination = OUTPUT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)
        return {'path': relative, 'url': url, 'sha256': hashlib.sha256(payload).hexdigest()}

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        files = list(pool.map(download, downloads.items()))
    manifest = {'reshade_version': VERSION, 'installer_url': source,
                'installer_sha256': INSTALLER_SHA256, 'dll_sha256': DLL_SHA256,
                'publisher_thumbprint': SIGNER, 'signature_status': signature['Status'], 'files': files}
    (OUTPUT / 'postfx-sources.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({'version': VERSION, 'dll': str(OUTPUT / 'dxgi.dll'),
                      'shader_files': len(files), 'default_effects': ['SMAA', 'CAS']}, indent=2))


if __name__ == '__main__':
    main()
