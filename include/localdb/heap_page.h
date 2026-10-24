#pragma once

#include "localdb/disk_manager.h"
#include "localdb/identifiers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace localdb {

class HeapPage {
public:
    using Buffer = std::vector<std::uint8_t>;

    static constexpr std::uint16_t kFormatVersion = 1;

    // Initialize validates all arguments before modifying buffer.
    static void Initialize(Buffer& buffer, std::size_t tuple_width);

    // Open never modifies buffer. It validates the complete persistent page
    // representation before constructing the in-memory view.
    static HeapPage Open(PageId page_id, const Buffer& buffer,
                         std::size_t expected_tuple_width);

    // Creates a new empty page for fixed-size serialized tuples. The layout is
    // header, allocation bitmap, deletion bitmap, tuple slots, then padding.
    // Tuple widths must be positive multiples of eight and must leave room for
    // at least one complete tuple and both bitmap regions.
    // tuple_size must be positive and small enough for at least one slot.
    HeapPage(PageId page_id, std::size_t tuple_size);

    // Reconstructs an owned page from a buffer previously produced by
    // buffer(). The input buffer is copied; it is not borrowed.
    static HeapPage from_buffer(PageId page_id, std::size_t tuple_size,
                                const Buffer& buffer) {
        return Open(page_id, buffer, tuple_size);
    }

    PageId page_id() const noexcept { return page_id_; }
    std::size_t tuple_width() const noexcept { return tuple_size_; }
    std::size_t slot_capacity() const noexcept { return slot_count_; }
    std::size_t allocated_slot_count() const noexcept { return allocated_count_; }

    // Compatibility names retained for the earlier API.
    std::size_t tuple_size() const noexcept { return tuple_size_; }
    std::size_t slot_count() const noexcept { return slot_count_; }
    std::size_t free_slot_count() const noexcept;

    // The returned reference remains valid while this HeapPage object remains
    // alive. Disk I/O is the caller's responsibility.
    const Buffer& buffer() const noexcept { return buffer_; }

    // Returns a new RID, or nullopt if the page is full. The tuple must have
    // exactly tuple_size() bytes.
    std::optional<RID> insert(const std::vector<std::uint8_t>& tuple);
    std::optional<SlotId> find_free_slot() const;

    // Returns an independently owned tuple copy. A missing/deleted slot throws
    // RecordNotFound.
    std::optional<std::vector<std::uint8_t>> get(const RID& rid) const;

    // Copies exactly tuple_width() bytes into the beginning of output. The
    // output vector must already have room for the complete tuple.
    void read(SlotId slot_id, std::vector<std::uint8_t>& output) const;

    // Returns false when the slot is valid but empty.
    bool update(const RID& rid, const std::vector<std::uint8_t>& tuple);
    bool erase(const RID& rid);

    bool occupied(SlotId slot_id) const;
    bool is_allocated(SlotId slot_id) const;
    bool is_deleted(SlotId slot_id) const;

    // Compatibility names retained for the earlier API.
    bool allocated(SlotId slot_id) const { return is_allocated(slot_id); }
    bool deleted(SlotId slot_id) const { return is_deleted(slot_id); }

private:
    // The first eight bytes are reserved for a future log sequence number.
    static constexpr std::size_t kHeaderSize = 24;

    HeapPage(PageId page_id, std::size_t tuple_size, Buffer buffer,
             std::size_t slot_count, std::size_t allocated_count);

    static std::size_t bitmap_bytes(std::size_t slot_count);
    static void write_u16(Buffer& buffer, std::size_t offset, std::uint16_t value);
    static std::uint16_t read_u16(const Buffer& buffer, std::size_t offset);
    static std::size_t calculate_slot_count(std::size_t tuple_width);

    static void validate_tuple_width(std::size_t tuple_width);
    void set_allocation_bit(SlotId slot_id, bool value);
    void set_deletion_bit(SlotId slot_id, bool value);
    void set_allocated_count(std::size_t count);
    void validate_rid(const RID& rid) const;
    void validate_tuple(const std::vector<std::uint8_t>& tuple) const;
    std::size_t bitmap_offset(bool deletion_bitmap, SlotId slot_id) const;
    std::uint8_t bitmap_mask(SlotId slot_id) const;
    std::size_t tuple_offset(SlotId slot_id) const;

    PageId page_id_;
    std::size_t tuple_size_;
    std::size_t slot_count_;
    std::size_t allocated_count_;
    Buffer buffer_{};
};

}  // namespace localdb
