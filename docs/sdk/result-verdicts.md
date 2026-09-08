# Invocation outcomes and test verdicts

Native SDK 0.3.0 adds Result::TestVerdict. Python SDK 0.1.0 provides Result.verdict.
Both return a technically successful call carrying artest.schema.command-result.v1:

    {"verdict":"failed","data":{"value":4.2,"unit":"V","minimum":4.8},
     "dataSchema":"artest.schema.measurement.voltage.v1","message":"Below minimum"}

The Engine validates the envelope and maps only passed/failed to measurement
verdicts. Invalid envelopes are errors. Existing WithData responses retain their
technical-success interpretation; arbitrary JSON fields are not inferred as
measurement verdicts.

Engine creation option resultSchemaVersion selects the serialized run contract:

| Selection | Schema | Behavior |
| --- | --- | --- |
| Omitted or 1 | artest.schema.run-result.v1 | Existing JSON shape, no added outcome fields |
| 2 | artest.schema.run-result.v2 | Every step/attempt includes schema, indeterminate, dataSchema and data under outcome |

Both versions preserve accurate step/run status and summaries. Version 2 preserves
structured data for reporting. The result handle copies its selected version and
remains independent of later Engine destruction. Unsupported versions are rejected.
The native ABI and Engine function table do not change.

An indeterminate outcome means the Engine cannot confirm external side effects.
It overrides retries and continue-on-failure policy. It is not a normal failed
limit check. Process cleanup failure may make the overall run Error even if an
individual measurement passed.

The SDK ships both run-result schemas. Consumers should select the contract they
understand; do not silently validate version 2 against the version 1 schema.
