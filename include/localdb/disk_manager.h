#pragma once

#include "localdb/catalog.h"
#include "localdb/errors.h"
#include "localdb/identifiers.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace localdb {

constexpr std::size_t kPageSize = 4096;

class DiskManager {
public:
    explicit DiskManager(const std::filesystem::path& path,
                         std::filesystem::path catalog_path = {});
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    TableId create_table(std::string name, Schema schema);
    const TableInfo& open_table(TableId table_id) const;
    const TableInfo& open_table(const std::string& name) const;

    PageId allocate_page(TableId table_id);
    void read_page(PageId page_id, std::vector<std::uint8_t>& page);
    void write_page(PageId page_id, const std::vector<std::uint8_t>& page);
    void sync();
    void flush();
    void close();

    std::size_t physical_page_count() const;
    std::size_t page_count(TableId table_id) const;
    const Catalog& catalog() const noexcept { return catalog_; }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    void open_or_create();
    static std::streamoff offset_for(DiskPageId page_id);
    void ensure_open() const;
    DiskPageId allocate_physical_page();
    void read_physical_page(DiskPageId page_id, std::vector<std::uint8_t>& page);
    void write_physical_page(DiskPageId page_id, const std::vector<std::uint8_t>& page);

    std::filesystem::path path_;
    std::fstream file_;
    Catalog catalog_;
    bool closed_ = false;
};

}  // namespace localdb
