"""Publish this repository's `main` as the `dev` branches of the two public faces:

  paperboat           (remote `origin`)    everything but examples/backends
  paperboat-examples  (remote `examples`)  examples/backends, at its root

Each publish is a pull request into `dev`, on a branch of its own
(`publish/<date>-<title>`), holding a copy of each `main` commit since the last publish
(same message, author and date; its tree is that repo's part of `main`), so a feature's
commits stay apart in the PR while `dev` never gets `main`'s own history. Merge it on
GitHub once CI passes. A publish while an earlier one is still open stacks on it (its PR
targets the earlier branch, and GitHub retargets it to `dev` when that one merges). Files git ignores (TODO.md, GUIDELINES.md, notes for assistants) are never in
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
        git("rm", "-r", "-q", "-f", "--cached", "--ignore-unmatch", EXAMPLES, env=env)
        return git("write-tree", env=env)


def examples_tree(rev: str = "HEAD") -> str:
    return git("rev-parse", "-q", "--verify", f"{rev}:{EXAMPLES}", check=False)


def since_published(tree_of, paths: list[str], parent: str) -> list[str]:
    """main's commits touching `paths` since `parent` was published, oldest first: back to
    the one whose tree it is, or else to the last one made before it."""
    published, when = git("rev-parse", f"{parent}^{{tree}}"), int(git("log", "-1", "--format=%ct", parent))
    out = []
    for line in git("log", "--format=%H %ct", "-n", "300", "HEAD", "--", *paths).splitlines():
        sha, at = line.split(" ")
        if tree_of(sha) == published or int(at) <= when:
            break
        out.append(sha)
    return out[::-1]


def replay(sha: str, tree: str, parent: str | None) -> str:
    """A copy of main's commit `sha` with this tree, on `parent`."""
    name, email, date, message = git("log", "-1", "--format=%an%x00%ae%x00%ad%x00%B", sha).split("\0", 3)
    env = {"GIT_AUTHOR_NAME": name, "GIT_AUTHOR_EMAIL": email, "GIT_AUTHOR_DATE": date}
    return git("commit-tree", tree, "-m", message.strip(), *(["-p", parent] if parent else []), env=env)


def open_publish(repo: str) -> str | None:
    """The branch of a publish PR still open in `repo`, if any (the newest)."""
    proc = subprocess.run(["gh", "pr", "list", "-R", repo, "--state", "open", "--json", "headRefName",
                           "-q", ".[].headRefName"], capture_output=True, text=True)
    branches = [b for b in proc.stdout.split() if b.startswith("publish/")]
    return max(branches) if branches else None


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
    repo = repo_of(remote)
    base = open_publish(repo) or "dev"
    git("fetch", "-q", remote, base, check=False)
    parent = git("rev-parse", "-q", "--verify", f"{remote}/{base}", check=False)
    if parent and git("rev-parse", f"{parent}^{{tree}}") == tree:
        print(f"{name}: {base} is already up to date ({parent[:7]})")
        return
    if not parent:  # nothing to open a PR into yet: the first publish is dev itself
        commit = git("commit-tree", tree, "-m", message)
        if not dry:
            git("push", remote, f"{commit}:refs/heads/dev")
        print(f"{name}: {commit[:7]} pushed to {remote} dev")
        return
    head, subjects = parent, []
    for sha in since_published(tree_of, paths, parent):
        t = tree_of(sha)
        if t == git("rev-parse", f"{head}^{{tree}}"):
            continue  # nothing of this repo's changed
        check_clean(t, name)
        head = replay(sha, t, head)
        subjects.append(git("log", "-1", "--format=%s", head))
    if git("rev-parse", f"{head}^{{tree}}") != tree:  # what the log didn't reach
        head = git("commit-tree", tree, "-m", message, "-p", head)
        subjects.append(message)
    changed = git("diff", "--stat", parent, head).splitlines()[-1:]
    print(f"{name}: {len(subjects)} commits on {base} ({parent[:7]}): {changed[0] if changed else ''}")
    body = "\n".join(f"- {c}" for c in subjects)
    if dry:
        print(body)
        return
    slug = re.sub(r"[^a-z0-9]+", "-", message.lower()).strip("-")[:40]
    branch = f"publish/{datetime.date.today():%Y-%m-%d}-{slug}"
    git("push", "-f", remote, f"{head}:refs/heads/{branch}")
    proc = subprocess.run(["gh", "pr", "create", "-R", repo, "--base", base, "--head", branch,
                           "--title", message, "--body", body], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{name}: pushed {branch}, but gh pr create failed: {proc.stderr.strip()}")
    print(f"{name}: {proc.stdout.strip()}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Publish main as pull requests into the dev branches of paperboat and paperboat-examples")
    ap.add_argument("message", help="the PR title (short, imperative)")
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
