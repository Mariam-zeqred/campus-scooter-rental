"""Monthly cost estimate for the stack at a given fleet size.

    python cost.py                      # 10, 100 and 1 000 scooters
    python cost.py --scooters 250
    python cost.py --parked-period 300  # what one setting changes

Unit prices: AWS Price List bulk API, region us-east-1, read on 2026-11-15
(offer files AWSIoT 20261003, AWSLambda 20261111, AmazonDynamoDB 20261111,
AmazonS3 20261109, AmazonApiGateway 20261112, AmazonCognito 20261020).
Everything else is an assumption, listed in `Usage`; none of it was measured,
because the stack has never been deployed. Free tiers are ignored except
Cognito's 10 000 monthly users, which is part of its price list.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass

PRICE = {  # USD
    "iot_message": 1.00 / 1e6,
    "iot_connection_minute": 0.08 / 1e6,
    "iot_rule_trigger": 0.15 / 1e6,
    "iot_rule_action": 0.15 / 1e6,
    "lambda_request": 0.20 / 1e6,
    "lambda_gb_second": 0.0000166667,
    "dynamodb_write": 0.625 / 1e6,
    "dynamodb_read": 0.125 / 1e6,
    "s3_gb_month": 0.023,
    "s3_put": 0.005 / 1e3,
    "http_api_request": 1.00 / 1e6,
    "cognito_user": 0.015,  # Essentials plan, per monthly active user
}
COGNITO_FREE_USERS = 10_000
DAYS = 30


@dataclass
class Usage:
    """Assumptions about how the fleet is used. Change them on the command line or here."""

    scooters: int = 100
    rides_per_scooter_day: float = 8
    ride_minutes: float = 10
    riding_period_s: float = 1       # firmware: one record per second in a ride
    parked_period_s: float = 30      # firmware: one record per 30 s when parked
    riders_per_scooter: float = 20   # monthly active users
    api_calls_per_ride: float = 6    # list, book, end, history, ...
    new_riders_share: float = 0.05   # of users, each uploads one ID photo a month
    id_photo_mb: float = 0.3
    scooter_row_kb: float = 0.3
    lambda_memory_gb: float = 0.128
    ingest_ms: float = 20            # assumed run times
    api_ms: float = 50
    id_check_ms: float = 2000


def lambda_cost(invocations: float, ms: float, u: Usage) -> float:
    return invocations * (PRICE["lambda_request"] + ms / 1000 * u.lambda_memory_gb * PRICE["lambda_gb_second"])


def estimate(u: Usage) -> dict[str, float]:
    """Monthly cost per line, in USD."""
    riding_s = u.rides_per_scooter_day * u.ride_minutes * 60
    records = u.scooters * DAYS * (riding_s / u.riding_period_s + (86_400 - riding_s) / u.parked_period_s)
    rides = u.scooters * DAYS * u.rides_per_scooter_day
    users = u.scooters * u.riders_per_scooter
    uploads = users * u.new_riders_share
    api_calls = rides * u.api_calls_per_ride

    # Listing scooters is a table scan: it reads every row (0.5 read unit per 4 KB).
    scan_units = max(1.0, u.scooters * u.scooter_row_kb / 4 * 0.5)
    reads = rides * (4 + 2 * scan_units)
    writes = records + rides * 4 + uploads

    return {
        "IoT Core messages": (records + rides) * PRICE["iot_message"],
        "IoT Core connection": u.scooters * DAYS * 1440 * PRICE["iot_connection_minute"],
        "IoT rules": records * (PRICE["iot_rule_trigger"] + PRICE["iot_rule_action"]),
        "Lambda": lambda_cost(records, u.ingest_ms, u) + lambda_cost(api_calls, u.api_ms, u)
                  + lambda_cost(uploads, u.id_check_ms, u),
        "DynamoDB writes": writes * PRICE["dynamodb_write"],
        "DynamoDB reads": reads * PRICE["dynamodb_read"],
        "API Gateway": api_calls * PRICE["http_api_request"],
        "S3 (ID photos)": uploads * (PRICE["s3_put"] + u.id_photo_mb / 1024 * PRICE["s3_gb_month"]),
        "Cognito": max(0.0, users - COGNITO_FREE_USERS) * PRICE["cognito_user"],
    }


def report(u: Usage) -> str:
    lines = estimate(u)
    total = sum(lines.values())
    out = [f"{u.scooters} scooters, {u.scooters * u.riders_per_scooter:.0f} monthly riders, "
           f"{u.scooters * DAYS * u.rides_per_scooter_day:,.0f} rides/month"]
    for name, cost in sorted(lines.items(), key=lambda kv: -kv[1]):
        out.append(f"  {name:<22} ${cost:>10,.2f}  {cost / total:5.1%}")
    out.append(f"  {'total':<22} ${total:>10,.2f}  (${total / u.scooters:.2f} per scooter)")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scooters", type=int, nargs="*", default=[10, 100, 1000])
    parser.add_argument("--parked-period", type=float, default=30, help="seconds between records when parked")
    args = parser.parse_args()
    for n in args.scooters:
        print(report(Usage(scooters=n, parked_period_s=args.parked_period)))
        print()


if __name__ == "__main__":
    main()
