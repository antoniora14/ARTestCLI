#pragma once

#include "Result.h"
#include <cmath>
#include <cstddef>
#include <string_view>

namespace artest::sdk
{
// Deliberately exposes only a supported subset of ARTest Schema Profile 1.
// Values are owned locally; callers cannot inject unsupported raw schema keywords.
class Schema final
{
  public:
    static Schema Object() { return Schema{Json{{"type", "object"}, {"properties", Json::object()},
                                               {"additionalProperties", false}}}; }
    static Schema Integer() { return Schema{Json{{"type", "integer"}}}; }
    static Schema Number() { return Schema{Json{{"type", "number"}}}; }
    static Schema Boolean() { return Schema{Json{{"type", "boolean"}}}; }
    static Schema String() { return Schema{Json{{"type", "string"}}}; }
    static Schema Array(const Schema &items)
    {
        return Schema{Json{{"type", "array"}, {"items", items.m_value}}};
    }

    Schema &Required(std::string name, const Schema &schema)
    {
        AddProperty(name, schema);
        if (!m_value.contains("required")) m_value["required"] = Json::array();
        m_value["required"].push_back(std::move(name));
        return *this;
    }
    Schema &Optional(std::string name, const Schema &schema)
    {
        AddProperty(name, schema);
        return *this;
    }
    Schema &AllowAdditionalProperties(bool allow = true)
    {
        RequireType("object");
        m_value["additionalProperties"] = allow;
        return *this;
    }
    Schema &Minimum(double value) { return NumericBound("minimum", value); }
    Schema &Maximum(double value) { return NumericBound("maximum", value); }
    Schema &MinLength(std::size_t value) { return SizeBound("string", "minLength", value); }
    Schema &MaxLength(std::size_t value) { return SizeBound("string", "maxLength", value); }
    Schema &MinItems(std::size_t value) { return SizeBound("array", "minItems", value); }
    Schema &MaxItems(std::size_t value) { return SizeBound("array", "maxItems", value); }
    Schema &Description(std::string text)
    {
        if (text.find('\0') != std::string::npos)
            throw std::invalid_argument("Schema description contains a null byte.");
        m_value["description"] = std::move(text);
        return *this;
    }

    [[nodiscard]] Json Document() const
    {
        Validate(m_value, 0);
        // dump also checks UTF-8. Match the Engine's per-schema size bound.
        if (m_value.dump(2).size() + 1 > 1024 * 1024)
            throw std::invalid_argument("Schema exceeds the 1 MiB limit.");
        return m_value;
    }

  private:
    explicit Schema(Json value) : m_value(std::move(value)) {}
    void RequireType(std::string_view type) const
    {
        if (m_value["type"].get_ref<const std::string &>() != type)
            throw std::invalid_argument("Schema keyword does not apply to this type.");
    }
    void AddProperty(const std::string &name, const Schema &schema)
    {
        RequireType("object");
        if (name.empty() || name.find('\0') != std::string::npos ||
            m_value["properties"].contains(name))
            throw std::invalid_argument("Schema property names must be nonempty and unique.");
        m_value["properties"][name] = schema.m_value;
    }
    Schema &NumericBound(const char *keyword, double value)
    {
        if ((m_value["type"] != "integer" && m_value["type"] != "number") || !std::isfinite(value))
            throw std::invalid_argument("Numeric bounds require a number schema and a finite value.");
        m_value[keyword] = value;
        return *this;
    }
    Schema &SizeBound(const char *type, const char *keyword, std::size_t value)
    {
        RequireType(type);
        m_value[keyword] = value;
        return *this;
    }
    static void Validate(const Json &node, unsigned depth)
    {
        if (depth > 32)
            throw std::invalid_argument("Schema exceeds the nesting limit.");
        for (const auto &[low, high] :
             {std::pair{"minimum", "maximum"}, {"minLength", "maxLength"}, {"minItems", "maxItems"}})
            if (node.contains(low) && node.contains(high) && node[low] > node[high])
                throw std::invalid_argument("Schema lower bound exceeds its upper bound.");
        if (node["type"] == "object")
            for (const auto &child : node["properties"]) Validate(child, depth + 1);
        else if (node["type"] == "array")
            Validate(node["items"], depth + 1);
    }
    Json m_value;
};
} // namespace artest::sdk
