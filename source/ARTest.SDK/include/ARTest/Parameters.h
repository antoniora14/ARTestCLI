#pragma once

#include "Result.h"
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>

namespace artest::sdk
{
// A call-scoped read-only view. Copy individual values if they are needed later.
class Parameters final
{
  public:
    explicit Parameters(const Json &values) : m_values(values)
    {
        if (!values.is_object())
            throw std::invalid_argument("Parameters must be a JSON object.");
    }
    Parameters(Json &&) = delete;
    [[nodiscard]] bool Contains(std::string_view name) const
    {
        return m_values.contains(name);
    }
    [[nodiscard]] const Json &Raw() const noexcept
    {
        return m_values;
    }

    template <class T> [[nodiscard]] T Get(std::string_view name) const
    {
        const auto found = m_values.find(name);
        if (found == m_values.end())
            throw std::invalid_argument("Required parameter is missing: " + std::string{name});
        const auto &value = *found;
        if constexpr (std::is_same_v<T, Json>)
            return value;
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (value.is_string())
                return value.get<T>();
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (value.is_boolean())
                return value.get<T>();
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if (value.is_number_unsigned())
            {
                const auto number = value.get<std::uint64_t>();
                if (std::in_range<T>(number))
                    return static_cast<T>(number);
            }
            else if (value.is_number_integer())
            {
                const auto number = value.get<std::int64_t>();
                if (std::in_range<T>(number))
                    return static_cast<T>(number);
            }
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (value.is_number())
            {
                const auto number = value.get<double>();
                if (std::isfinite(number) && number >= -(std::numeric_limits<T>::max)() &&
                    number <= (std::numeric_limits<T>::max)())
                    return static_cast<T>(number);
            }
        }
        else
            static_assert(std::is_same_v<T, void>,
                          "Use a number, bool, std::string or Json parameter type.");
        if constexpr (!std::is_same_v<T, Json>)
            throw std::invalid_argument("Parameter has the wrong type or is out of range: " +
                                        std::string{name});
    }

    template <class T> [[nodiscard]] T Optional(std::string_view name, T fallback) const
    {
        return Contains(name) ? Get<T>(name) : std::move(fallback);
    }

  private:
    const Json &m_values;
};
} // namespace artest::sdk
