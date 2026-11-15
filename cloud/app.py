#!/usr/bin/env python3
"""CDK entry point. `python app.py` writes the CloudFormation template to cdk.out/.

Nothing here talks to AWS: the stack is environment-agnostic, so synthesis
needs no account and no credentials. See README.md for what `make plan` does.
"""
import aws_cdk as cdk

from stacks.scooter_stack import ScooterStack

app = cdk.App(outdir="cdk.out")
ScooterStack(
    app,
    "CampusScooter",
    org_id=app.node.try_get_context("org_id") or "campus-demo",
    # Hosts the ID-check function may fetch (the university portal). Empty = fetch nothing.
    allowed_id_hosts=app.node.try_get_context("allowed_id_hosts") or "",
)
cdk.Tags.of(app).add("project", "campus-scooter-rental")
app.synth()
