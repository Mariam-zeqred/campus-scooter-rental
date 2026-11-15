"""The cost model: arithmetic checks, not price checks."""
from __future__ import annotations

import pytest

import cost


def test_one_parked_scooter_by_hand():
    # No rides, no riders: 86 400 / 30 = 2 880 records a day, 86 400 a month.
    u = cost.Usage(scooters=1, rides_per_scooter_day=0, riders_per_scooter=0)
    lines = cost.estimate(u)
    records = 86_400
    assert lines["IoT Core messages"] == pytest.approx(records * 1.00 / 1e6)
    assert lines["IoT rules"] == pytest.approx(records * 0.30 / 1e6)
    assert lines["DynamoDB writes"] == pytest.approx(records * 0.625 / 1e6)
    assert lines["IoT Core connection"] == pytest.approx(43_200 * 0.08 / 1e6)
    assert lines["API Gateway"] == 0 and lines["Cognito"] == 0


def test_cognito_is_free_up_to_ten_thousand_users():
    assert cost.estimate(cost.Usage(scooters=500))["Cognito"] == 0            # 10 000 riders
    assert cost.estimate(cost.Usage(scooters=1000))["Cognito"] == pytest.approx(10_000 * 0.015)


def test_slower_parked_reporting_is_cheaper():
    base = sum(cost.estimate(cost.Usage(scooters=100)).values())
    slow = sum(cost.estimate(cost.Usage(scooters=100, parked_period_s=300)).values())
    assert slow < base


def test_scan_reads_grow_faster_than_the_fleet():
    small = cost.estimate(cost.Usage(scooters=100))["DynamoDB reads"]
    large = cost.estimate(cost.Usage(scooters=1000))["DynamoDB reads"]
    assert large > 10 * small  # ten times the scooters, more than ten times the read cost


def test_report_lists_every_line_and_a_total():
    text = cost.report(cost.Usage(scooters=10))
    for name in cost.estimate(cost.Usage(scooters=10)):
        assert name in text
    assert "total" in text and "per scooter" in text
