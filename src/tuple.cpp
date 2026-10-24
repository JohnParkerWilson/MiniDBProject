#include "localdb/tuple.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace localdb {
namespace {

template <typename T>
void append_integer(std::vector<std::uint8_t>& output, T value) {
    // Database integers use little-endian encoding: least-significant byte first.
    using U = std::make_unsigned_t<T>;
    U unsigned_value{};
    std::memcpy(&unsigned_value, &value, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<std::uint8_t>(unsigned_value >> (i * 8)));
    }
}

template <typename T>
T read_integer(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    // Read the little-endian representation written by append_integer().
    if (input.size() - offset < sizeof(T)) {
        throw DatabaseError(ErrorCode::Corruption, "Tuple ended during integer decoding");
    }
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<U>(input[offset++]) << (i * 8);
    }
    T result{};
    std::memcpy(&result, &value, sizeof(T));
    return result;
}

void check_type(const Value& value, TypeId type) {
    const bool valid =
        (type == TypeId::Int32 && std::holds_alternative<std::int32_t>(value)) ||
        (type == TypeId::Int64 && std::holds_alternative<std::int64_t>(value)) ||
        (type == TypeId::Boolean && std::holds_alternative<bool>(value)) ||
        (type == TypeId::String32 && std::holds_alternative<std::string>(value)) ||
        (type == TypeId::Varchar && std::holds_alternative<std::string>(value));
    if (!valid) {
        throw DatabaseError(ErrorCode::TypeMismatch, "Tuple value does not match schema");
    }
}

}  // namespace

const Value& Tuple::value_at(std::size_t index) const {
    if (index >= values_.size()) {
        throw DatabaseError(ErrorCode::InvalidArgument, "Tuple index is out of range");
    }
    return values_[index];
}

std::vector<std::uint8_t> Tuple::serialize(const Schema& schema) const {
    if (values_.size() != schema.column_count()) {
        throw DatabaseError(ErrorCode::InvalidArgument, "Tuple and schema column counts differ");
    }
    if (values_.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw DatabaseError(ErrorCode::SerializationError, "Tuple has too many fields");
    }

    std::vector<std::uint8_t> output;
    append_integer<std::uint16_t>(output, static_cast<std::uint16_t>(values_.size()));
    for (std::size_t i = 0; i < values_.size(); ++i) {
        check_type(values_[i], schema.column(i).type);
        output.push_back(0);  // Reserved for a future NULL representation.
        switch (schema.column(i).type) {
            case TypeId::Int32: append_integer(output, std::get<std::int32_t>(values_[i])); break;
            case TypeId::Int64: append_integer(output, std::get<std::int64_t>(values_[i])); break;
            case TypeId::Boolean: output.push_back(std::get<bool>(values_[i]) ? 1 : 0); break;
            case TypeId::String32: {
                const auto& text = std::get<std::string>(values_[i]);
                if (text.size() > 32 || text.find('\0') != std::string::npos) {
                    throw DatabaseError(
                        ErrorCode::SerializationError,
                        "STRING32 value must contain at most 32 non-null bytes");
                }
                output.insert(output.end(), text.begin(), text.end());
                output.insert(output.end(), 32 - text.size(), 0);
                break;
            }
            case TypeId::Varchar: {
                const auto& text = std::get<std::string>(values_[i]);
                if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
                    throw DatabaseError(ErrorCode::SerializationError, "String is too long");
                }
                append_integer<std::uint32_t>(output, static_cast<std::uint32_t>(text.size()));
                output.insert(output.end(), text.begin(), text.end());
                break;
            }
        }
    }
    return output;
}

Tuple Tuple::deserialize(const Schema& schema, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const auto count = read_integer<std::uint16_t>(bytes, offset);
    if (count != schema.column_count()) {
        throw DatabaseError(ErrorCode::Corruption, "Serialized tuple field count differs from schema");
    }

    std::vector<Value> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (offset >= bytes.size()) {
            throw DatabaseError(ErrorCode::Corruption, "Tuple ended before null flag");
        }
        if (bytes[offset++] != 0) {
            throw DatabaseError(ErrorCode::Corruption, "NULL values are not supported yet");
        }
        switch (schema.column(i).type) {
            case TypeId::Int32: values.emplace_back(read_integer<std::int32_t>(bytes, offset)); break;
            case TypeId::Int64: values.emplace_back(read_integer<std::int64_t>(bytes, offset)); break;
            case TypeId::Boolean:
                if (offset >= bytes.size()) throw DatabaseError(ErrorCode::Corruption, "Tuple ended during boolean decoding");
                values.emplace_back(bytes[offset++] != 0);
                break;
            case TypeId::String32: {
                if (bytes.size() - offset < 32) {
                    throw DatabaseError(ErrorCode::Corruption, "Tuple ended during STRING32 decoding");
                }
                const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
                const auto end = begin + 32;
                const auto terminator = std::find(begin, end, static_cast<std::uint8_t>(0));
                values.emplace_back(
                    std::string(reinterpret_cast<const char*>(bytes.data() + offset),
                                static_cast<std::size_t>(std::distance(begin, terminator))));
                offset += 32;
                break;
            }
            case TypeId::Varchar: {
                const auto length = read_integer<std::uint32_t>(bytes, offset);
                if (bytes.size() - offset < length) throw DatabaseError(ErrorCode::Corruption, "Tuple ended during string decoding");
                values.emplace_back(std::string(
                    reinterpret_cast<const char*>(bytes.data() + offset), length));
                offset += length;
                break;
            }
        }
    }
    if (offset != bytes.size()) {
        throw DatabaseError(ErrorCode::Corruption, "Trailing bytes found after tuple");
    }
    return Tuple(std::move(values));
}

}  // namespace localdb
