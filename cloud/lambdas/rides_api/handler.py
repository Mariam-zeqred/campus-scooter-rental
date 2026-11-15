"""Ride API behind API Gateway (HTTP API, Cognito JWT authorizer).

    GET  /scooters               scooters that can be booked now
    POST /scooters/{id}/book     start a trip (one scooter, one rider)
    POST /trips/{id}/end         end the trip, compute the fare, lock the scooter
    GET  /history                the caller's trips, newest first
    POST /id-upload-url          a one-time link for uploading the ID photo

The routes follow the ride lifecycle of the team's Node/Express API described
in the project book, rebuilt on DynamoDB for the serverless design. The
caller's identity always comes from the verified token (`sub`), never from the
request body.
"""
from __future__ import annotations

import datetime as dt
import json
import os
import uuid
from decimal import Decimal
from typing import Any

MIN_BOOKABLE_BATTERY = 15  # percent
# Fare placeholders (the book has a `fare` field but no tariff).
UNLOCK_FEE = Decimal(os.environ.get("UNLOCK_FEE", "5"))
PER_MINUTE = Decimal(os.environ.get("PER_MINUTE", "1"))


def _reply(status: int, body: Any) -> dict:
    return {
        "statusCode": status,
        "headers": {"content-type": "application/json"},
        "body": json.dumps(body, default=lambda v: float(v) if isinstance(v, Decimal) else str(v)),
    }


def fare_for(seconds: int) -> Decimal:
    """Unlock fee plus a per-minute rate, every started minute counts."""
    minutes = max(1, -(-seconds // 60))
    return UNLOCK_FEE + PER_MINUTE * minutes


class Api:
    """The routes, with every AWS dependency passed in so tests can use fakes."""

    def __init__(self, users: Any, scooters: Any, trips: Any, *, publish: Any, presign: Any, org: str) -> None:
        self.users, self.scooters, self.trips = users, scooters, trips
        self.publish, self.presign, self.org = publish, presign, org

    def list_scooters(self) -> dict:
        from boto3.dynamodb.conditions import Attr

        found = self.scooters.scan(
            FilterExpression=Attr("status").eq("available") & Attr("batteryLevel").gte(MIN_BOOKABLE_BATTERY)
        )
        return _reply(200, {"scooters": found.get("Items", [])})

    def book(self, user_id: str, scooter_id: str, now: dt.datetime) -> dict:
        user = self.users.get_item(Key={"userId": user_id}).get("Item")
        if not user or user.get("accountStatus") != "verified":
            return _reply(403, {"error": "account is not verified yet"})
        trip_id = str(uuid.uuid4())
        try:
            # The condition is what prevents two riders booking the same scooter:
            # DynamoDB applies check-and-write as one step.
            booked = self.scooters.update_item(
                Key={"scooterId": scooter_id},
                UpdateExpression="SET #s = :booked, bookedBy = :u, currentTrip = :t",
                ConditionExpression="#s = :available AND batteryLevel >= :min",
                ExpressionAttributeNames={"#s": "status"},
                ExpressionAttributeValues={
                    ":booked": "booked", ":available": "available", ":u": user_id,
                    ":t": trip_id, ":min": MIN_BOOKABLE_BATTERY,
                },
                ReturnValues="ALL_NEW",
            )["Attributes"]
        except self.scooters.meta.client.exceptions.ConditionalCheckFailedException:
            return _reply(409, {"error": "scooter is not available"})
        self.trips.put_item(Item={
            "tripId": trip_id, "userId": user_id, "scooterId": scooter_id, "status": "ONGOING",
            "startTime": now.isoformat(timespec="seconds"), "startLocation": booked.get("location"),
        })
        return _reply(201, {"tripId": trip_id, "scooterId": scooter_id})

    def end_trip(self, user_id: str, trip_id: str, now: dt.datetime) -> dict:
        trip = self.trips.get_item(Key={"tripId": trip_id}).get("Item")
        if not trip or trip["userId"] != user_id:
            return _reply(404, {"error": "trip not found"})
        if trip["status"] != "ONGOING":
            return _reply(409, {"error": "trip already ended"})
        seconds = int((now - dt.datetime.fromisoformat(trip["startTime"])).total_seconds())
        fare = fare_for(seconds)
        scooter = self.scooters.update_item(
            Key={"scooterId": trip["scooterId"]},
            UpdateExpression="SET #s = :available REMOVE bookedBy, currentTrip",
            ExpressionAttributeNames={"#s": "status"},
            ExpressionAttributeValues={":available": "available"},
            ReturnValues="ALL_NEW",
        )["Attributes"]
        self.trips.update_item(
            Key={"tripId": trip_id},
            UpdateExpression="SET #s = :done, endTime = :e, #d = :d, fare = :f, endLocation = :l",
            ExpressionAttributeNames={"#s": "status", "#d": "duration"},
            ExpressionAttributeValues={
                ":done": "COMPLETED", ":e": now.isoformat(timespec="seconds"),
                ":d": seconds, ":f": fare, ":l": scooter.get("location"),
            },
        )
        # Backstop: the phone normally locks over BLE; this also tells the scooter over MQTT.
        self.publish(f"campus/{self.org}/scooter/{trip['scooterId']}/cmd", "LOCK")
        return _reply(200, {"tripId": trip_id, "duration": seconds, "fare": fare})

    def history(self, user_id: str) -> dict:
        from boto3.dynamodb.conditions import Key

        found = self.trips.query(
            IndexName="byUser", KeyConditionExpression=Key("userId").eq(user_id), ScanIndexForward=False, Limit=50
        )
        return _reply(200, {"trips": found.get("Items", [])})

    def id_upload_url(self, user_id: str) -> dict:
        key = f"id-scans/{user_id}/{uuid.uuid4()}.jpg"
        return _reply(200, {"key": key, "url": self.presign(key)})

    def route(self, event: dict, now: dt.datetime) -> dict:
        claims = event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {})
        user_id = claims.get("sub")
        if not user_id:
            return _reply(401, {"error": "not signed in"})
        params = event.get("pathParameters") or {}
        match event.get("routeKey"):
            case "GET /scooters":
                return self.list_scooters()
            case "POST /scooters/{id}/book":
                return self.book(user_id, params["id"], now)
            case "POST /trips/{id}/end":
                return self.end_trip(user_id, params["id"], now)
            case "GET /history":
                return self.history(user_id)
            case "POST /id-upload-url":
                return self.id_upload_url(user_id)
        return _reply(404, {"error": "no such route"})


_api: Api | None = None


def lambda_handler(event: dict, context: Any) -> dict:
    global _api
    if _api is None:  # built once per container, reused across invocations
        import boto3

        db = boto3.resource("dynamodb")
        iot = boto3.client("iot-data")
        s3 = boto3.client("s3")
        bucket = os.environ["ID_BUCKET"]
        _api = Api(
            db.Table(os.environ["USERS_TABLE"]),
            db.Table(os.environ["SCOOTERS_TABLE"]),
            db.Table(os.environ["TRIPS_TABLE"]),
            publish=lambda topic, text: iot.publish(topic=topic, qos=1, payload=text.encode()),
            presign=lambda key: s3.generate_presigned_url(
                "put_object", Params={"Bucket": bucket, "Key": key, "ContentType": "image/jpeg"}, ExpiresIn=300
            ),
            org=os.environ.get("ORG_ID", "campus-demo"),
        )
    return _api.route(event, dt.datetime.now(dt.UTC))
