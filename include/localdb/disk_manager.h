#pragma once

#include "localdb/errors.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace localdb {

using PageId = std::uint32_t;
constexpr std::size_t kPageSize = 4096;

class DiskManager {
public:
    explicit DiskManager(const std::filesystem::path& path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    PageId allocate_page();
    void read_page(PageId page_id, std::vector<std::uint8_t>& page);
    void write_page(PageId page_id, const std::vector<std::uint8_t>& page);
    void flush();

    std::size_t page_count() const;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    void open_or_create();
    static std::streamoff offset_for(PageId page_id);

    std::filesystem::path path_;
    std::fstream file_;
};

}  // namespace localdb
