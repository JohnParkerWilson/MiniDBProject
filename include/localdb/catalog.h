#pragma once

#include "localdb/identifiers.h"
#include "localdb/schema.h"

#include <filesystem>
#include <string>
#include <vector>

namespace localdb {

struct TableInfo {
    TableId id;
    std::string name;
    Schema schema;
    std::vector<DiskPageId> physical_pages;
};

// Persistent table catalog. The catalog file stores stable table IDs, table
// names, schemas, and the next table ID to allocate.
class Catalog {
public:
    explicit Catalog(std::filesystem::path path);

    TableId create_table(std::string name, Schema schema);
    const TableInfo& table(TableId table_id) const;
    const TableInfo& table(const std::string& name) const;
    PageId add_page(TableId table_id, DiskPageId physical_page_id);
    DiskPageId physical_page(PageId page_id) const;
    std::size_t page_count(TableId table_id) const;
    const std::vector<TableInfo>& tables() const noexcept { return tables_; }
    std::size_t table_count() const noexcept { return tables_.size(); }

    void flush() const;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    void load();

    std::filesystem::path path_;
    TableId next_table_id_ = 1;
    std::vector<TableInfo> tables_;
};

}  // namespace localdb
