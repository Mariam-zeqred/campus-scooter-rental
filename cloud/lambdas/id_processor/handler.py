"""ID check, run by S3 when a student uploads a photo of their university card.

    s3://<bucket>/id-scans/<userId>/<file>  ->  this function  ->  Users table

Steps: read the image, decode the barcode into a link, fetch that page from the
university portal, extract the student fields, and record the outcome on the
user's row. Outcomes:

    verified        fields found on an allowed portal page
    manual_review   something could not be read automatically (a person decides)
    rejected        the barcode points somewhere that is not the university portal

The link inside the barcode is untrusted input, so it is only fetched when it
is https and its host is on the ALLOWED_ID_HOSTS list; otherwise anybody could
make this function call an address of their choosing.
"""
from __future__ import annotations

import datetime as dt
import os
import urllib.parse
import urllib.request
from collections.abc import Callable
from typing import Any

import id_fields  # the module next to this file

MAX_PAGE_BYTES = 200_000


class DecoderUnavailable(RuntimeError):
    """Raised when no barcode library is packaged with the function."""


def decode_barcode(image_bytes: bytes) -> str | None:
    """Return the text of the first barcode in the image, or None if there is none.

    Needs Pillow and pyzbar (which needs the zbar system library). They are not
    in the default Lambda runtime; see cloud/README.md for the packaging note.
    """
    try:
        import io

        from PIL import Image
        from pyzbar import pyzbar
    except ImportError as exc:  # pragma: no cover - depends on packaging
        raise DecoderUnavailable(str(exc)) from exc
    codes = pyzbar.decode(Image.open(io.BytesIO(image_bytes)))
    return codes[0].data.decode("utf-8") if codes else None


def fetch_page(url: str) -> str:
    """GET the page, with a short timeout and a size cap."""
    with urllib.request.urlopen(url, timeout=5) as response:  # noqa: S310 - host is allow-listed by the caller
        return response.read(MAX_PAGE_BYTES).decode("utf-8", errors="replace")


def url_allowed(url: str, allowed_hosts: set[str]) -> bool:
    parts = urllib.parse.urlsplit(url)
    return parts.scheme == "https" and (parts.hostname or "").lower() in allowed_hosts


def user_id_from_key(key: str) -> str | None:
    """id-scans/<userId>/<file> -> userId."""
    pieces = key.split("/")
    return pieces[1] if len(pieces) == 3 and pieces[0] == "id-scans" and pieces[1] else None


def check_image(
    image_bytes: bytes,
    *,
    allowed_hosts: set[str],
    decoder: Callable[[bytes], str | None] = decode_barcode,
    fetcher: Callable[[str], str] = fetch_page,
) -> dict[str, Any]:
    """Run the check on one image. Pure apart from the injected decoder/fetcher."""
    try:
        link = decoder(image_bytes)
    except DecoderUnavailable:
        return {"status": "manual_review", "reason": "decoder_unavailable"}
    if not link:
        return {"status": "manual_review", "reason": "no_barcode"}
    if not url_allowed(link, allowed_hosts):
        return {"status": "rejected", "reason": "host_not_allowed"}
    try:
        text = id_fields.html_to_text(fetcher(link))
    except Exception:  # network errors, timeouts, bad encodings: a person looks at it
        return {"status": "manual_review", "reason": "fetch_failed"}
    fields = id_fields.extract_fields(text)
    if not id_fields.is_complete(fields):
        return {"status": "manual_review", "reason": "fields_missing", "fields": fields}
    return {"status": "verified", "reason": "ok", "fields": fields}


def record_outcome(table: Any, user_id: str, key: str, outcome: dict[str, Any], now: dt.datetime) -> None:
    """Write the result on the user's row. The student's name is not copied:
    it was typed at sign-up, and the fewer copies of personal data the better."""
    fields = outcome.get("fields", {})
    table.update_item(
        Key={"userId": user_id},
        UpdateExpression=(
            "SET accountStatus = :s, idCheckReason = :r, idCheckedAt = :t, collegeIdImage = :k, "
            "studentId = :n, college = :c, yearLevel = :y"
        ),
        ExpressionAttributeValues={
            ":s": outcome["status"],
            ":r": outcome["reason"],
            ":t": now.isoformat(timespec="seconds"),
            ":k": key,
            ":n": fields.get("student_number", ""),
            ":c": fields.get("college", ""),
            ":y": fields.get("year_level", ""),
        },
    )


def lambda_handler(event: dict, context: Any, *, s3: Any = None, table: Any = None, **overrides: Any) -> dict:
    if s3 is None or table is None:  # real invocation: build the AWS clients
        import boto3

        s3 = s3 or boto3.client("s3")
        table = table or boto3.resource("dynamodb").Table(os.environ["USERS_TABLE"])
    allowed = {h.strip().lower() for h in os.environ.get("ALLOWED_ID_HOSTS", "").split(",") if h.strip()}
    results = []
    for record in event.get("Records", []):
        bucket = record["s3"]["bucket"]["name"]
        key = urllib.parse.unquote_plus(record["s3"]["object"]["key"])
        user_id = user_id_from_key(key)
        if user_id is None:
            results.append({"key": key, "status": "ignored"})
            continue
        image = s3.get_object(Bucket=bucket, Key=key)["Body"].read()
        outcome = check_image(image, allowed_hosts=allowed, **overrides)
        record_outcome(table, user_id, key, outcome, dt.datetime.now(dt.UTC))
        results.append({"key": key, "status": outcome["status"], "reason": outcome["reason"]})
    return {"results": results}
