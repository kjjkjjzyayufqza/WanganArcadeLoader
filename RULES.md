# Repository Rules

## English-only policy

- Use English for all repository-owned documentation, source comments,
  docstrings, configuration descriptions, diagnostic messages, scripts, tests,
  filenames, directory names, commit messages, and contribution text.
- Write all assistant responses, progress updates, review notes, and generated
  artifacts for this project in English, even when a request uses another language.
- Do not introduce Chinese characters, Chinese punctuation, bilingual sections,
  or untranslated text. Use standard ASCII punctuation in prose and examples.
- Represent required non-English external identifiers with Unicode escapes,
  preserving their exact values for compatibility.
- Translate existing repository-owned text into English when editing it.
  Before publishing, check the complete current tracked text for Chinese
  characters, including filenames, comments, examples, and quoted material.
- Apply this policy to current files and new contributions. Preserve existing
  published history and required upstream license and attribution notices.

## Privacy and publication

- Follow the privacy, identity, fork, and verification requirements in
  [AGENTS.md](AGENTS.md).
- Keep personal information, credentials, local records, and generated private
  data out of the repository and commit messages.
- Review the staged diff and outgoing commits before publishing. Run the
  repository privacy gate and the narrow verifier appropriate to the change.
- Push only to the approved fork. Obtain explicit authorization before any
  operation that rewrites published history.

## Project scope and license

- Describe this project accurately as a Windows x64 loader and runtime interface
  adaptation prototype. Do not claim universal compatibility or third-party
  endorsement.
- Keep the README focused on the loader implementation, build process,
  configuration, and usage/distribution terms.
- Preserve the upstream GPLv3 license. Project documentation must not replace
  the license or reduce the rights it grants.
