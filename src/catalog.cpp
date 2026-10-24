#include "localdb/catalog.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_set>

namespace localdb {
namespace {

constexpr char kMagic[] = "LDBCAT01";
constexpr std::uint32_t kFormatVersion = 2;

void append_u8(std::vector<std::uint8_t>& output, std::uint8_t value) {
    output.push_back(value);
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
}

void append_string(std::vector<std::uint8_t>& output, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw DatabaseError(ErrorCode::SerializationError, "Catalog string is too long");
    }
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

std::uint8_t read_u8(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    if (offset >= input.size()) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog ended during byte decoding");
    }
    return input[offset++];
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    if (input.size() - offset < sizeof(std::uint32_t)) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog ended during integer decoding");
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<std::uint32_t>(input[offset++]) << (i * 8);
    }
    return value;
}

std::string read_string(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    const auto length = read_u32(input, offset);
    if (input.size() - offset < length) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog ended during string decoding");
    }
    std::string value(reinterpret_cast<const char*>(input.data() + offset), length);
    offset += length;
    return value;
}

bool valid_type(std::uint8_t raw_type) {
    return raw_type <= static_cast<std::uint8_t>(TypeId::Varchar);
}

}  // namespace

Catalog::Catalog(std::filesystem::path path) : path_(std::move(path)) {
    if (std::filesystem::exists(path_)) {
        load();
    }
}

TableId Catalog::create_table(std::string name, Schema schema) {
    if (name.empty()) {
        throw DatabaseError(ErrorCode::InvalidArgument, "Table name cannot be empty");
    }
    for (const auto& existing : tables_) {
        if (existing.name == name) {
            throw DatabaseError(ErrorCode::InvalidArgument, "Table name already exists");
        }
    }
    if (next_table_id_ == 0) {
        throw DatabaseError(ErrorCode::IoError, "No table IDs remain");
    }

    const TableId id = next_table_id_++;
    tables_.push_back(TableInfo{id, std::move(name), std::move(schema), {}});
    flush();
    return id;
}

const TableInfo& Catalog::table(TableId table_id) const {
    for (const auto& table_info : tables_) {
        if (table_info.id == table_id) {
            return table_info;
        }
    }
    throw DatabaseError(ErrorCode::InvalidArgument, "Table ID does not exist");
}

const TableInfo& Catalog::table(const std::string& name) const {
    for (const auto& table_info : tables_) {
        if (table_info.name == name) {
            return table_info;
        }
    }
    throw DatabaseError(ErrorCode::InvalidArgument, "Table name does not exist");
}

PageId Catalog::add_page(TableId table_id, DiskPageId physical_page_id) {
    for (auto& table_info : tables_) {
        if (table_info.id == table_id) {
            if (table_info.physical_pages.size() > std::numeric_limits<PageNumber>::max()) {
                throw DatabaseError(ErrorCode::IoError, "Table has too many pages");
            }
            table_info.physical_pages.push_back(physical_page_id);
            const PageId page_id{
                table_id,
                static_cast<PageNumber>(table_info.physical_pages.size() - 1)};
            flush();
            return page_id;
        }
    }
    throw DatabaseError(ErrorCode::InvalidArgument, "Table ID does not exist");
}

DiskPageId Catalog::physical_page(PageId page_id) const {
    const auto& table_info = table(page_id.table_id);
    if (page_id.page_number >= table_info.physical_pages.size()) {
        throw DatabaseError(ErrorCode::PageNotFound, "Logical page does not exist");
    }
    return table_info.physical_pages[page_id.page_number];
}

std::size_t Catalog::page_count(TableId table_id) const {
    return table(table_id).physical_pages.size();
}

void Catalog::flush() const {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kMagic, kMagic + sizeof(kMagic) - 1);
    append_u32(bytes, kFormatVersion);
    append_u32(bytes, next_table_id_);
    append_u32(bytes, static_cast<std::uint32_t>(tables_.size()));

    for (const auto& table_info : tables_) {
        append_u32(bytes, table_info.id);
        append_string(bytes, table_info.name);
        append_u32(bytes, static_cast<std::uint32_t>(table_info.schema.column_count()));
        for (const auto& column : table_info.schema.columns()) {
            append_string(bytes, column.name);
            append_u8(bytes, static_cast<std::uint8_t>(column.type));
            append_u8(bytes, column.nullable ? 1 : 0);
        }
        append_u32(bytes, static_cast<std::uint32_t>(table_info.physical_pages.size()));
        for (const auto physical_page_id : table_info.physical_pages) {
            append_u32(bytes, physical_page_id);
        }
    }

    const auto temporary_path = path_.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw DatabaseError(ErrorCode::IoError, "Unable to write catalog: " + path_.string());
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            throw DatabaseError(ErrorCode::IoError, "Unable to flush catalog: " + path_.string());
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, path_, error);
    if (error) {
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary_path, path_, error);
    }
    if (error) {
        throw DatabaseError(ErrorCode::IoError, "Unable to replace catalog: " + path_.string());
    }
}

void Catalog::load() {
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw DatabaseError(ErrorCode::IoError, "Unable to open catalog: " + path_.string());
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::size_t magic_size = sizeof(kMagic) - 1;
    if (bytes.size() < magic_size ||
        std::memcmp(bytes.data(), kMagic, magic_size) != 0) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog has an invalid magic value");
    }

    std::size_t offset = magic_size;
    const auto format_version = read_u32(bytes, offset);
    if (format_version != 1 && format_version != kFormatVersion) {
        throw DatabaseError(ErrorCode::Corruption, "Unsupported catalog format version");
    }
    next_table_id_ = read_u32(bytes, offset);
    const auto table_count = read_u32(bytes, offset);
    if (next_table_id_ == 0 || table_count > bytes.size()) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog contains invalid table metadata");
    }
    tables_.clear();
    tables_.reserve(table_count);

    std::unordered_set<TableId> table_ids;
    std::unordered_set<std::string> table_names;
    TableId largest_table_id = 0;

    for (std::uint32_t i = 0; i < table_count; ++i) {
        const TableId id = read_u32(bytes, offset);
        const std::string name = read_string(bytes, offset);
        if (id == 0 || !table_ids.insert(id).second ||
            name.empty() || !table_names.insert(name).second) {
            throw DatabaseError(ErrorCode::Corruption,
                                "Catalog contains duplicate or invalid table metadata");
        }
        largest_table_id = std::max(largest_table_id, id);
        const auto column_count = read_u32(bytes, offset);
        std::vector<Column> columns;
        columns.reserve(column_count);
        for (std::uint32_t column_index = 0; column_index < column_count; ++column_index) {
            const std::string column_name = read_string(bytes, offset);
            const auto raw_type = read_u8(bytes, offset);
            const auto raw_nullable = read_u8(bytes, offset);
            if (!valid_type(raw_type) || raw_nullable > 1) {
                throw DatabaseError(ErrorCode::Corruption, "Catalog contains an invalid column");
            }
            columns.emplace_back(column_name, static_cast<TypeId>(raw_type), raw_nullable != 0);
        }
        std::vector<DiskPageId> physical_pages;
        if (format_version >= 2) {
            const auto page_count = read_u32(bytes, offset);
            physical_pages.reserve(page_count);
            for (std::uint32_t page_index = 0; page_index < page_count; ++page_index) {
                physical_pages.push_back(read_u32(bytes, offset));
            }
        }
        tables_.push_back(TableInfo{
            id, name, Schema(std::move(columns)), std::move(physical_pages)});
    }

    if (next_table_id_ <= largest_table_id) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Catalog next table ID is not greater than existing IDs");
    }

    if (offset != bytes.size()) {
        throw DatabaseError(ErrorCode::Corruption, "Catalog contains trailing bytes");
    }
}

}  // namespace localdb
