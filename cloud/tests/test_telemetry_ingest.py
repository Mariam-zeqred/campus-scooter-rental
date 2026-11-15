"""Telemetry and alerts from the scooters, against a moto DynamoDB table."""
from __future__ import annotations

import json

import pytest
from conftest import load_handler

handler = load_handler("telemetry_ingest")


def record(**changes):
    """A message exactly as the firmware builds it (see firmware test_cloud_record_and_topics)."""
    msg = json.loads(
        '{"device_id":"SC-0001","timestamp":1767225600,"state":"RIDING","zone":"CLEAR","throttle":35,'
        '"gps":{"lat":31.040948,"lng":31.378470},"battery":{"voltage":38.2,"percent":58},'
        '"diagnostics":{"rssi":-65,"uptime":12044}}'
    )
    msg.update({"topic_device": "SC-0001", "received_at": 1767225601000})  # added by the IoT rule
    msg.update(changes)
    return msg


def test_firmware_record_is_stored(tables):
    result = handler.lambda_handler(record(), None, table=tables["scooters"])
    assert result == {"stored": True, "kind": "telemetry"}
    row = tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]
    assert row["deviceState"] == "RIDING" and row["batteryLevel"] == 58 and row["lastSeen"] == 1767225600
    assert row["location"]["type"] == "Point"
    assert [float(v) for v in row["location"]["coordinates"]] == [31.378470, 31.040948]  # [lng, lat]


def test_telemetry_does_not_touch_the_rental_status(tables):
    tables["scooters"].put_item(Item={"scooterId": "SC-0001", "status": "booked", "bookedBy": "user-1"})
    handler.lambda_handler(record(), None, table=tables["scooters"])
    row = tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]
    assert row["status"] == "booked" and row["bookedBy"] == "user-1"


def test_older_record_does_not_overwrite_newer(tables):
    handler.lambda_handler(record(timestamp=2000000000, state="LOCKED"), None, table=tables["scooters"])
    late = handler.lambda_handler(record(timestamp=1999999990, state="RIDING"), None, table=tables["scooters"])
    assert late["stored"] is False
    assert tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]["deviceState"] == "LOCKED"


def test_no_fix_keeps_the_last_position(tables):
    handler.lambda_handler(record(), None, table=tables["scooters"])
    handler.lambda_handler(record(timestamp=1767225700, gps=None), None, table=tables["scooters"])
    row = tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]
    assert row["lastSeen"] == 1767225700 and "location" in row


def test_unset_clock_falls_back_to_broker_time(tables):
    handler.lambda_handler(record(timestamp=0), None, table=tables["scooters"])
    assert tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]["lastSeen"] == 1767225601


@pytest.mark.parametrize("changes, reason", [
    ({"topic_device": "SC-0002"}, "does not match"),          # one scooter writing as another
    ({"state": "FLYING"}, "unknown state"),
    ({"battery": {"percent": 140}}, "battery"),
    ({"gps": {"lat": 131.0, "lng": 31.0}}, "gps out of range"),
    ({"gps": {"lat": "x", "lng": 31.0}}, "gps without numbers"),
])
def test_bad_records_are_dropped(tables, changes, reason):
    result = handler.lambda_handler(record(**changes), None, table=tables["scooters"])
    assert result["stored"] is False and reason in result["reason"]
    assert "Item" not in tables["scooters"].get_item(Key={"scooterId": "SC-0001"})


class FakeSns:
    def __init__(self):
        self.sent = []

    def publish(self, **kwargs):
        self.sent.append(kwargs)


def test_alert_notifies_and_marks_the_scooter(tables, monkeypatch):
    monkeypatch.setenv("ALERT_TOPIC_ARN", "arn:aws:sns:us-east-1:000000000000:alerts")
    sns = FakeSns()
    alert = {"device_id": "SC-0001", "timestamp": 1767225600, "alert": "TILT", "received_at": 1767225601000}
    assert handler.lambda_handler(alert, None, table=tables["scooters"], sns=sns)["kind"] == "alert"
    assert sns.sent[0]["Subject"] == "Scooter SC-0001: TILT"
    assert tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]["lastAlert"] == "TILT"

    bad = handler.lambda_handler({"device_id": "SC-0001", "alert": "PARTY"}, None, table=tables["scooters"], sns=sns)
    assert bad["stored"] is False and len(sns.sent) == 1
