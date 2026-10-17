#!/usr/bin/env python3
"""Reuse successful runtime/guest checks when only the iPad frontend changed."""
import json
import os
import subprocess
import urllib.request


def frontend_path(path):
    return path.startswith(("apps/ios/", "docs/")) or path in {
        ".github/workflows/ios.yml", ".github/workflows/linux.yml",
        "scripts/ios-ci-scope.py", "AGENTS.md",
    }


def main():
    if os.environ.get("TRAMES_FULL_CHECK") == "true":
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write("frontend_only=false\n")
        return
    repo = os.environ["GITHUB_REPOSITORY"]
    workflow = os.environ.get("TRAMES_VALIDATION_WORKFLOW", "ios.yml")
    request = urllib.request.Request(
        f"https://api.github.com/repos/{repo}/actions/workflows/{workflow}/runs?status=success&per_page=30",
        headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                 "Accept": "application/vnd.github+json"},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        runs = json.load(response)["workflow_runs"]
    for run in runs:
        sha = run["head_sha"]
        if subprocess.run(["git", "merge-base", "--is-ancestor", sha, "HEAD"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            continue
        paths = subprocess.check_output(["git", "diff", "--name-only", sha, "HEAD"], text=True).splitlines()
        if all(frontend_path(path) for path in paths):
            if os.environ.get("TRAMES_NEEDS_GUEST_ARTIFACTS", "true") == "true":
                # Targeted frontend runs do not republish unchanged guest artifacts.
                artifacts_request = urllib.request.Request(
                    f"https://api.github.com/repos/{repo}/actions/runs/{run['id']}/artifacts?per_page=100",
                    headers=dict(request.headers),
                )
                with urllib.request.urlopen(artifacts_request, timeout=30) as response:
                    artifacts = {a["name"] for a in json.load(response)["artifacts"] if not a["expired"]}
                if not {"full-musl-library-and-source", "vulkan-compute-driver-and-native-source"} <= artifacts:
                    continue
            print(f"Reusing runtime and guest validation from successful run {run['id']} ({sha}).")
            with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                output.write(f"frontend_only=true\nbaseline_run={run['id']}\nbaseline_sha={sha}\n")
            return
        # The newest successful ancestor includes all available previous evidence.
        break
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        output.write("frontend_only=false\n")


if __name__ == "__main__":
    main()
