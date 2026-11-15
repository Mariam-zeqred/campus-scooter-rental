"""Pull student fields out of the text of a university ID page.

A student card carries a barcode / QR code whose content is a link to a page on
the university portal. The page text mixes English and Arabic labels. The
patterns below are the ones printed in the project book (chapter 20, the
team's barcode-verification script); this module only wraps them so they can
be unit-tested without any network or image library.
"""
from __future__ import annotations

import re
from html.parser import HTMLParser

# Field name -> pattern, as in the book. Arabic labels:
#   رقم الطالب = student number, الكلية = college, حالة القيد = enrolment status.
PATTERNS: dict[str, str] = {
    "student_name": r"Personal Data\s*(?:لم يحصل علي التطعيم\s*)?([ء-ي\s]+)\s*رقم الطالب",
    "student_number": r"رقم الطالب:\s*(\d+)",
    "college": r"الكلية:\s*(.*?)\s*\d{4}/",
    "year_level": r"(\d{4}/[A-Za-z]+)",
    "enrollment_status": r"حالة القيد:\s*([ء-ي]+)",
}

REQUIRED = ("student_number", "student_name", "college")


class _TextOnly(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.parts: list[str] = []
        self._skip = 0

    def handle_starttag(self, tag: str, attrs: list) -> None:
        if tag in ("script", "style"):
            self._skip += 1

    def handle_endtag(self, tag: str) -> None:
        if tag in ("script", "style") and self._skip:
            self._skip -= 1

    def handle_data(self, data: str) -> None:
        if not self._skip:
            self.parts.append(data)


def html_to_text(html: str) -> str:
    """Visible text of an HTML page, tags removed, whitespace collapsed."""
    parser = _TextOnly()
    parser.feed(html)
    return re.sub(r"\s+", " ", " ".join(parser.parts)).strip()


def extract_fields(text: str) -> dict[str, str]:
    """Return every field whose pattern matched; missing fields are left out."""
    found: dict[str, str] = {}
    for name, pattern in PATTERNS.items():
        match = re.search(pattern, text)
        if match:
            found[name] = match.group(1).strip()
    return found


def is_complete(fields: dict[str, str]) -> bool:
    """True when the fields needed to accept a student automatically are present."""
    return all(fields.get(name) for name in REQUIRED)
