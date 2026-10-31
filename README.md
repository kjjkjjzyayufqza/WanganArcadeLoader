# WanganArcadeLoader 6RR - local Windows build

Source: https://github.com/Taatsuuyaa/WanganArcadeLoader (master).
This is the Windows x64 6RR loader that builds `bngrw.dll`, independent of TeknoParrot.

## Rebuild

Requirements: Python 3.11+, Git, MinGW-w64 GCC (the system drive's
`msys64/mingw64` directory by default), and NASM.
Run `.\build.bat` (or `powershell -ExecutionPolicy Bypass -File .\build-windows.ps1`).
The batch entry point accepts the same parameters, for example `.\build.bat -Jobs 4`,
and returns the build exit code without pausing, so agents can use it directly.
The script installs project-local Meson 1.12.1 when needed and writes the deployable files to `out/`.

Pinned dependencies:

- SDL2: 2.32.8, Meson WrapDB 2.32.8-1 (source and overlay SHA-256 verified by Meson).
- MinHook: v1.3.4.
- tomlc99: 29076dfd095bbbbd50a3c1b2760d29f4b83e74ac (2026-01-30).

SDL remains on its 2.x API because SDL3 requires a separate input-system migration.
The build links the compiler runtimes and the above libraries statically into the DLL.

## Game setup

Target executable: `wmn6r.exe` (the `wmn6rR.exe` alongside it is an older, different build).
Only deploy after checking the target executable matches the fixed addresses in this hook.
Back up each replaced file, especially `bngrw.dll`, before copying files from `out/`.
Keep the existing `card.ini` and saves.

To import an existing TeknoParrot keyboard profile, run
`python import-wmmt6-keys.py --profile <PROFILE_XML> --game-exe <GAME_EXE>`.
Generated bindings go to ignored `out/keyconfig.toml`; the source profile and its
path are never copied into the repository or generated configuration.

Keyboard controls: W/S = accelerator/brake; A/D = steering; Z/X = next/previous gear;
F1-F6 = gears 1-6; left Shift = view; left Ctrl = intrude; left Alt = coin;
C = enter switch; Enter = additional Start binding; 1/2 = test/service; P = card tap.
The original test-menu up/down mappings contain unnamed legacy key IDs (105/110), so
the loader uses the up/down arrow keys for those two test-menu functions.

Gamepad mappings remain available. This version reads the `START` binding, accepts left/right
modifier keys. Logging writes directly to disk without a debug console.
Movies are skipped by default.

Standalone play defaults to the top-level `offline = true` option. It selects the game's
disabled AMAuth-service path and completes the corresponding offline startup check.
This prevents `E0001 UnknownError` when the local AMAuth COM service is unavailable
(`80040154`), without discarding other game errors. Missing neighbor data during the
offline attract demo is checked before updating it. Online authentication and server
card saves require `offline = false` and a separately configured working service.

## Logs

`WAL.log` in the executable's directory contains game debug messages, loader warnings,
hook installation results and graphics diagnostics, with local timestamps and process/thread IDs.
Every launch appends a session header; earlier sessions remain in the same file.
UTF-8 entries are written directly with `WriteFile`, without a buffered console or per-entry
disk-flush stalls. The file handle uses `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`,
so compatible viewers can follow the file while the game runs, including across restarts.
ReShade continues to write its own `ReShade.log` beside it.

Follow the combined log from another PowerShell window:

```powershell
Get-Content (Join-Path $env:WMMT6RR_GAME_DIR 'WAL.log') -Encoding UTF8 -Tail 100 -Wait
```

Deploy with `python deploy-loader.py --game <GAME_DIR>`. Every overwritten file is
backed up under the selected game's `loader-backup/<timestamp>` directory, with
SHA-256 checks and a restore command. Keep backups and generated manifests local.

## Graphics

The local preset renders at 2560x1440. `config.toml` controls the native D3D11 hooks:

```toml
res = { x = 2560, y = 1440 } # Keep this top-level, before [graphics].

[graphics]
enabled = true
vsync = true
fps_limit = 60
anisotropy = 16
borderless = true
diagnostics = true
```

Restart the game after changing these settings. `fps_limit = 0` disables the software cap;
`anisotropy = 0` preserves game samplers. VSync and the frame cap are coordinated so a
60 Hz display is paced by VSync instead of competing with a second 60 FPS timer.
Point-filtered UI and comparison/shadow samplers keep their original filtering.
`borderless` applies when the top-level `windowed = true`.
The combined `WAL.log` records installed hooks, back-buffer size, frame rate and sampler overrides.

Post-processing uses official ReShade 6.8.0, downloaded/extracted by `python prepare-postfx.py`.
It stages the 64-bit `dxgi.dll`, pinned upstream SMAA/CAS/FXAA shaders, textures and source receipts
under ignored `out/`. The installer is not executed and the OS certificate store is not modified.
SMAA + light FidelityFX CAS sharpening are enabled by `WMMT6RR-ReShade.ini`; FXAA is an
available alternative. This is post-process antialiasing, not a claim of native MSAA support.

- Home: open ReShade settings. Disable Performance Mode there to edit effect parameters live.
- Scroll Lock: toggle all post-processing, leaving resolution/VSync/limiting/filtering active.
- Edit `WMMT6RR-ReShade.ini` for persistent effect strengths.

Build/package: `.\build.bat`, then
`python prepare-postfx.py`, then `python deploy-loader.py --game <GAME_DIR>`.
Deployment backs up nested shader/config files and preserves existing keybindings, calibration
and controller database unless `--replace-input-config` is explicitly requested.

Upstream references: [ReShade](https://reshade.me/),
[SweetFX shaders](https://github.com/CeeJayDK/SweetFX),
[Present synchronization](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present),
[sampler filtering](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_sampler_desc).

## Repository privacy

Follow [AGENTS.md](AGENTS.md). This fork keeps `origin` as the approved push
destination and `upstream` fetch-only. Personal records, screenshots, credentials,
runtime logs, profiles, saves, and build/deployment output must stay local.

For a fresh clone, configure repository-local hooks and identity:

```powershell
git config --local core.hooksPath .githooks
$githubAccount = gh api user | ConvertFrom-Json
git config --local user.name $githubAccount.login
git config --local user.email ('{0}+{1}@users.noreply.github.com' -f $githubAccount.id, $githubAccount.login)
git config --local privacy.commitName $githubAccount.login
git config --local privacy.commitEmail (git config --local --get user.email)
git config --local privacy.allowedPushUrl (git remote get-url --push origin)
git config --local privacy.trustedBase <INSPECTED_UPSTREAM_COMMIT>
```

Confirm that `gh` is authenticated as the intended fork owner before configuring
identity. GitHub CLI is required for the identity setup above. The account's
GitHub noreply address preserves attribution without publishing a private email;
an invented generic noreply address may belong to somebody else's account.
Use the inspected upstream commit before fork-specific changes as the baseline.
Account-specific values belong only in `.git/config`. Hooks fail closed until
configured. They inspect staged content, commit messages, author metadata,
every outgoing commit, and the push destination. They do not replace manual
review. Do not disable hooks or use `--no-verify`.

Run `python scripts/check-repo-privacy.py --self-test` to exercise synthetic leak
fixtures and check the current staged tree. No sensitive values are printed.
