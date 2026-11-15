"""Shared test setup: import paths for the three handlers and moto-backed tables."""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path

import boto3
import pytest
from moto import mock_aws

CLOUD = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CLOUD))
sys.path.insert(0, str(CLOUD / "lambdas" / "id_processor"))  # handler imports id_fields from here

# moto never contacts AWS, but boto3 still insists on a region and some keys.
os.environ.setdefault("AWS_DEFAULT_REGION", "us-east-1")
os.environ.setdefault("AWS_ACCESS_KEY_ID", "testing")
os.environ.setdefault("AWS_SECRET_ACCESS_KEY", "testing")


def load_handler(folder: str):
    """Each Lambda folder has its own handler.py; load one under a unique name."""
    spec = importlib.util.spec_from_file_location(f"{folder}_handler", CLOUD / "lambdas" / folder / "handler.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def tables():
    """Users, Scooters and Trips tables with the same keys as the stack."""
    with mock_aws():
        db = boto3.resource("dynamodb")

        def make(name: str, key: str, **extra):
            return db.create_table(
                TableName=name,
                KeySchema=[{"AttributeName": key, "KeyType": "HASH"}],
                AttributeDefinitions=[{"AttributeName": key, "AttributeType": "S"}] + extra.pop("attrs", []),
                BillingMode="PAY_PER_REQUEST",
                **extra,
            )

        users = make("Users", "userId")
        scooters = make("Scooters", "scooterId")
        trips = make(
            "Trips", "tripId",
            attrs=[{"AttributeName": "userId", "AttributeType": "S"},
                   {"AttributeName": "startTime", "AttributeType": "S"}],
            GlobalSecondaryIndexes=[{
                "IndexName": "byUser",
                "KeySchema": [{"AttributeName": "userId", "KeyType": "HASH"},
                              {"AttributeName": "startTime", "KeyType": "RANGE"}],
                "Projection": {"ProjectionType": "ALL"},
            }],
        )
        yield {"users": users, "scooters": scooters, "trips": trips}
