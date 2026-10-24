#include "localdb/disk_manager.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace localdb {

DiskManager::DiskManager(const std::filesystem::path& path,
                         std::filesystem::path catalog_path)
    : path_(path),
      catalog_(catalog_path.empty() ? std::filesystem::path(path.string() + ".catalog")
                                    : std::move(catalog_path)) {
    open_or_create();
    validate_catalog_mappings();
}

DiskManager::~DiskManager() {
    try {
        close();
    } catch (...) {
        // Destructors must not throw. Explicit close() or sync() reports errors.
        if (file_.is_open()) {
            file_.close();
        }
    }
}

void DiskManager::ensure_open() const {
    if (closed_ || !file_.is_open()) {
        throw DatabaseError(ErrorCode::IoError, "Database is closed");
    }
}

void DiskManager::validate_catalog_mappings() const {
    const auto physical_count = physical_page_count();
    std::unordered_set<DiskPageId> mapped_pages;
    for (const auto& table_info : catalog_.tables()) {
        for (const auto physical_page_id : table_info.physical_pages) {
            if (physical_page_id >= physical_count ||
                !mapped_pages.insert(physical_page_id).second) {
                throw DatabaseError(
                    ErrorCode::Corruption,
                    "Catalog contains an invalid or duplicated physical page mapping");
            }
        }
    }
}

void DiskManager::open_or_create() {
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_) {
        file_.clear();
        std::ofstream create_file(path_, std::ios::binary);
        if (!create_file) {
            throw DatabaseError(ErrorCode::IoError, "Unable to create database file: " + path_.string());
        }
        create_file.close();
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError, "Unable to open database file: " + path_.string());
    }
}

std::streamoff DiskManager::offset_for(DiskPageId page_id) {
    return static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(kPageSize);
}

std::size_t DiskManager::physical_page_count() const {
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size % kPageSize != 0) {
        throw DatabaseError(ErrorCode::Corruption, "Database file has an invalid size");
    }
    return static_cast<std::size_t>(size / kPageSize);
}

DiskPageId DiskManager::allocate_physical_page() {
    ensure_open();
    const auto count = physical_page_count();
    if (count > std::numeric_limits<DiskPageId>::max()) {
        throw DatabaseError(ErrorCode::IoError, "Database has too many pages");
    }
    std::vector<std::uint8_t> empty_page(kPageSize, 0);
    write_physical_page(static_cast<DiskPageId>(count), empty_page);
    return static_cast<DiskPageId>(count);
}

TableId DiskManager::create_table(std::string name, Schema schema) {
    ensure_open();
    return catalog_.create_table(std::move(name), std::move(schema));
}

const TableInfo& DiskManager::open_table(TableId table_id) const {
    ensure_open();
    return catalog_.table(table_id);
}

const TableInfo& DiskManager::open_table(const std::string& name) const {
    ensure_open();
    return catalog_.table(name);
}

PageId DiskManager::allocate_page(TableId table_id) {
    ensure_open();
    catalog_.table(table_id);
    const DiskPageId physical_page_id = allocate_physical_page();
    // The zero-filled physical page is initialized before its logical ID is exposed.
    return catalog_.add_page(table_id, physical_page_id);
}

void DiskManager::read_physical_page(DiskPageId page_id,
                                     std::vector<std::uint8_t>& page) {
    ensure_open();
    if (page_id >= physical_page_count()) {
        throw DatabaseError(ErrorCode::PageNotFound,
                            "Requested physical page does not exist");
    }
    page.assign(kPageSize, 0);
    file_.clear();
    file_.seekg(offset_for(page_id));
    file_.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(kPageSize));
    if (!file_ || file_.gcount() != static_cast<std::streamsize>(kPageSize)) {
        throw DatabaseError(ErrorCode::IoError,
                            "Short read while reading database page");
    }
}

void DiskManager::write_physical_page(
    DiskPageId page_id, const std::vector<std::uint8_t>& page) {
    ensure_open();
    if (page.size() != kPageSize) {
        throw DatabaseError(ErrorCode::InvalidArgument, "A database page must be exactly 4096 bytes");
    }
    if (page_id > physical_page_count()) {
        throw DatabaseError(ErrorCode::PageNotFound, "Cannot write a page beyond the next page id");
    }
    file_.clear();
    file_.seekp(offset_for(page_id));
    file_.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError,
                            "Short write while writing database page");
    }
    file_.flush();
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError,
                            "Unable to flush database page");
    }

    std::error_code error;
    const auto file_size = std::filesystem::file_size(path_, error);
    const auto expected_size =
        (static_cast<std::uintmax_t>(std::max<std::size_t>(
             physical_page_count(), static_cast<std::size_t>(page_id) + 1))) *
        static_cast<std::uintmax_t>(kPageSize);
    if (error || file_size != expected_size) {
        throw DatabaseError(ErrorCode::IoError,
                            "Database file size does not match completed page write");
    }
}

void DiskManager::read_page(PageId page_id, std::vector<std::uint8_t>& page) {
    ensure_open();
    read_physical_page(catalog_.physical_page(page_id), page);
}

void DiskManager::write_page(PageId page_id, const std::vector<std::uint8_t>& page) {
    ensure_open();
    write_physical_page(catalog_.physical_page(page_id), page);
}

std::size_t DiskManager::page_count(TableId table_id) const {
    ensure_open();
    return catalog_.page_count(table_id);
}

void DiskManager::sync() {
    ensure_open();
    file_.flush();
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError, "Unable to flush database file");
    }
    catalog_.flush();
}

void DiskManager::flush() {
    sync();
}

void DiskManager::close() {
    if (closed_) {
        return;
    }
    if (file_.is_open()) {
        sync();
        file_.close();
    }
    closed_ = true;
}

}  // namespace localdb
