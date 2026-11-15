#!/usr/bin/env python3
"""`make plan`: synthesise the CDK stack and list what it would create.

This runs entirely on this computer. It needs no AWS account, sends nothing to
AWS and creates nothing. The template is written to cloud/cdk.out/.
To compare against a real account you would run `npx cdk diff` yourself; this
repository never runs `cdk deploy`.
"""
from __future__ import annotations

import collections
import json
import subprocess
import sys
from pathlib import Path

CLOUD = Path(__file__).resolve().parent.parent / "cloud"


def main() -> None:
    subprocess.run([sys.executable, "app.py"], cwd=CLOUD, check=True)
    template = json.loads((CLOUD / "cdk.out" / "CampusScooter.template.json").read_text(encoding="utf-8"))
    counts = collections.Counter(r["Type"] for r in template["Resources"].values())
    print(f"Plan: {sum(counts.values())} resources would be created (nothing was sent to AWS)")
    for kind, n in sorted(counts.items()):
        print(f"  {n:>2}  {kind}")
    print("Outputs:", ", ".join(sorted(template.get("Outputs", {}))))


if __name__ == "__main__":
    main()
