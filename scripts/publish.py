"""Publish this repository's `main` as the `dev` branches of the two public faces:

  paperboat           (remote `origin`)    everything but examples/backends
  paperboat-examples  (remote `examples`)  examples/backends, at its root

Each publish is one new commit on top of the remote `dev` (its tree is `main`'s as it is
now), so neither repo ever gets `main`'s own history. It goes up as a pull request into
`dev`, on a branch of its own (`publish/<date>-<title>`); its body lists the `main`
commits since the last publish. Merge it on GitHub (squash: one commit on `dev`) once CI
passes. Files git ignores (TODO.md, GUIDELINES.md, notes for assistants) are never in
`main`'s tree; as a second guard, a commit holding any of them is refused.

    python3 scripts/publish.py "Route loop wires around blocks"   # a PR on both
    python3 scripts/publish.py --dry-run "…"                      # show, push nothing
    python3 scripts/publish.py --only paperboat "…"

Needs a clean working tree (what's published is what's committed), push access to both
remotes and the GitHub CLI (gh auth login, gh auth setup-git; the `workflow` scope to
publish a change to .github/workflows).
"""

import argparse
import datetime
import os
import re
import subprocess
import sys
import tempfile

FORBIDDEN = ("CLAUDE.md", "AGENTS.md", "GEMINI.md", "TODO.md", "GUIDELINES.md", ".claude/", ".remember/")
EXAMPLES = "examples/backends"


def git(*args: str, env: dict | None = None, check: bool = True) -> str:
    proc = subprocess.run(["git", *args], capture_output=True, text=True, env={**os.environ, **(env or {})})
    if check and proc.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def framework_tree(rev: str = "HEAD") -> str:
    """main's tree without the examples."""
    with tempfile.TemporaryDirectory() as tmp:
        env = {"GIT_INDEX_FILE": os.path.join(tmp, "index")}
        git("read-tree", rev, env=env)
        git("rm", "-r", "-q", "--cached", "--ignore-unmatch", EXAMPLES, env=env)
        return git("write-tree", env=env)


def examples_tree(rev: str = "HEAD") -> str:
    return git("rev-parse", "-q", "--verify", f"{rev}:{EXAMPLES}", check=False)


def since_published(tree_of, paths: list[str], parent: str) -> list[str]:
    """Subjects of main's commits touching `paths` since `parent` was published: back to the
    one whose tree it is, or else to the last one made before it."""
    published, when = git("rev-parse", f"{parent}^{{tree}}"), int(git("log", "-1", "--format=%ct", parent))
    out = []
    for line in git("log", "--format=%H %ct %s", "-n", "300", "HEAD", "--", *paths).splitlines():
        sha, at, subject = line.split(" ", 2)
        if tree_of(sha) == published or int(at) <= when:
            break
        out.append(subject)
    return out


def check_clean(tree: str, name: str) -> None:
    files = git("ls-tree", "-r", "--name-only", tree).splitlines()
    bad = [f for f in files for x in FORBIDDEN
           if f == x or f.endswith("/" + x) or f.startswith(x) or ("/" + x) in ("/" + f)]
    if bad:
        sys.exit(f"{name}: refusing to publish {sorted(set(bad))}")


def repo_of(remote: str) -> str:
    """owner/name of a GitHub remote."""
    url = git("remote", "get-url", remote)
    m = re.search(r"github\.com[:/](.+?)(?:\.git)?$", url)
    if not m:
        sys.exit(f"{remote} is not a GitHub remote: {url}")
    return m.group(1)


def publish(name: str, remote: str, tree_of, paths: list[str], message: str, dry: bool) -> None:
    tree = tree_of("HEAD")
    check_clean(tree, name)
    git("fetch", "-q", remote, "dev", check=False)
    parent = git("rev-parse", "-q", "--verify", f"{remote}/dev", check=False)
    if parent and git("rev-parse", f"{parent}^{{tree}}") == tree:
        print(f"{name}: dev is already up to date ({parent[:7]})")
        return
    args = ["commit-tree", tree, "-m", message] + (["-p", parent] if parent else [])
    commit = git(*args)
    changed = git("diff", "--stat", parent, commit).splitlines()[-1:] if parent else ["first commit"]
    print(f"{name}: {commit[:7]} on {parent[:7] if parent else '(new)'}: {changed[0] if changed else ''}")
    commits = since_published(tree_of, paths, parent) if parent else []
    body = "\n".join(f"- {c}" for c in reversed(commits)) or message
    if dry:
        print(body)
        return
    if not parent:  # nothing to open a PR into yet: the first publish is dev itself
        git("push", remote, f"{commit}:refs/heads/dev")
        print(f"{name}: pushed to {remote} dev")
        return
    slug = re.sub(r"[^a-z0-9]+", "-", message.lower()).strip("-")[:40]
    branch = f"publish/{datetime.date.today():%Y-%m-%d}-{slug}"
    git("push", "-f", remote, f"{commit}:refs/heads/{branch}")
    proc = subprocess.run(["gh", "pr", "create", "-R", repo_of(remote), "--base", "dev", "--head", branch,
                           "--title", message, "--body", body], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{name}: pushed {branch}, but gh pr create failed: {proc.stderr.strip()}")
    print(f"{name}: {proc.stdout.strip()}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Publish main as pull requests into the dev branches of paperboat and paperboat-examples")
    ap.add_argument("message", help="the commit message (short, imperative)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only", choices=["paperboat", "examples"])
    args = ap.parse_args()
    os.chdir(git("rev-parse", "--show-toplevel"))
    if git("status", "--porcelain", "--untracked-files=no"):
        sys.exit("commit or stash your changes first: only what is committed is published")
    if args.only in (None, "paperboat"):
        publish("paperboat", "origin", framework_tree, [".", f":(exclude){EXAMPLES}"], args.message, args.dry_run)
    if args.only in (None, "examples"):
        publish("paperboat-examples", "examples", examples_tree, [EXAMPLES], args.message, args.dry_run)


if __name__ == "__main__":
    main()
