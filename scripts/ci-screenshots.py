#!/usr/bin/env python3
"""Pass CI screenshots between jobs through the repository, not artifacts.

The ui-consistency job compares every platform's screenshots with Linux's
(ROADMAP Phase 1 gate 1). Artifact storage has a small quota, so each job
stores its screenshots as one archive in a commit of its own on the branch

    ci-screenshots/<branch>/<platform>

made with GitHub's Git Data API (no git needed) and replaced on every run.
The commit message names the run, so a job never compares an older run's
screenshots. scripts/ci-screenshots.ps1 does the push on Windows.

    ci-screenshots.py push  --name macos --dir shots
    ci-screenshots.py fetch --name macos --dest shots/macos [--any-run]
    ci-screenshots.py fetch-all --dest shots   # shots/screenshots-<name>

Needs GITHUB_TOKEN (contents: write to push), GITHUB_REPOSITORY,
GITHUB_RUN_ID and GITHUB_REF_NAME, as set in a GitHub Actions job.
"""

import argparse
import base64
import http.client
import io
import json
import os
import sys
import tarfile
import time
import urllib.error
import urllib.request
import zipfile


def api(method, path, body=None, tries=4):
    """One GitHub API call. A dropped connection, a body cut off part way
    (http.client.IncompleteRead, seen on the WSL runner fetching a 4 MB
    screenshot archive) or a 5xx answer is tried again, `tries` times in all,
    2, 4, 8 ... seconds apart; other HTTP errors (404 for a missing branch,
    422) go to the caller at once."""
    url = os.environ.get("GITHUB_API_URL", "https://api.github.com") + path
    data = json.dumps(body).encode() if body is not None else None
    for attempt in range(tries):
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.loads(r.read() or b"null")
        except urllib.error.HTTPError as e:
            if e.code < 500 or attempt == tries - 1:
                raise
        except (http.client.HTTPException, urllib.error.URLError, OSError):
            if attempt == tries - 1:
                raise
        print("ci-screenshots: %s %s failed, trying again" % (method, path), file=sys.stderr)
        time.sleep(2 << attempt)


def branch(name):
    # The run's branch, so runs on different branches do not meet.
    return "ci-screenshots/%s/%s" % (os.environ["GITHUB_REF_NAME"], name)


def stamp():
    return "run %s" % os.environ["GITHUB_RUN_ID"]


def push(name, directory):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        tar.add(directory, arcname=".")
    repo = "/repos/" + os.environ["GITHUB_REPOSITORY"]
    blob = api("POST", repo + "/git/blobs",
               {"content": base64.b64encode(buf.getvalue()).decode(), "encoding": "base64"})
    tree = api("POST", repo + "/git/trees",
               {"tree": [{"path": "shots.tar.gz", "mode": "100644", "type": "blob", "sha": blob["sha"]}]})
    commit = api("POST", repo + "/git/commits",
                 {"message": "Screenshots: %s, %s, %s" % (name, stamp(), os.environ.get("GITHUB_SHA", "")),
                  "tree": tree["sha"], "parents": []})
    ref = "heads/" + branch(name)
    try:
        api("PATCH", repo + "/git/refs/" + ref, {"sha": commit["sha"], "force": True})
    except urllib.error.HTTPError as e:
        if e.code not in (404, 422):
            raise
        api("POST", repo + "/git/refs", {"ref": "refs/" + ref, "sha": commit["sha"]})
    print("pushed %s (%d KB) to %s" % (directory, len(buf.getvalue()) // 1024, branch(name)))


def extract(filename, data, dest):
    os.makedirs(dest, exist_ok=True)
    root = os.path.realpath(dest)
    if filename.endswith(".zip"):
        # Windows PowerShell 5.1 may write backslashes in entry names.
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for info in z.infolist():
                rel = info.filename.replace("\\", "/")
                target = os.path.realpath(os.path.join(dest, rel))
                if not target.startswith(root + os.sep) or rel.endswith("/"):
                    continue
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as f:
                    f.write(z.read(info))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
            for member in tar.getmembers():
                target = os.path.realpath(os.path.join(dest, member.name))
                if not member.isfile() or not target.startswith(root + os.sep):
                    continue
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as f:
                    f.write(tar.extractfile(member).read())


def fetch(name, dest, any_run):
    repo = "/repos/" + os.environ["GITHUB_REPOSITORY"]
    try:
        ref = api("GET", repo + "/git/ref/heads/" + branch(name))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("no screenshots from %s" % name)
            return 1
        raise
    commit = api("GET", repo + "/git/commits/" + ref["object"]["sha"])
    if not any_run and stamp() not in commit["message"]:
        print("the screenshots from %s are from another run: %s" % (name, commit["message"]))
        return 1
    tree = api("GET", repo + "/git/trees/" + commit["tree"]["sha"])
    entry = tree["tree"][0]
    blob = api("GET", repo + "/git/blobs/" + entry["sha"])
    extract(entry["path"], base64.b64decode(blob["content"]), dest)
    print("fetched %s into %s" % (name, dest))
    return 0


def fetch_all(dest):
    """Every platform's screenshots from this run, into dest/screenshots-<name>."""
    repo = "/repos/" + os.environ["GITHUB_REPOSITORY"]
    prefix = branch("")
    refs = api("GET", repo + "/git/matching-refs/heads/" + prefix)
    found = 0
    for r in refs:
        name = r["ref"][len("refs/heads/" + prefix):]
        if not name or "/" in name or name == "diffs":
            continue
        if fetch(name, os.path.join(dest, "screenshots-" + name), False) == 0:
            found += 1
    print("%d platforms' screenshots from this run" % found)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["push", "fetch", "fetch-all"])
    ap.add_argument("--name", help="push, fetch: the platform: linux, macos-self-hosted, ...")
    ap.add_argument("--dir", help="push: the screenshots folder")
    ap.add_argument("--dest", help="fetch: where to unpack them")
    ap.add_argument("--any-run", action="store_true", help="fetch: accept another run's screenshots")
    args = ap.parse_args()
    if args.action == "fetch-all":
        if not args.dest:
            ap.error("fetch-all needs --dest")
        return fetch_all(args.dest)
    if not args.name:
        ap.error("push and fetch need --name")
    if args.action == "push":
        if not args.dir:
            ap.error("push needs --dir")
        push(args.name, args.dir)
        return 0
    if not args.dest:
        ap.error("fetch needs --dest")
    return fetch(args.name, args.dest, args.any_run)


if __name__ == "__main__":
    sys.exit(main())
