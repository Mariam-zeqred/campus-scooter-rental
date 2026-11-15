"""The synthesised CloudFormation template: what is in it and how it is locked down."""
from __future__ import annotations

import json

import aws_cdk as cdk
import pytest
from aws_cdk.assertions import Match, Template

from stacks.scooter_stack import ScooterStack


@pytest.fixture(scope="module")
def template() -> Template:
    app = cdk.App()
    return Template.from_stack(ScooterStack(app, "Test", org_id="campus-demo"))


def test_expected_resources(template):
    template.resource_count_is("AWS::DynamoDB::Table", 4)
    template.resource_count_is("AWS::Cognito::UserPool", 1)
    template.resource_count_is("AWS::S3::Bucket", 1)
    template.resource_count_is("AWS::ApiGatewayV2::Route", 5)
    template.resource_count_is("AWS::IoT::TopicRule", 2)
    template.resource_count_is("AWS::CloudWatch::Alarm", 2)


def test_tables_are_on_demand_with_point_in_time_recovery(template):
    tables = template.find_resources("AWS::DynamoDB::Table")
    for table in tables.values():
        assert table["Properties"]["BillingMode"] == "PAY_PER_REQUEST"
        assert table["Properties"]["PointInTimeRecoverySpecification"]["PointInTimeRecoveryEnabled"] is True
        assert table["DeletionPolicy"] == "Retain"
    keys = sorted(t["Properties"]["KeySchema"][0]["AttributeName"] for t in tables.values())
    assert keys == ["paymentId", "scooterId", "tripId", "userId"]  # as in the book's console screenshot


def test_id_bucket_is_private_encrypted_and_expires_images(template):
    template.has_resource_properties("AWS::S3::Bucket", {
        "PublicAccessBlockConfiguration": {"BlockPublicAcls": True, "BlockPublicPolicy": True,
                                           "IgnorePublicAcls": True, "RestrictPublicBuckets": True},
        "BucketEncryption": Match.any_value(),
        "LifecycleConfiguration": {"Rules": [Match.object_like(
            {"Prefix": "id-scans/", "ExpirationInDays": 30, "Status": "Enabled"})]},
    })
    template.has_resource_properties("Custom::S3BucketNotifications", {
        "NotificationConfiguration": {"LambdaFunctionConfigurations": [Match.object_like(
            {"Events": ["s3:ObjectCreated:*"],
             "Filter": {"Key": {"FilterRules": [{"Name": "prefix", "Value": "id-scans/"}]}}})]},
    })


def test_every_route_requires_a_cognito_token(template):
    routes = template.find_resources("AWS::ApiGatewayV2::Route")
    assert all(r["Properties"]["AuthorizationType"] == "JWT" for r in routes.values())
    assert sorted(r["Properties"]["RouteKey"] for r in routes.values()) == [
        "GET /history", "GET /scooters", "POST /id-upload-url", "POST /scooters/{id}/book", "POST /trips/{id}/end"]


def test_user_pool_has_optional_mfa_and_no_client_secret(template):
    template.has_resource_properties("AWS::Cognito::UserPool", {"MfaConfiguration": "OPTIONAL"})
    template.has_resource_properties("AWS::Cognito::UserPoolClient", {"GenerateSecret": False})


def test_scooter_policy_confines_a_device_to_its_own_topics(template):
    policy = next(iter(template.find_resources("AWS::IoT::Policy").values()))["Properties"]["PolicyDocument"]
    text = json.dumps(policy)
    assert "${iot:Connection.Thing.ThingName}" in text
    assert '"iot:*"' not in text
    for statement in policy["Statement"]:
        assert statement["Action"] in ("iot:Connect", "iot:Publish", "iot:Subscribe", "iot:Receive")
        assert ":*\"" not in json.dumps(statement["Resource"])  # no bare wildcard resource
    assert "scooter/+/telemetry" not in text  # the wildcard belongs in the rule, not the device policy


def test_no_lambda_role_has_wildcard_actions(template):
    for policy in template.find_resources("AWS::IAM::Policy").values():
        for statement in policy["Properties"]["PolicyDocument"]["Statement"]:
            actions = statement["Action"] if isinstance(statement["Action"], list) else [statement["Action"]]
            assert all(not a.endswith(":*") and a != "*" for a in actions), actions


def test_topic_rules_select_from_the_books_topic_tree(template):
    rules = template.find_resources("AWS::IoT::TopicRule")
    sql = sorted(r["Properties"]["TopicRulePayload"]["Sql"] for r in rules.values())
    assert "FROM 'campus/campus-demo/alerts/critical'" in sql[0]
    assert "topic(4) AS topic_device" in sql[1] and "FROM 'campus/campus-demo/scooter/+/telemetry'" in sql[1]
