# ARTest Schema Profile 1

This is a deliberately bounded, fail-closed subset for offline component
configuration and parameter validation. It is not a full JSON Schema
2020-12 implementation. The manifest's structural editor schema is a different
document and may use the full JSON Schema vocabulary.

Every schema node is an object with an explicit single type: object, array,
string, integer, number, boolean, or null. Schema nesting is limited to 32.
Each schema file must be inside its package and at most 1 MiB.

Supported keywords:

- Annotations: $schema, $id, title, description (strings), and default.
  Annotations do not trigger network access; default does not mutate the plan.
- Object: properties, required, additionalProperties (boolean only).
  Required properties must be declared and names must be unique.
- Array: items (another profile schema), minItems, maxItems.
- String: minLength, maxLength, counted as Unicode characters in valid UTF-8.
- Numeric: minimum, maximum (inclusive).
- enum: a non-empty array; values must also satisfy the declared type.

Integer values must use an integer JSON representation (1 is accepted; 1.0 is
not). No coercion is performed. Optional properties remain optional. Unknown
properties are rejected only when additionalProperties is false.

All other keywords, including $ref, pattern, oneOf, anyOf, format and custom
validators, are rejected as SCHEMA_KEYWORD_UNSUPPORTED. Unsupported schema
features must never be silently ignored or delegated to a DLL during compilation.
Use simple schemas for D3.2. A future validator/profile upgrade must be explicit,
tested against a conformance corpus, and versioned.

Important limit: offline validation covers declared structure, ranges and binding
contracts, not every command-specific semantic relationship or hardware response.
For example CAN frame DLC/data-length equality and 29-bit ID parsing are validated
by the driver at runtime. A compile pass is not proof that execution will pass.
Drivers must still defensively validate calls because other ABI clients can invoke
them without the plan compiler.

Primary diagnostics include SCHEMA_INVALID, SCHEMA_KEYWORD_UNSUPPORTED,
PARAMETER_TYPE_INVALID, PARAMETER_REQUIRED, PARAMETER_UNKNOWN,
PARAMETER_RANGE_INVALID, PARAMETER_SIZE_INVALID and PARAMETER_ENUM_INVALID.
Compilation includes parameter paths in diagnostics. The intrinsic Wait command
retains WAIT_DURATION_INVALID for compatibility.
