# WanganArcadeLoader

A Windows x64 loader and runtime interface adaptation proof of concept (PoC) for studying interface compatibility, input mapping, and rendering controls. The primary build artifact is `bngrw.dll`.

This project continues development from [Taatsuuyaa/WanganArcadeLoader](https://github.com/Taatsuuyaa/WanganArcadeLoader), preserving the upstream attribution, copyright notices, and license.

## Scope

This repository provides the loader implementation, build scripts, and configuration files. External applications, proprietary content, firmware, authorization credentials, and online services required at runtime are not supplied by this project.

- Runtime interface adaptation and configuration loading.
- Keyboard and controller input mapping.
- Window, resolution, frame rate, and sampling controls.
- Local diagnostic logging and build utilities.

The implementation depends on specific target versions and interface layouts. Validate compatibility separately. This is an experimental prototype, with no guarantee of compatibility with arbitrary applications or versions.

## Build

Requirements: Windows x64, Python 3.11 or newer, Git, MinGW-w64 GCC, NASM, and Ninja.

Run from the repository root:

```powershell
.\build.bat
```

To select a compiler toolchain and parallel job count:

```powershell
.\build.bat -ToolchainRoot $env:MINGW_ROOT -Jobs 4
```

The script installs a project-local copy of Meson when needed and obtains dependencies using the definitions in this repository. Output is written to `out/`, including `bngrw.dll` and the accompanying configuration files.

Build output, download caches, and machine-specific toolchain settings are excluded from version control. Confirm target compatibility and retain a recoverable backup before deploying or replacing existing files.

## Configuration

- `dist/config.toml`: runtime, window, and rendering options.
- `dist/keyconfig.toml`: example keyboard and controller mappings.
- [RULES.md](RULES.md): English-only content and contribution policy.
- [AGENTS.md](AGENTS.md): contribution workflow, Git identity, and privacy checks.

Startup options are top-level keys in `config.toml`, before any section header:

```toml
offline = true
ignoreOfflineTimeLimit = true
fastBoot = true
```

`ignoreOfflineTimeLimit` disables the offline uptime restriction when `offline`
is enabled. `fastBoot` omits the initial diagnostic view and selects the native
short presentation path. Required initialization and resource loading continue
normally. Set either option to `false` to retain its native behavior.

## Runtime diagnostics

Launch with `Start-6RR.cmd` to append runtime diagnostics to `WAL.log` beside the
executable. The launcher captures standard output, standard error, and the final
process exit code, including abrupt termination. The loader also records native
runtime messages, Windows debug output, and exception details.

The log supports concurrent readers and preserves earlier sessions. Common
credential fields, local paths, email addresses, and long numeric identifiers
are redacted before new entries are written. Keep logs local and review them
before sharing. Process-exit monitoring requires the included launcher to stay
running; messages never emitted by the process cannot be recovered from a log.

Keep personal configuration, imported profiles, diagnostic logs, and backups local. Remove personal paths, account information, device identifiers, and other sensitive data before submitting reports or changes.

## Usage and distribution terms

1. **License:** This project is provided under the GNU General Public License v3.0 (GPLv3). The complete terms in [LICENSE](LICENSE) govern its use. This summary does not replace the license or reduce the rights it grants.
2. **Modification and redistribution:** Follow the GPLv3 requirements for copyright and license notices, modification notices, Corresponding Source, and applicable licensing conditions. Binary distributions must also satisfy the license's requirements for providing Corresponding Source or an applicable means of obtaining it.
3. **Third-party rights:** This project's open-source license does not replace separate authorization required for external proprietary applications, content, trademarks, or services. Obtain the necessary rights for the applications, materials, and environments you use. Dependencies and optional components remain subject to their respective licenses.
4. **Attribution and affiliation:** This is an independently maintained derivative project. Names and source references identify the project and its origins; they do not imply third-party endorsement, sponsorship, or support.
5. **Warranty and liability:** To the extent permitted by applicable law, the software is provided "as is," without express or implied warranties, including merchantability or fitness for a particular purpose. Warranty disclaimers and limitations of liability are governed by GPLv3 Sections 15 and 16 and applicable law.
6. **Privacy and contributions:** Do not commit personal information, credentials, private keys, authorization materials, private data, or logs and screenshots containing them. Follow [RULES.md](RULES.md) and [AGENTS.md](AGENTS.md), review the content, run the privacy checks, and use a verified GitHub identity with its account-specific noreply address. All contributions and project communications must be in English.
