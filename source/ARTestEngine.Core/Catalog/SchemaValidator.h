#pragma once

#include "../../ThirdParty/json.hpp"
#include "../Diagnostics.h"
#include <set>
#include <string>

namespace artest
{
// ARTest Schema Profile 1 deliberately fails closed on unsupported keywords.
// It is a bounded JSON Schema subset, not a claim of full draft compliance.
class SchemaValidator final
{
  public:
    [[nodiscard]] static OperationResult Check(const nlohmann::json &schema,
                                               const std::string &path = "schema",
                                               unsigned depth = 0)
    {
        if (depth > 32 || !schema.is_object())
            return Invalid("SCHEMA_INVALID", "A schema must be an object within the depth limit.",
                           path);
        static const std::set<std::string> supported{
            "$schema",     "$id",      "title",
            "description", "default",  "type",
            "properties",  "required", "additionalProperties",
            "minimum",     "maximum",  "minLength",
            "maxLength",   "items",    "minItems",
            "maxItems",    "enum"};
        for (const auto &[key, value] : schema.items())
        {
            if (!supported.contains(key))
                return Invalid("SCHEMA_KEYWORD_UNSUPPORTED", "Unsupported schema keyword: " + key,
                               path);
            if ((key == "$schema" || key == "$id" || key == "title" || key == "description") &&
                !value.is_string())
                return Invalid("SCHEMA_INVALID", "Schema annotations must be strings.",
                               path + "/" + key);
        }
        if (!schema.contains("type") || !schema["type"].is_string())
            return Invalid("SCHEMA_INVALID", "An explicit type is required.", path);
        const auto type = schema["type"].get<std::string>();
        if (!std::set<std::string>{"object", "array", "string", "integer", "number", "boolean",
                                   "null"}
                 .contains(type))
            return Invalid("SCHEMA_INVALID", "Unsupported schema type.", path);
        if (schema.contains("enum") && (!schema["enum"].is_array() || schema["enum"].empty()))
            return Invalid("SCHEMA_INVALID", "enum must be a non-empty array.", path);
        for (const auto *key : {"minimum", "maximum"})
            if (schema.contains(key) &&
                ((type != "number" && type != "integer") || !schema[key].is_number()))
                return Invalid("SCHEMA_INVALID", "Numeric bounds require a numeric type and value.",
                               path);
        for (const auto *key : {"minLength", "maxLength", "minItems", "maxItems"})
            if (schema.contains(key) &&
                (!schema[key].is_number_integer() || schema[key] < 0 ||
                 (std::string{key}.find("Length") != std::string::npos ? type != "string"
                                                                       : type != "array")))
                return Invalid("SCHEMA_INVALID",
                               "Size bounds require a non-negative integer and matching type.",
                               path);
        if (schema.contains("properties"))
        {
            if (type != "object" || !schema["properties"].is_object())
                return Invalid("SCHEMA_INVALID", "properties requires an object schema.", path);
            for (const auto &[key, child] : schema["properties"].items())
            {
                auto result = Check(child, path + "/properties/" + key, depth + 1);
                if (!result.Succeeded())
                    return result;
            }
        }
        if (schema.contains("required"))
        {
            if (type != "object" || !schema["required"].is_array())
                return Invalid("SCHEMA_INVALID", "required must be an array on an object schema.",
                               path);
            std::set<std::string> names;
            for (const auto &name : schema["required"])
                if (!name.is_string() || !names.insert(name.get<std::string>()).second ||
                    !schema.contains("properties") ||
                    !schema["properties"].contains(name.get<std::string>()))
                    return Invalid("SCHEMA_INVALID",
                                   "Required names must be unique declared properties.", path);
        }
        if (schema.contains("additionalProperties") &&
            (type != "object" || !schema["additionalProperties"].is_boolean()))
            return Invalid("SCHEMA_INVALID",
                           "additionalProperties must be a boolean on an object schema.", path);
        if (schema.contains("items"))
        {
            if (type != "array")
                return Invalid("SCHEMA_INVALID", "items requires an array schema.", path);
            return Check(schema["items"], path + "/items", depth + 1);
        }
        return OperationResult::Success();
    }

    [[nodiscard]] static OperationResult Validate(const nlohmann::json &schema,
                                                  const nlohmann::json &value,
                                                  const std::string &path)
    {
        auto contract = Check(schema);
        if (!contract.Succeeded())
            return contract;
        return ValidateValue(schema, value, path, 0);
    }

  private:
    static OperationResult Invalid(std::string code, std::string message, std::string path)
    {
        return OperationResult::Failure(std::move(code), std::move(message), std::move(path));
    }

    static OperationResult ValidateValue(const nlohmann::json &schema, const nlohmann::json &value,
                                         const std::string &path, unsigned depth)
    {
        if (depth > 32)
            return Invalid("PARAMETER_DEPTH_INVALID", "Parameter depth limit exceeded.", path);
        const auto type = schema.at("type").get<std::string>();
        const bool matches = type == "object"    ? value.is_object()
                             : type == "array"   ? value.is_array()
                             : type == "string"  ? value.is_string()
                             : type == "integer" ? value.is_number_integer()
                             : type == "number"  ? value.is_number()
                             : type == "boolean" ? value.is_boolean()
                                                 : value.is_null();
        if (!matches)
            return Invalid("PARAMETER_TYPE_INVALID", "Expected " + type + ".", path);
        if (schema.contains("enum"))
        {
            bool found = false;
            for (const auto &item : schema["enum"])
                found = found || item == value;
            if (!found)
                return Invalid("PARAMETER_ENUM_INVALID", "Value is not an allowed enum member.",
                               path);
        }
        if (value.is_number())
            if ((schema.contains("minimum") && value < schema["minimum"]) ||
                (schema.contains("maximum") && value > schema["maximum"]))
                return Invalid("PARAMETER_RANGE_INVALID", "Value is outside the declared range.",
                               path);
        if (value.is_string() || value.is_array())
        {
            // UTF-8 continuation bytes do not count as additional characters.
            std::size_t size = value.size();
            if (value.is_string())
            {
                size = 0;
                for (const unsigned char byte : value.get_ref<const std::string &>())
                    if ((byte & 0xc0U) != 0x80U)
                        ++size;
            }
            const auto minimum = value.is_string() ? "minLength" : "minItems";
            const auto maximum = value.is_string() ? "maxLength" : "maxItems";
            if ((schema.contains(minimum) && size < schema[minimum].get<std::size_t>()) ||
                (schema.contains(maximum) && size > schema[maximum].get<std::size_t>()))
                return Invalid("PARAMETER_SIZE_INVALID", "Value has an invalid length.", path);
        }
        if (value.is_object())
        {
            if (schema.contains("required"))
                for (const auto &name : schema["required"])
                    if (!value.contains(name.get<std::string>()))
                        return Invalid("PARAMETER_REQUIRED", "Required property is missing.",
                                       path + "/" + name.get<std::string>());
            for (const auto &[key, item] : value.items())
            {
                if (schema.contains("properties") && schema["properties"].contains(key))
                {
                    auto result =
                        ValidateValue(schema["properties"][key], item, path + "/" + key, depth + 1);
                    if (!result.Succeeded())
                        return result;
                }
                else if (!schema.value("additionalProperties", true))
                    return Invalid("PARAMETER_UNKNOWN", "Property is not declared by the schema.",
                                   path + "/" + key);
            }
        }
        if (value.is_array() && schema.contains("items"))
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                auto result = ValidateValue(schema["items"], value[i],
                                            path + "/" + std::to_string(i), depth + 1);
                if (!result.Succeeded())
                    return result;
            }
        return OperationResult::Success();
    }
};
} // namespace artest
