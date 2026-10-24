#include "localdb/schema.h"

#include <utility>

namespace localdb {

Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        const auto& column = columns_[i];
        if (column.name.empty()) {
            throw DatabaseError(ErrorCode::InvalidSchema, "Column name cannot be empty");
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (columns_[previous].name == column.name) {
                throw DatabaseError(ErrorCode::InvalidSchema,
                                    "Column names must be unique");
            }
        }
    }
}

const Column& Schema::column(std::size_t index) const {
    if (index >= columns_.size()) {
        throw DatabaseError(ErrorCode::InvalidArgument, "Column index is out of range");
    }
    return columns_[index];
}

const char* type_name(TypeId type) noexcept {
    switch (type) {
        case TypeId::Int32: return "INT32";
        case TypeId::Int64: return "INT64";
        case TypeId::Boolean: return "BOOLEAN";
        case TypeId::String32: return "STRING32";
        case TypeId::Varchar: return "VARCHAR";
    }
    return "UNKNOWN";
}

}  // namespace localdb
