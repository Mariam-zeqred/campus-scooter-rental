"""Scooter telemetry and alerts, delivered by AWS IoT Core topic rules.

    campus/<org>/scooter/<id>/telemetry  ->  latest state on the Scooters row
    campus/<org>/alerts/critical         ->  SNS notification + lastAlert on the row

The rule adds `topic_device` (the scooter id taken from the topic) and
`received_at` (broker time, ms). A record is only accepted when the id inside
the payload equals the id in the topic it was published on; together with the
IoT policy (a scooter may only publish under its own id) this means one
scooter cannot overwrite another's row.
"""
from __future__ import annotations

import json
import os
from decimal import Decimal
from typing import Any

STATES = {"LOCKED", "UNLOCKING", "RIDING", "WARNING", "EMERGENCY"}
ZONES = {"CLEAR", "SLOW", "RED"}
ALERTS = {"OBSTACLE", "TILT", "THEFT", "GEOFENCE"}


class Rejected(ValueError):
    """The message does not look like something a scooter would send."""


def validate_telemetry(msg: dict) -> dict:
    """Return the cleaned fields to store, or raise Rejected."""
    device = msg.get("device_id")
    if not device or device != msg.get("topic_device"):
        raise Rejected("device_id does not match the topic")
    if msg.get("state") not in STATES or msg.get("zone", "CLEAR") not in ZONES:
        raise Rejected("unknown state or zone")
    battery = msg.get("battery") or {}
    percent = battery.get("percent")
    if not isinstance(percent, (int, float)) or not 0 <= percent <= 100:
        raise Rejected("battery percent out of range")

    # Device time if its clock is set, otherwise the broker's receive time.
    seen = int(msg.get("timestamp") or 0) or int(msg.get("received_at", 0)) // 1000
    item: dict[str, Any] = {
        "deviceState": msg["state"],
        "zone": msg.get("zone", "CLEAR"),
        "batteryLevel": int(percent),
        "lastSeen": seen,
    }
    gps = msg.get("gps")
    if gps:  # null means "no fix": keep the last known position
        lat, lng = gps.get("lat"), gps.get("lng")
        if not (isinstance(lat, (int, float)) and isinstance(lng, (int, float))):
            raise Rejected("gps without numbers")
        if not (-90 <= lat <= 90 and -180 <= lng <= 180):
            raise Rejected("gps out of range")
        # GeoJSON order is [longitude, latitude], as in the book's data model.
        item["location"] = {"type": "Point", "coordinates": [Decimal(str(lng)), Decimal(str(lat))]}
    return item


def store_telemetry(table: Any, device: str, item: dict) -> bool:
    """Update the scooter row unless a newer record is already stored.

    Scooters replay records they buffered while offline, so messages can
    arrive out of order; the condition keeps old ones from winning.
    """
    names = {f"#{k}": k for k in item}
    values = {f":{k}": v for k, v in item.items()}
    try:
        table.update_item(
            Key={"scooterId": device},
            UpdateExpression="SET " + ", ".join(f"#{k} = :{k}" for k in item),
            ConditionExpression="attribute_not_exists(#lastSeen) OR #lastSeen <= :lastSeen",
            ExpressionAttributeNames=names,
            ExpressionAttributeValues=values,
        )
        return True
    except table.meta.client.exceptions.ConditionalCheckFailedException:
        return False


def handle_alert(msg: dict, table: Any, sns: Any, topic_arn: str) -> dict:
    device, alert = msg.get("device_id"), msg.get("alert")
    if not device or alert not in ALERTS:
        raise Rejected("bad alert")
    when = int(msg.get("timestamp") or 0) or int(msg.get("received_at", 0)) // 1000
    table.update_item(
        Key={"scooterId": device},
        UpdateExpression="SET lastAlert = :a, lastAlertAt = :t",
        ExpressionAttributeValues={":a": alert, ":t": when},
    )
    sns.publish(
        TopicArn=topic_arn,
        Subject=f"Scooter {device}: {alert}",
        Message=json.dumps({"scooterId": device, "alert": alert, "at": when}),
    )
    return {"stored": True, "kind": "alert"}


def lambda_handler(event: dict, context: Any, *, table: Any = None, sns: Any = None) -> dict:
    if table is None:
        import boto3

        table = boto3.resource("dynamodb").Table(os.environ["SCOOTERS_TABLE"])
        sns = sns or boto3.client("sns")
    try:
        if "alert" in event:
            return handle_alert(event, table, sns, os.environ.get("ALERT_TOPIC_ARN", ""))
        item = validate_telemetry(event)
        return {"stored": store_telemetry(table, event["device_id"], item), "kind": "telemetry"}
    except Rejected as why:
        # Bad input is logged and dropped; raising would only make IoT retry it.
        print(json.dumps({"rejected": str(why)}))
        return {"stored": False, "reason": str(why)}
