"""The serverless back end of the scooter rental system, as one CDK stack.

Sign-in        Cognito user pool (email, optional MFA)
ID check       S3 bucket (id-scans/) -> Lambda -> Users table
Rides          API Gateway HTTP API (JWT authorizer) -> Lambda -> Scooters / Trips tables
Scooters       IoT Core policy + two topic rules -> Lambda -> Scooters table, SNS alerts
Operations     CloudWatch log retention and two error alarms

This follows "phase 2" of the cloud chapter in the project book, plus the IoT
Core side described in the final book. It is synthesised and tested, never
deployed (see cloud/README.md).
"""
from __future__ import annotations

from pathlib import Path

from aws_cdk import CfnOutput, Duration, RemovalPolicy, Stack
from aws_cdk import aws_apigatewayv2 as apigw
from aws_cdk import aws_cloudwatch as cloudwatch
from aws_cdk import aws_cloudwatch_actions as cw_actions
from aws_cdk import aws_cognito as cognito
from aws_cdk import aws_dynamodb as dynamodb
from aws_cdk import aws_iam as iam
from aws_cdk import aws_iot as iot
from aws_cdk import aws_lambda as lambda_
from aws_cdk import aws_logs as logs
from aws_cdk import aws_s3 as s3
from aws_cdk import aws_s3_notifications as s3n
from aws_cdk import aws_sns as sns
from aws_cdk.aws_apigatewayv2_authorizers import HttpJwtAuthorizer
from aws_cdk.aws_apigatewayv2_integrations import HttpLambdaIntegration
from constructs import Construct

LAMBDAS = Path(__file__).resolve().parent.parent / "lambdas"


class ScooterStack(Stack):
    def __init__(self, scope: Construct, construct_id: str, *, org_id: str = "campus-demo",
                 allowed_id_hosts: str = "", **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        # ---- Sign-in ---------------------------------------------------------
        pool = cognito.UserPool(
            self, "Users",
            self_sign_up_enabled=True,
            sign_in_aliases=cognito.SignInAliases(email=True),
            auto_verify=cognito.AutoVerifiedAttrs(email=True),
            mfa=cognito.Mfa.OPTIONAL,
            mfa_second_factor=cognito.MfaSecondFactor(otp=True, sms=False),
            password_policy=cognito.PasswordPolicy(min_length=10, require_digits=True, require_lowercase=True,
                                                   require_uppercase=True, require_symbols=False),
            account_recovery=cognito.AccountRecovery.EMAIL_ONLY,
            removal_policy=RemovalPolicy.RETAIN,
        )
        client = pool.add_client("WebApp", generate_secret=False,
                                 auth_flows=cognito.AuthFlow(user_srp=True))

        # ---- Data ------------------------------------------------------------
        # Same four tables (and keys) as the DynamoDB console screenshot in the book.
        def table(name: str, key: str) -> dynamodb.Table:
            return dynamodb.Table(
                self, name,
                partition_key=dynamodb.Attribute(name=key, type=dynamodb.AttributeType.STRING),
                billing_mode=dynamodb.BillingMode.PAY_PER_REQUEST,
                point_in_time_recovery_specification=dynamodb.PointInTimeRecoverySpecification(
                    point_in_time_recovery_enabled=True),
                removal_policy=RemovalPolicy.RETAIN,
            )

        users = table("UsersTable", "userId")
        scooters = table("ScootersTable", "scooterId")
        trips = table("TripsTable", "tripId")
        table("PaymentsTable", "paymentId")  # modelled in the book; no payment code in this repo
        trips.add_global_secondary_index(
            index_name="byUser",
            partition_key=dynamodb.Attribute(name="userId", type=dynamodb.AttributeType.STRING),
            sort_key=dynamodb.Attribute(name="startTime", type=dynamodb.AttributeType.STRING),
        )

        # ---- ID images -------------------------------------------------------
        id_bucket = s3.Bucket(
            self, "IdImages",
            block_public_access=s3.BlockPublicAccess.BLOCK_ALL,
            encryption=s3.BucketEncryption.S3_MANAGED,
            enforce_ssl=True,
            # An ID photo is only needed for the check; it is deleted after 30 days.
            lifecycle_rules=[s3.LifecycleRule(prefix="id-scans/", expiration=Duration.days(30))],
            removal_policy=RemovalPolicy.RETAIN,
        )

        alerts = sns.Topic(self, "OperatorAlerts", enforce_ssl=True)

        def function(name: str, folder: str, env: dict[str, str], timeout: int = 10) -> lambda_.Function:
            return lambda_.Function(
                self, name,
                runtime=lambda_.Runtime.PYTHON_3_12,
                handler="handler.lambda_handler",
                code=lambda_.Code.from_asset(str(LAMBDAS / folder)),
                memory_size=128,
                timeout=Duration.seconds(timeout),
                environment=env,
                log_group=logs.LogGroup(self, f"{name}Logs", retention=logs.RetentionDays.ONE_MONTH,
                                        removal_policy=RemovalPolicy.DESTROY),
            )

        # ---- ID check: S3 upload -> Lambda -> Users table ---------------------
        id_processor = function("IdProcessor", "id_processor",
                                {"USERS_TABLE": users.table_name, "ALLOWED_ID_HOSTS": allowed_id_hosts},
                                timeout=20)
        id_processor.add_to_role_policy(iam.PolicyStatement(
            actions=["s3:GetObject"], resources=[id_bucket.arn_for_objects("id-scans/*")]))
        users.grant(id_processor, "dynamodb:UpdateItem")
        id_bucket.add_event_notification(
            s3.EventType.OBJECT_CREATED, s3n.LambdaDestination(id_processor),
            s3.NotificationKeyFilter(prefix="id-scans/"),
        )

        # ---- Ride API ----------------------------------------------------------
        rides = function("RidesApi", "rides_api", {
            "USERS_TABLE": users.table_name, "SCOOTERS_TABLE": scooters.table_name,
            "TRIPS_TABLE": trips.table_name, "ID_BUCKET": id_bucket.bucket_name, "ORG_ID": org_id,
        })
        users.grant(rides, "dynamodb:GetItem")
        scooters.grant(rides, "dynamodb:Scan", "dynamodb:UpdateItem")
        trips.grant(rides, "dynamodb:GetItem", "dynamodb:PutItem", "dynamodb:UpdateItem", "dynamodb:Query")
        rides.add_to_role_policy(iam.PolicyStatement(  # for the pre-signed upload link
            actions=["s3:PutObject"], resources=[id_bucket.arn_for_objects("id-scans/*")]))
        rides.add_to_role_policy(iam.PolicyStatement(
            actions=["iot:Publish"],
            resources=[self.format_arn(service="iot", resource="topic",
                                       resource_name=f"campus/{org_id}/scooter/*/cmd")],
        ))

        api = apigw.HttpApi(self, "Api", default_authorizer=HttpJwtAuthorizer(
            "CognitoJwt", f"https://cognito-idp.{self.region}.amazonaws.com/{pool.user_pool_id}",
            jwt_audience=[client.user_pool_client_id]))
        integration = HttpLambdaIntegration("RidesIntegration", rides)
        for method, path in [
            (apigw.HttpMethod.GET, "/scooters"),
            (apigw.HttpMethod.POST, "/scooters/{id}/book"),
            (apigw.HttpMethod.POST, "/trips/{id}/end"),
            (apigw.HttpMethod.GET, "/history"),
            (apigw.HttpMethod.POST, "/id-upload-url"),
        ]:
            api.add_routes(path=path, methods=[method], integration=integration)

        # ---- Scooters: IoT Core ------------------------------------------------
        # One policy for every scooter. The thing-name variable confines each
        # device to its own topics: connect only as itself, publish only its own
        # telemetry, receive only its own commands.
        thing = "${iot:Connection.Thing.ThingName}"

        def iot_arn(kind: str, name: str) -> str:
            return self.format_arn(service="iot", resource=kind, resource_name=name)

        iot.CfnPolicy(self, "ScooterPolicy", policy_document={
            "Version": "2012-10-17",
            "Statement": [
                {"Effect": "Allow", "Action": "iot:Connect", "Resource": iot_arn("client", thing)},
                {"Effect": "Allow", "Action": "iot:Publish", "Resource": [
                    iot_arn("topic", f"campus/{org_id}/scooter/{thing}/telemetry"),
                    iot_arn("topic", f"campus/{org_id}/alerts/critical"),
                ]},
                {"Effect": "Allow", "Action": "iot:Subscribe",
                 "Resource": iot_arn("topicfilter", f"campus/{org_id}/scooter/{thing}/cmd")},
                {"Effect": "Allow", "Action": "iot:Receive",
                 "Resource": iot_arn("topic", f"campus/{org_id}/scooter/{thing}/cmd")},
            ],
        })

        ingest = function("TelemetryIngest", "telemetry_ingest",
                          {"SCOOTERS_TABLE": scooters.table_name, "ALERT_TOPIC_ARN": alerts.topic_arn})
        scooters.grant(ingest, "dynamodb:UpdateItem")
        alerts.grant_publish(ingest)
        ingest.add_permission("AllowIotRules", principal=iam.ServicePrincipal("iot.amazonaws.com"),
                              source_account=self.account)
        for rule_id, sql in [
            ("TelemetryRule", f"SELECT *, topic(4) AS topic_device, timestamp() AS received_at "
                              f"FROM 'campus/{org_id}/scooter/+/telemetry'"),
            ("AlertRule", f"SELECT *, timestamp() AS received_at FROM 'campus/{org_id}/alerts/critical'"),
        ]:
            iot.CfnTopicRule(self, rule_id, topic_rule_payload=iot.CfnTopicRule.TopicRulePayloadProperty(
                sql=sql, aws_iot_sql_version="2016-03-23",
                actions=[iot.CfnTopicRule.ActionProperty(
                    lambda_=iot.CfnTopicRule.LambdaActionProperty(function_arn=ingest.function_arn))],
            ))

        # ---- Operations --------------------------------------------------------
        for name, fn in [("IdProcessorErrors", id_processor), ("TelemetryIngestErrors", ingest)]:
            alarm = cloudwatch.Alarm(
                self, name,
                metric=fn.metric_errors(period=Duration.minutes(5)),
                threshold=1, evaluation_periods=1,
                treat_missing_data=cloudwatch.TreatMissingData.NOT_BREACHING,
            )
            alarm.add_alarm_action(cw_actions.SnsAction(alerts))

        CfnOutput(self, "ApiUrl", value=api.api_endpoint)
        CfnOutput(self, "UserPoolId", value=pool.user_pool_id)
        CfnOutput(self, "WebClientId", value=client.user_pool_client_id)
        CfnOutput(self, "IdBucketName", value=id_bucket.bucket_name)
