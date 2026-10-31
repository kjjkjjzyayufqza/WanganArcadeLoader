# Repository rules

## Fork workflow

- Work in this fork. `origin` is the approved fork; `upstream` is fetch-only.
- Never push to upstream. Use `codex/` for new working branches.
- Keep the upstream license and existing attribution intact.
- Do not reset, clean, overwrite, force-push, delete remote refs, or rewrite
  published history without explicit authorization for that operation.
- Before committing, review the exact staged diff. Before pushing, inspect
  every outgoing commit, including intermediate commits.

## Privacy is mandatory

- Never write personal information or secrets into source, comments,
  documentation, tests, examples, logs, screenshots, commit messages, or artifacts.
- This includes real names, personal email, phone/address, account credentials,
  API tokens, cookies, private keys, local usernames/paths, private network
  addresses, card/access/chip identifiers, device serials, and save/profile data.
- Use relative paths, environment variables, explicit command-line arguments,
  and clearly synthetic examples. Never copy a user's real values into examples.
- Do not transcribe private conversations or attach local screenshots. Existing
  private records stay local and ignored; do not delete them to prepare a commit.
- Do not print detected sensitive values or filenames into tool output or reports.
  Report only the category and count; inspect and redact locally when required.
- Generated manifests, diagnostic logs, deployment backups, imported profiles,
  credentials, and personal configuration are local-only and must stay ignored.
- Never commit dangerous operations involving personal information: credential
  changes, personal-data exports/uploads, private-data deletion, or scripts that
  embed or transmit private values. Stop that operation and leave private state
  local. Sanitize the change before continuing.
- Use the verified GitHub login and its account-ID-plus-login GitHub noreply
  address for author and committer. Never invent a generic identity: GitHub
  may associate it with somebody else's account. Never use a private email.
- Store the approved identity only in repository-local `privacy.commitName`
  and `privacy.commitEmail`, alongside matching `user.name` and `user.email`.
  Preserve inherited upstream history.
- Never bypass privacy hooks with `--no-verify`, disable them, or add exemptions
  to get sensitive material through. Hook changes require the same privacy review.
- Pattern checks cannot recognize every kind of personal information. Human/agent
  review remains required; a passing hook is not permission to publish private data.

## Local setup and verification

- Python 3.11+ and Git are required by the privacy hooks.
- Set repository-local `core.hooksPath` to `.githooks`.
- Set repository-local `privacy.allowedPushUrl` to the approved fork's push URL.
- Set repository-local `privacy.trustedBase` to the inspected upstream commit
  already present before fork-specific changes. Do not advance it to hide findings.
- After explicit user authorization for a history correction, pin local
  `privacy.authorizedRewrite` to the exact remote ref, old SHA and new SHA
  separated by spaces. Use `--force-with-lease` and remove this one-operation
  authorization after the attempt. Full commit/privacy checks remain mandatory.
- Keep account-specific remotes and configuration in `.git/config`, never in files.
- Run `python scripts/check-repo-privacy.py --self-test` after changing the guard.
- Run one narrow semantic verifier for the actual change. Use `build.bat` when
  validating loader/build changes; repository-policy changes need the privacy gate.
