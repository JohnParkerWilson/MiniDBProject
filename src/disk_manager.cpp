#include "localdb/disk_manager.h"

#include <algorithm>
#include <limits>

namespace localdb {

DiskManager::DiskManager(const std::filesystem::path& path) : path_(path) {
    open_or_create();
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
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

std::streamoff DiskManager::offset_for(PageId page_id) {
    return static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(kPageSize);
}

std::size_t DiskManager::page_count() const {
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size % kPageSize != 0) {
        throw DatabaseError(ErrorCode::Corruption, "Database file has an invalid size");
    }
    return static_cast<std::size_t>(size / kPageSize);
}

PageId DiskManager::allocate_page() {
    const auto count = page_count();
    if (count > std::numeric_limits<PageId>::max()) {
        throw DatabaseError(ErrorCode::IoError, "Database has too many pages");
    }
    std::vector<std::uint8_t> empty_page(kPageSize, 0);
    write_page(static_cast<PageId>(count), empty_page);
    return static_cast<PageId>(count);
}

void DiskManager::read_page(PageId page_id, std::vector<std::uint8_t>& page) {
    if (page_id >= page_count()) {
        throw DatabaseError(ErrorCode::PageNotFound, "Requested page does not exist");
    }
    page.assign(kPageSize, 0);
    file_.clear();
    file_.seekg(offset_for(page_id));
    file_.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(kPageSize));
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError, "Unable to read database page");
    }
}

void DiskManager::write_page(PageId page_id, const std::vector<std::uint8_t>& page) {
    if (page.size() != kPageSize) {
        throw DatabaseError(ErrorCode::InvalidArgument, "A database page must be exactly 4096 bytes");
    }
    if (page_id > page_count()) {
        throw DatabaseError(ErrorCode::PageNotFound, "Cannot write a page beyond the next page id");
    }
    file_.clear();
    file_.seekp(offset_for(page_id));
    file_.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError, "Unable to write database page");
    }
    file_.flush();
}

void DiskManager::flush() {
    file_.flush();
    if (!file_) {
        throw DatabaseError(ErrorCode::IoError, "Unable to flush database file");
    }
}

}  // namespace localdb
