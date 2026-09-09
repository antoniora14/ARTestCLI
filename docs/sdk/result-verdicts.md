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
Selecting a run-result schema does not change the Engine function table.
The original D4.2 verdict addition did not change native ABI 0.1; C-01 separately
introduces ABI 0.2 for structured external-effect uncertainty.

An indeterminate outcome means the Engine cannot confirm external side effects.
It overrides retries and continue-on-failure policy. It is not a normal failed
limit check. Process cleanup failure may make the overall run Error even if an
individual measurement passed.

The SDK ships both run-result schemas. Consumers should select the contract they
understand; do not silently validate version 2 against the version 1 schema.

SDK 0.4.0 / Python SDK 0.2.0 let live drivers explicitly signal an uncertain
write through Result::Indeterminate / Result.indeterminate. This is independent
of Error, TimedOut or Cancelled. The v2 outcome retains the indeterminate flag
on the step and attempt. Legacy v1 retains its shape and failure diagnostic,
and still suppresses replay. See [the authoring contract](external-effect-uncertainty.md).
