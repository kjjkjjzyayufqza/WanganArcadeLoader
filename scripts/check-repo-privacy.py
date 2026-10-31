"""Fail closed on private staged data and outgoing commits; never echo findings."""

import fnmatch
from contextlib import contextmanager
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

PRIVATE_NAMES = (
    "conversation*", ".env", ".env.*", "*.local.*", "*.log", "*.log.*",
    "*.dmp", "*.dump", "*.bak", "*.backup", "*.pem", "*.key", "*.p12",
    "*.pfx", "*.7z", "*.zip", "*.tar", "*.gz", "*.exe", "*.dll",
    "*.png", "*.jpg", "*.jpeg", "*.webp", "*.mp4", "*.db", "*.sqlite*",
    "card.ini", "credentials*", "secrets*", "native-mingw.ini",
)
PRIVATE_DIRS = {
    "work", "build", "out", ".build-tools", "private", ".private", "local",
    ".local", ".codex", "conversation-assets", "saves", "savedata", "loader-backup",
    ".ssh", ".aws", "__pycache__",
}
RULES = {
    "absolute local path": r"(?i)(?:(?<![a-z0-9])[a-z]:[\\/]+|/(?:home|Users)/)[^\s\"'<>]+",
    "email address": r"(?i)\b[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}\b",
    "access token": r"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-(?:proj-)?[A-Za-z0-9_-]{20,}|AKIA[A-Z0-9]{16})",
    "private key": r"-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----",
    "credential assignment": r'''(?im)^\s*["']?(?:password|passwd|api_?key|api_?token|access_?token|client_?secret|authorization|cookie|access_?code|chip_?id|card_?id|pcb_?serial)["']?\s*[:=]\s*["'][^\s"'<>$][^"']{3,}["']''',
    "private network address": r"\b(?:10\.(?:\d{1,3}\.){2}\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})\b",
}


class Blocked(Exception):
    pass


def git(*args, input_data=None):
    result = subprocess.run(["git", *args], input=input_data, capture_output=True)
    if result.returncode:
        raise Blocked("Git inspection failed; no private details were printed")
    return result.stdout


def config(name):
    return git("config", "--local", "--get", name).decode().strip()


def approved_identity():
    name, email = config("privacy.commitName"), config("privacy.commitEmail")
    match = re.fullmatch(r"[1-9][0-9]*\+([A-Za-z0-9-]+)@users\.noreply\.github\.com", email)
    if not match or name != match[1]:
        raise Blocked("configure the verified GitHub login and account-specific noreply identity")
    return name, email


def check_identity(name, email):
    if (name, email) != approved_identity():
        raise Blocked("commit identity differs from the approved GitHub noreply identity")


def check_text(text):
    # Shader technique syntax is not an email address. Only synthetic addresses
    # are accepted in versioned content; account-specific identity stays local.
    text = re.sub(r"\b\w+@\w+\.fxh?\b", "<shader>", text)
    text = re.sub(r"(?i)\b[a-z0-9._%+-]+@example\.(?:com|org|net)\b", "<example>", text)
    for category, pattern in RULES.items():
        if re.search(pattern, text):
            raise Blocked("sensitive content category: " + category)


def entries(ref=None):
    if ref is None:
        raw = git("ls-files", "--stage", "-z")
    else:
        raw = git("ls-tree", "-r", "-z", ref)
    result = {}
    for item in raw.split(b"\0"):
        if not item:
            continue
        metadata, path = item.split(b"\t", 1)
        mode, middle, last = metadata.decode("ascii").split()
        if ref is None and last != "0":
            raise Blocked("resolve merge conflicts before committing")
        result[path] = (mode, last if ref else middle)
    return result


def check_tree(tree, baseline):
    for raw_path, (mode, oid) in tree.items():
        # Preserve exact inherited source/binary/license files without rewriting
        # upstream history. Any change to them goes through the same checks.
        if baseline.get(raw_path) == (mode, oid):
            continue
        path = raw_path.decode("utf-8", errors="strict")
        check_text(path)
        parts = path.lower().split("/")
        if any(part in PRIVATE_DIRS for part in parts[:-1]):
            raise Blocked("private/generated directory in proposed tree")
        if parts[-1] != ".env.example" and any(
            fnmatch.fnmatchcase(parts[-1], pattern) for pattern in PRIVATE_NAMES
        ):
            raise Blocked("private/generated file type in proposed tree")
        if mode not in {"100644", "100755"}:
            raise Blocked("symlinks and submodules require separate privacy review")
        data = git("cat-file", "blob", oid)
        if len(data) > 1024 * 1024 or b"\0" in data:
            raise Blocked("new binary or oversized content requires separate privacy review")
        try:
            check_text(data.decode("utf-8-sig"))
        except UnicodeError:
            raise Blocked("non-UTF-8 content requires separate privacy review") from None


def baseline():
    base = config("privacy.trustedBase")
    if not re.fullmatch(r"[0-9a-f]{40,64}", base):
        raise Blocked("configure the inspected upstream baseline")
    return base, entries(base)


def check_staged():
    for role in ("AUTHOR", "COMMITTER"):
        value = git("var", "GIT_" + role + "_IDENT").decode().strip()
        match = re.fullmatch(r"(.+) <([^>]+)> \d+ [+-]\d{4}", value)
        if not match:
            raise Blocked("cannot inspect proposed commit identity")
        check_identity(*match.groups())
    _, trusted = baseline()
    check_tree(entries(), trusted)


def check_update(remote_ref, remote_sha, local_sha):
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", remote_sha, local_sha],
        capture_output=True,
    )
    if result.returncode == 0:
        return
    if result.returncode != 1:
        raise Blocked("cannot inspect remote ancestry")
    # Explicit user authorization is pinned locally to one ref and both SHAs.
    # Every outgoing commit still undergoes the full identity/privacy checks.
    expected = " ".join((remote_ref, remote_sha, local_sha))
    if config("privacy.authorizedRewrite") != expected:
        raise Blocked("history rewrite does not match the exact authorized update")


def check_push(remote, url, updates):
    allowed = config("privacy.allowedPushUrl")
    if remote != "origin" or url != allowed or git(
        "remote", "get-url", "--push", "origin"
    ).decode().strip() != allowed:
        raise Blocked("push destination is not the approved fork")
    base, trusted = baseline()
    checked = set()
    for update in updates.splitlines():
        fields = update.split()
        if len(fields) != 4:
            raise Blocked("cannot inspect push updates")
        local_ref, local_sha, remote_ref, remote_sha = fields
        if not remote_ref.startswith("refs/heads/") or set(local_sha) == {"0"}:
            raise Blocked("remote deletions and tag pushes require separate authorization")
        git("merge-base", "--is-ancestor", base, local_sha)
        if set(remote_sha) != {"0"}:
            check_update(remote_ref, remote_sha, local_sha)
        for commit in git("rev-list", local_sha, "^" + base).decode().splitlines():
            if commit in checked:
                continue
            metadata = git("show", "-s", "--format=%an%x00%ae%x00%cn%x00%ce%x00%B", commit)
            author, email, committer, committer_email, message = metadata.decode().split("\0", 4)
            check_identity(author, email)
            check_identity(committer, committer_email)
            check_text(message)
            check_tree(entries(commit), trusted)
            checked.add(commit)


@contextmanager
def fixture_repo():
    original = Path.cwd()
    with tempfile.TemporaryDirectory(prefix="repo-privacy-") as folder:
        try:
            os.chdir(folder)
            yield
        finally:
            # Windows cannot remove a directory while it is the process cwd.
            os.chdir(original)


def self_test():
    # All fixtures are synthetic and assembled to avoid recording secret-shaped
    # strings in this repository. Test real Git trees, metadata and push ranges.
    script = Path(__file__).resolve()
    original = Path.cwd()
    old_env = os.environ.copy()
    for name in list(os.environ):
        if name.startswith("GIT_"):
            del os.environ[name]
    try:
        with fixture_repo():
            git("init", "-q")
            identity = ("privacy-fixture", "1234567890+privacy-fixture" + "@users.noreply.github.com")
            git("config", "user.name", identity[0])
            git("config", "user.email", identity[1])
            git("config", "privacy.commitName", identity[0])
            git("config", "privacy.commitEmail", identity[1])
            git("config", "commit.gpgsign", "false")
            git("config", "core.hooksPath", str(script.parent.parent / ".githooks"))
            # Disable hook execution only inside this throwaway fixture repo.
            Path("safe.txt").write_text("public source\n", encoding="utf-8")
            git("add", "safe.txt")
            git("-c", "core.hooksPath=", "commit", "-qm", "Synthetic baseline")
            base = git("rev-parse", "HEAD").decode().strip()
            git("config", "privacy.trustedBase", base)
            allowed = "https://example.com/approved/fork.git"
            git("remote", "add", "origin", allowed)
            git("config", "privacy.allowedPushUrl", allowed)
            cases = 0

            def expect_block(action):
                nonlocal cases
                try:
                    action()
                except Blocked:
                    cases += 1
                else:
                    raise AssertionError("a synthetic private fixture was accepted")

            for name, payload in (
                ("private/note.txt", "local only"),
                ("CONVERSATION-test.md", "local only"),
                ("safe.txt", "token = " + "ghp_" + "A" * 32),
                ("safe.txt", "path = " + "Q:" + "/private/game"),
                ("safe.txt", "contact = " + "test" + "@private.invalid"),
                ("safe.txt", "password = " + chr(34) + "synthetic-secret" + chr(34)),
                ("safe.txt", "host = " + "192." + "168.1.2"),
                ("safe.txt", "\0binary"),
            ):
                path = Path(name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(payload, encoding="utf-8")
                git("add", name)
                expect_block(check_staged)
                git("reset", "-q", base, "--", name)
            Path("safe.txt").write_text("public change\n", encoding="utf-8")
            git("add", "safe.txt")
            check_staged()
            cases += 1
            git("-c", "core.hooksPath=", "commit", "-qm", "Safe public change")
            head = git("rev-parse", "HEAD").decode().strip()
            update = "refs/heads/master " + head + " refs/heads/master " + base
            check_push("origin", allowed, update)
            cases += 1
            # Rewrites remain blocked unless the user-authorized ref and both
            # commit IDs match exactly; authorization never skips privacy scans.
            replacement = git(
                "commit-tree", head + "^{tree}", "-p", base,
                "-m", "Synthetic replacement",
            ).decode().strip()
            rewrite = "refs/heads/master " + replacement + " refs/heads/master " + head
            expect_block(lambda: check_push("origin", allowed, rewrite))
            exact = "refs/heads/master " + head + " " + replacement
            git("config", "privacy.authorizedRewrite", exact)
            check_push("origin", allowed, rewrite)
            cases += 1
            for incorrect in (
                "refs/heads/other " + head + " " + replacement,
                "refs/heads/master " + base + " " + replacement,
                "refs/heads/master " + head + " " + base,
            ):
                git("config", "privacy.authorizedRewrite", incorrect)
                expect_block(lambda: check_push("origin", allowed, rewrite))
            git("config", "--unset", "privacy.authorizedRewrite")
            expect_block(lambda: check_push("upstream", allowed, update))
            expect_block(lambda: check_push("origin", allowed + "/other", update))
            expect_block(lambda: check_identity("Synthetic Person", identity[1]))
            expect_block(lambda: check_identity(identity[0], "someone" + "@users.noreply.github.com"))
            expect_block(lambda: check_text(identity[1]))
            # A fabricated generic address must not pass identity setup.
            git("config", "privacy.commitEmail", "generic" + "@users.noreply.github.com")
            expect_block(approved_identity)
            git("config", "privacy.commitEmail", identity[1])
            expect_block(lambda: check_text("message " + "ghp_" + "B" * 32))
            # A later clean tree must not conceal a private intermediate commit.
            Path("safe.txt").write_text("ghp_" + "C" * 32, encoding="utf-8")
            git("add", "safe.txt")
            git("-c", "core.hooksPath=", "commit", "-qm", "Synthetic unsafe ancestor")
            Path("safe.txt").write_text("public again\n", encoding="utf-8")
            git("add", "safe.txt")
            git("-c", "core.hooksPath=", "commit", "-qm", "Synthetic cleanup")
            head = git("rev-parse", "HEAD").decode().strip()
            update = "refs/heads/master " + head + " refs/heads/master " + base
            expect_block(lambda: check_push("origin", allowed, update))
            print("PRIVACY_SELF_TEST_PASS cases=" + str(cases))
    finally:
        os.chdir(original)
        os.environ.clear()
        os.environ.update(old_env)


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        self_test()
        # Same command also checks the actual staged repository, when configured.
        check_staged()
    elif args == ["--staged"]:
        check_staged()
    elif len(args) == 2 and args[0] == "--message":
        check_text(Path(args[1]).read_text(encoding="utf-8"))
    elif len(args) == 3 and args[0] == "--pre-push":
        check_push(args[1], args[2], sys.stdin.read())
    else:
        raise Blocked("unsupported privacy check invocation")
    print("PRIVACY_CHECK_PASS")


if __name__ == "__main__":
    try:
        main()
    except Blocked as error:
        # Blocked messages are fixed categories; never include inspected values.
        print("PRIVACY_CHECK_BLOCKED: " + str(error), file=sys.stderr)
        sys.exit(1)
    except (OSError, UnicodeError, ValueError, AssertionError, RecursionError):
        # Never include exception text: OS/Git errors may contain private paths.
        print("PRIVACY_CHECK_BLOCKED: inspect locally; no sensitive values were printed.", file=sys.stderr)
        sys.exit(1)
