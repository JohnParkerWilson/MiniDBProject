#pragma once

#include "localdb/errors.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace localdb {

enum class TypeId : std::uint8_t {
    Int32,
    Int64,
    Boolean,
    String32,
    Varchar,
};

struct Column {
    std::string name;
    TypeId type;
    bool nullable = false;

    Column(std::string column_name, TypeId column_type, bool column_nullable = false)
        : name(std::move(column_name)), type(column_type), nullable(column_nullable) {}
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns);

    std::size_t column_count() const noexcept { return columns_.size(); }
    const Column& column(std::size_t index) const;
    const std::vector<Column>& columns() const noexcept { return columns_; }

private:
    std::vector<Column> columns_;
};

const char* type_name(TypeId type) noexcept;

}  // namespace localdb
