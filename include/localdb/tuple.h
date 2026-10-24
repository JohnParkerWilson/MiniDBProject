#pragma once

#include "localdb/schema.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace localdb {

using Value = std::variant<std::int32_t, std::int64_t, bool, std::string>;

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

    const Value& value_at(std::size_t index) const;
    std::size_t size() const noexcept { return values_.size(); }
    const std::vector<Value>& values() const noexcept { return values_; }

    // Encoding: uint16 field count, then for each field a uint8 null flag and
    // a type-specific payload. STRING32 always occupies 32 bytes; VARCHAR is
    // encoded as uint32 length + bytes.
    std::vector<std::uint8_t> serialize(const Schema& schema) const;
    static Tuple deserialize(const Schema& schema,
                             const std::vector<std::uint8_t>& bytes);

private:
    std::vector<Value> values_;
};

}  // namespace localdb
