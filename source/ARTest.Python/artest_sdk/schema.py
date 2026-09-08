"""Small typed projection of ARTest Schema Profile 1; never infer coercions."""
from dataclasses import MISSING, field, fields, is_dataclass
from typing import get_type_hints, get_origin, get_args
import math

def parameter(*, default=MISSING, minimum=None, maximum=None, description=""):
    metadata = {"description": description}
    if minimum is not None: metadata["minimum"] = minimum
    if maximum is not None: metadata["maximum"] = maximum
    return field(default=default, metadata=metadata)

def schema_for(cls, depth=0):
    if depth > 32: raise ValueError("SCHEMA_INVALID: nesting exceeds 32")
    primitive = {int: "integer", float: "number", str: "string", bool: "boolean", type(None): "null"}
    if cls in primitive: return {"type": primitive[cls]}
    if get_origin(cls) is list:
        return {"type": "array", "items": schema_for(get_args(cls)[0], depth + 1)}
    if not isinstance(cls, type) or not is_dataclass(cls):
        raise TypeError("Use a dataclass or a supported primitive type, not arbitrary annotations")
    hints = get_type_hints(cls)
    result = {"type": "object", "properties": {}, "required": [], "additionalProperties": False}
    for item in fields(cls):
        node = schema_for(hints[item.name], depth + 1)
        for key, value in item.metadata.items():
            if key not in {"minimum", "maximum", "description"}:
                raise ValueError("Unsupported parameter metadata: " + key)
            if key in {"minimum", "maximum"} and node["type"] not in {"integer", "number"}:
                raise ValueError("Numeric bounds require numeric parameters")
            node[key] = value
        if item.default is not MISSING:
            validate(node, item.default, item.name)
            node["default"] = item.default
        elif item.default_factory is not MISSING:
            raise ValueError("Default factories are not metadata-only; use explicit immutable defaults")
        else: result["required"].append(item.name)
        result["properties"][item.name] = node
    return result

def validate(schema, value, path="$"):
    kind = schema["type"]
    valid = {"object": lambda: type(value) is dict, "array": lambda: type(value) is list,
             "integer": lambda: type(value) is int, "number": lambda: type(value) in (int, float),
             "string": lambda: type(value) is str, "boolean": lambda: type(value) is bool,
             "null": lambda: value is None}[kind]()
    if not valid: raise ValueError("PARAMETER_TYPE_INVALID: " + path)
    if kind in ("number", "integer"):
        if (isinstance(value, float) and not math.isfinite(value)) or (type(value) is int and not -(2**63) <= value <= 2**64-1):
            raise ValueError("PARAMETER_RANGE_INVALID: " + path)
        if value < schema.get("minimum", -math.inf) or value > schema.get("maximum", math.inf):
            raise ValueError("PARAMETER_RANGE_INVALID: " + path)
    if kind == "object":
        for name in schema.get("required", []):
            if name not in value: raise ValueError("PARAMETER_REQUIRED: " + path + "." + name)
        for name, child in value.items():
            if name not in schema["properties"]:
                if not schema["additionalProperties"]: raise ValueError("PARAMETER_UNKNOWN: " + path + "." + name)
            else: validate(schema["properties"][name], child, path + "." + name)
    if kind == "array":
        for index, child in enumerate(value): validate(schema["items"], child, f"{path}[{index}]")

def decode(cls, value):
    validate(schema_for(cls), value)
    if is_dataclass(cls):
        hints = get_type_hints(cls)
        return cls(**{name: decode(hints[name], child) for name, child in value.items()})
    if get_origin(cls) is list: return [decode(get_args(cls)[0], item) for item in value]
    return value
