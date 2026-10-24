#pragma once

#include "localdb/disk_manager.h"
#include "localdb/identifiers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace localdb {

class HeapPage {
public:
    using Buffer = std::array<std::uint8_t, kPageSize>;

    // Creates a new empty page for fixed-size serialized tuples.
    // tuple_size must be positive and small enough for at least one slot.
    HeapPage(PageId page_id, std::size_t tuple_size);

    // Reconstructs a page from a buffer previously produced by buffer().
    static HeapPage from_buffer(PageId page_id,
                                std::size_t tuple_size,
                                const Buffer& buffer);

    PageId page_id() const noexcept { return page_id_; }
    std::size_t tuple_size() const noexcept { return tuple_size_; }
    std::size_t slot_count() const noexcept { return slot_count_; }
    std::size_t free_slot_count() const noexcept;

    const Buffer& buffer() const noexcept { return buffer_; }

    // Returns a new RID, or nullopt if the page is full. The tuple must have
    // exactly tuple_size() bytes.
    std::optional<RID> insert(const std::vector<std::uint8_t>& tuple);

    // Returns nullopt when the slot is valid but currently empty.
    std::optional<std::vector<std::uint8_t>> get(const RID& rid) const;

    // Returns false when the slot is valid but empty.
    bool update(const RID& rid, const std::vector<std::uint8_t>& tuple);
    bool erase(const RID& rid);

    bool occupied(SlotId slot_id) const;

private:
    static constexpr std::size_t kHeaderSize = 8;

    HeapPage(PageId page_id, std::size_t tuple_size, Buffer buffer,
             std::size_t slot_count);

    static std::size_t calculate_slot_count(std::size_t tuple_size);
    static std::size_t bitmap_bytes(std::size_t slot_count);
    static void write_u16(Buffer& buffer, std::size_t offset, std::uint16_t value);
    static std::uint16_t read_u16(const Buffer& buffer, std::size_t offset);

    void initialize_new_page();
    void validate_rid(const RID& rid) const;
    void validate_tuple(const std::vector<std::uint8_t>& tuple) const;
    std::size_t bitmap_offset(SlotId slot_id) const;
    std::uint8_t bitmap_mask(SlotId slot_id) const;
    std::size_t tuple_offset(SlotId slot_id) const;

    PageId page_id_;
    std::size_t tuple_size_;
    std::size_t slot_count_;
    Buffer buffer_{};
};

}  // namespace localdb
