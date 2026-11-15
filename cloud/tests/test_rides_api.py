"""Ride lifecycle through the API handler, against moto DynamoDB tables."""
from __future__ import annotations

import datetime as dt
import json
from decimal import Decimal

import pytest
from conftest import load_handler

handler = load_handler("rides_api")
T0 = dt.datetime(2026, 1, 10, 9, 0, 0, tzinfo=dt.UTC)


@pytest.fixture
def api(tables):
    published = []
    tables["users"].put_item(Item={"userId": "rider", "accountStatus": "verified"})
    tables["users"].put_item(Item={"userId": "newbie", "accountStatus": "manual_review"})
    point = {"type": "Point", "coordinates": [Decimal("31.3785"), Decimal("31.0409")]}
    tables["scooters"].put_item(Item={"scooterId": "SC-0001", "status": "available", "batteryLevel": 80,
                                      "location": point})
    tables["scooters"].put_item(Item={"scooterId": "SC-0002", "status": "available", "batteryLevel": 9})
    tables["scooters"].put_item(Item={"scooterId": "SC-0003", "status": "maintenance", "batteryLevel": 90})
    built = handler.Api(tables["users"], tables["scooters"], tables["trips"],
                        publish=lambda topic, text: published.append((topic, text)),
                        presign=lambda key: f"https://upload.invalid/{key}", org="campus-demo")
    built.tables, built.published = tables, published
    return built


def call(api, route, user="rider", now=T0, **path):
    event = {"routeKey": route, "pathParameters": path,
             "requestContext": {"authorizer": {"jwt": {"claims": {"sub": user} if user else {}}}}}
    response = api.route(event, now)
    return response["statusCode"], json.loads(response["body"])


def test_requests_without_a_token_identity_are_refused(api):
    assert call(api, "GET /scooters", user=None)[0] == 401
    assert call(api, "GET /nothing")[0] == 404


def test_only_available_charged_scooters_are_listed(api):
    status, body = call(api, "GET /scooters")
    assert status == 200
    assert [s["scooterId"] for s in body["scooters"]] == ["SC-0001"]


def test_unverified_user_cannot_book(api):
    assert call(api, "POST /scooters/{id}/book", user="newbie", id="SC-0001")[0] == 403
    assert call(api, "POST /scooters/{id}/book", user="stranger", id="SC-0001")[0] == 403


def test_a_scooter_can_only_be_booked_once(api):
    status, body = call(api, "POST /scooters/{id}/book", id="SC-0001")
    assert status == 201
    api.tables["users"].put_item(Item={"userId": "second", "accountStatus": "verified"})
    assert call(api, "POST /scooters/{id}/book", user="second", id="SC-0001")[0] == 409
    scooter = api.tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]
    assert scooter["status"] == "booked" and scooter["bookedBy"] == "rider"
    assert scooter["currentTrip"] == body["tripId"]
    assert call(api, "POST /scooters/{id}/book", id="SC-0002")[0] == 409  # battery too low
    assert call(api, "POST /scooters/{id}/book", id="SC-0003")[0] == 409  # in maintenance
    assert call(api, "POST /scooters/{id}/book", id="SC-9999")[0] == 409  # does not exist


def test_ending_a_trip_computes_the_fare_and_locks_the_scooter(api):
    _, booked = call(api, "POST /scooters/{id}/book", id="SC-0001")
    later = T0 + dt.timedelta(minutes=12, seconds=30)
    status, body = call(api, "POST /trips/{id}/end", now=later, id=booked["tripId"])
    assert status == 200
    assert body["duration"] == 750
    assert body["fare"] == 18.0  # 5 unlock + 13 started minutes x 1
    trip = api.tables["trips"].get_item(Key={"tripId": booked["tripId"]})["Item"]
    assert trip["status"] == "COMPLETED" and trip["endLocation"]["type"] == "Point"
    scooter = api.tables["scooters"].get_item(Key={"scooterId": "SC-0001"})["Item"]
    assert scooter["status"] == "available" and "bookedBy" not in scooter
    assert api.published == [("campus/campus-demo/scooter/SC-0001/cmd", "LOCK")]
    assert call(api, "POST /trips/{id}/end", now=later, id=booked["tripId"])[0] == 409  # already ended


def test_a_rider_cannot_end_someone_elses_trip(api):
    _, booked = call(api, "POST /scooters/{id}/book", id="SC-0001")
    api.tables["users"].put_item(Item={"userId": "other", "accountStatus": "verified"})
    assert call(api, "POST /trips/{id}/end", user="other", id=booked["tripId"])[0] == 404
    assert api.published == []


def test_history_is_per_user_newest_first(api):
    for minute in (0, 30):
        _, booked = call(api, "POST /scooters/{id}/book", now=T0 + dt.timedelta(minutes=minute), id="SC-0001")
        call(api, "POST /trips/{id}/end", now=T0 + dt.timedelta(minutes=minute + 5), id=booked["tripId"])
    _, body = call(api, "GET /history")
    assert [t["startTime"] for t in body["trips"]] == ["2026-01-10T09:30:00+00:00", "2026-01-10T09:00:00+00:00"]
    assert call(api, "GET /history", user="newbie")[1]["trips"] == []


def test_upload_link_is_scoped_to_the_caller(api):
    _, body = call(api, "POST /id-upload-url")
    assert body["key"].startswith("id-scans/rider/") and body["key"].endswith(".jpg")
    assert body["url"].endswith(body["key"])


def test_fare_rounds_up_started_minutes():
    assert handler.fare_for(0) == 6      # minimum one minute
    assert handler.fare_for(60) == 6
    assert handler.fare_for(61) == 7
