"""ID check: field extraction and the decision logic. No network, no image library.

The page text below is made up for the tests (invented name and number); it
only imitates the label layout that the book's patterns expect.
"""
from __future__ import annotations

import datetime as dt

import id_fields
from conftest import load_handler

handler = load_handler("id_processor")

PAGE = """<html><head><style>p{color:red}</style><script>var x = 1;</script></head><body>
<h1>Personal Data</h1><p>سارة احمد محمود</p>
<p>رقم الطالب: 800123456</p><p>الكلية: كلية الهندسة 2025/Fourth</p>
<p>حالة القيد: مقيد</p></body></html>"""

ALLOWED = {"portal.university.example"}
LINK = "https://portal.university.example/student?id=abc"


def test_html_to_text_drops_tags_scripts_and_styles():
    text = id_fields.html_to_text(PAGE)
    assert "<" not in text and "var x" not in text and "color:red" not in text
    assert "رقم الطالب: 800123456" in text


def test_extract_fields_from_page_text():
    fields = id_fields.extract_fields(id_fields.html_to_text(PAGE))
    assert fields["student_number"] == "800123456"
    assert fields["student_name"] == "سارة احمد محمود"
    assert fields["college"] == "كلية الهندسة"
    assert fields["year_level"] == "2025/Fourth"
    assert fields["enrollment_status"] == "مقيد"
    assert id_fields.is_complete(fields)


def test_missing_fields_are_left_out():
    fields = id_fields.extract_fields("nothing useful here")
    assert fields == {}
    assert not id_fields.is_complete(fields)


def test_only_https_links_to_allowed_hosts_are_fetched():
    assert handler.url_allowed(LINK, ALLOWED)
    assert not handler.url_allowed("http://portal.university.example/x", ALLOWED)  # not https
    assert not handler.url_allowed("https://evil.example/x", ALLOWED)
    assert not handler.url_allowed("https://portal.university.example.evil.example/x", ALLOWED)
    assert not handler.url_allowed("https://169.254.169.254/latest/meta-data/", ALLOWED)
    assert not handler.url_allowed(LINK, set())  # empty list: nothing is fetched


def test_user_id_comes_from_the_object_key():
    assert handler.user_id_from_key("id-scans/user-1/photo.jpg") == "user-1"
    assert handler.user_id_from_key("other/user-1/photo.jpg") is None
    assert handler.user_id_from_key("id-scans/photo.jpg") is None


def check(decoder, fetcher=lambda url: PAGE, allowed=ALLOWED):
    return handler.check_image(b"jpeg-bytes", allowed_hosts=allowed, decoder=decoder, fetcher=fetcher)


def test_verified_when_the_portal_page_has_the_fields():
    outcome = check(lambda image: LINK)
    assert outcome["status"] == "verified"
    assert outcome["fields"]["student_number"] == "800123456"


def test_rejected_when_the_barcode_points_elsewhere():
    fetched = []
    outcome = check(lambda image: "https://evil.example/steal", fetcher=lambda url: fetched.append(url) or PAGE)
    assert outcome == {"status": "rejected", "reason": "host_not_allowed"}
    assert fetched == []  # the link was never requested


def test_manual_review_paths():
    def no_decoder(image):
        raise handler.DecoderUnavailable("pyzbar missing")

    def failing_fetch(url):
        raise TimeoutError

    assert check(no_decoder)["reason"] == "decoder_unavailable"
    assert check(lambda image: None)["reason"] == "no_barcode"
    assert check(lambda image: LINK, fetcher=failing_fetch)["reason"] == "fetch_failed"
    assert check(lambda image: LINK, fetcher=lambda url: "<p>empty</p>")["reason"] == "fields_missing"
    for outcome in (check(no_decoder), check(lambda image: None)):
        assert outcome["status"] == "manual_review"


class FakeS3:
    def get_object(self, Bucket, Key):
        class Body:
            @staticmethod
            def read():
                return b"jpeg-bytes"

        return {"Body": Body()}


def test_handler_writes_the_outcome_on_the_user_row(tables, monkeypatch):
    monkeypatch.setenv("ALLOWED_ID_HOSTS", "portal.university.example")
    event = {"Records": [
        {"s3": {"bucket": {"name": "b"}, "object": {"key": "id-scans/user-1/card+photo.jpg"}}},
        {"s3": {"bucket": {"name": "b"}, "object": {"key": "somewhere-else/file.jpg"}}},
    ]}
    result = handler.lambda_handler(event, None, s3=FakeS3(), table=tables["users"],
                                    decoder=lambda image: LINK, fetcher=lambda url: PAGE)
    assert [r["status"] for r in result["results"]] == ["verified", "ignored"]
    row = tables["users"].get_item(Key={"userId": "user-1"})["Item"]
    assert row["accountStatus"] == "verified"
    assert row["studentId"] == "800123456"
    assert row["collegeIdImage"] == "id-scans/user-1/card photo.jpg"  # key is URL-decoded
    assert "student_name" not in row and "سارة" not in str(row)  # the name is not copied
    dt.datetime.fromisoformat(row["idCheckedAt"])
