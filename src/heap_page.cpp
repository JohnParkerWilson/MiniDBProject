#include "localdb/heap_page.h"

#include <algorithm>
#include <limits>

namespace localdb {
namespace {

constexpr std::uint8_t kHeapPageMagic[] = {'H', 'P', 'G', '1'};

}  // namespace

HeapPage::HeapPage(PageId page_id, std::size_t tuple_size)
    : page_id_(page_id),
      tuple_size_(tuple_size),
      slot_count_(calculate_slot_count(tuple_size)) {
    initialize_new_page();
}

HeapPage::HeapPage(PageId page_id, std::size_t tuple_size, Buffer buffer,
                   std::size_t slot_count)
    : page_id_(page_id),
      tuple_size_(tuple_size),
      slot_count_(slot_count),
      buffer_(std::move(buffer)) {}

std::size_t HeapPage::bitmap_bytes(std::size_t slot_count) {
    return (slot_count + 7) / 8;
}

std::size_t HeapPage::calculate_slot_count(std::size_t tuple_size) {
    if (tuple_size == 0 || tuple_size > std::numeric_limits<std::uint16_t>::max()) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page tuple size must fit in a nonzero uint16_t");
    }

    std::size_t slots = (kPageSize - kHeaderSize) / tuple_size;
    while (slots > 0 && kHeaderSize + bitmap_bytes(slots) + slots * tuple_size > kPageSize) {
        --slots;
    }
    if (slots == 0 || slots > std::numeric_limits<std::uint16_t>::max()) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Tuple size leaves no usable heap-page slots");
    }
    return slots;
}

void HeapPage::write_u16(Buffer& buffer, std::size_t offset, std::uint16_t value) {
    buffer[offset] = static_cast<std::uint8_t>(value & 0xff);
    buffer[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

std::uint16_t HeapPage::read_u16(const Buffer& buffer, std::size_t offset) {
    return static_cast<std::uint16_t>(buffer[offset]) |
           (static_cast<std::uint16_t>(buffer[offset + 1]) << 8);
}

void HeapPage::initialize_new_page() {
    buffer_.fill(0);
    std::copy(std::begin(kHeapPageMagic), std::end(kHeapPageMagic), buffer_.begin());
    write_u16(buffer_, 4, static_cast<std::uint16_t>(tuple_size_));
    write_u16(buffer_, 6, static_cast<std::uint16_t>(slot_count_));
}

HeapPage HeapPage::from_buffer(PageId page_id, std::size_t tuple_size,
                               const Buffer& buffer) {
    const auto expected_slots = calculate_slot_count(tuple_size);
    if (!std::equal(std::begin(kHeapPageMagic), std::end(kHeapPageMagic), buffer.begin())) {
        throw DatabaseError(ErrorCode::Corruption, "Heap page has an invalid magic value");
    }
    if (read_u16(buffer, 4) != tuple_size || read_u16(buffer, 6) != expected_slots) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page metadata does not match its tuple layout");
    }

    const auto used_bitmap_bytes = bitmap_bytes(expected_slots);
    const auto unused_bits = expected_slots % 8;
    if (unused_bits != 0) {
        const auto padding_mask = static_cast<std::uint8_t>(0xffu << unused_bits);
        if ((buffer[kHeaderSize + used_bitmap_bytes - 1] & padding_mask) != 0) {
            throw DatabaseError(ErrorCode::Corruption,
                                "Heap page occupancy bitmap contains set padding bits");
        }
    }

    return HeapPage(page_id, tuple_size, buffer, expected_slots);
}

void HeapPage::validate_rid(const RID& rid) const {
    if (rid.page_id != page_id_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "RID belongs to a different heap page");
    }
    if (rid.slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "RID slot is outside the heap page");
    }
}

void HeapPage::validate_tuple(const std::vector<std::uint8_t>& tuple) const {
    if (tuple.size() != tuple_size_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Tuple size does not match heap-page tuple size");
    }
}

std::size_t HeapPage::bitmap_offset(SlotId slot_id) const {
    return kHeaderSize + slot_id / 8;
}

std::uint8_t HeapPage::bitmap_mask(SlotId slot_id) const {
    return static_cast<std::uint8_t>(1u << (slot_id % 8));
}

std::size_t HeapPage::tuple_offset(SlotId slot_id) const {
    return kHeaderSize + bitmap_bytes(slot_count_) +
           static_cast<std::size_t>(slot_id) * tuple_size_;
}

bool HeapPage::occupied(SlotId slot_id) const {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    return (buffer_[bitmap_offset(slot_id)] & bitmap_mask(slot_id)) != 0;
}

std::size_t HeapPage::free_slot_count() const noexcept {
    std::size_t free_slots = 0;
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
        const auto slot_id = static_cast<SlotId>(slot);
        if ((buffer_[bitmap_offset(slot_id)] & bitmap_mask(slot_id)) == 0) {
            ++free_slots;
        }
    }
    return free_slots;
}

std::optional<RID> HeapPage::insert(const std::vector<std::uint8_t>& tuple) {
    validate_tuple(tuple);
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
        const auto slot_id = static_cast<SlotId>(slot);
        if (!occupied(slot_id)) {
            std::copy(tuple.begin(), tuple.end(), buffer_.begin() + tuple_offset(slot_id));
            buffer_[bitmap_offset(slot_id)] |= bitmap_mask(slot_id);
            return RID{page_id_, slot_id};
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> HeapPage::get(const RID& rid) const {
    validate_rid(rid);
    if (!occupied(rid.slot_id)) {
        return std::nullopt;
    }
    const auto begin = buffer_.begin() + tuple_offset(rid.slot_id);
    return std::vector<std::uint8_t>(begin, begin + tuple_size_);
}

bool HeapPage::update(const RID& rid, const std::vector<std::uint8_t>& tuple) {
    validate_rid(rid);
    validate_tuple(tuple);
    if (!occupied(rid.slot_id)) {
        return false;
    }
    std::copy(tuple.begin(), tuple.end(), buffer_.begin() + tuple_offset(rid.slot_id));
    return true;
}

bool HeapPage::erase(const RID& rid) {
    validate_rid(rid);
    if (!occupied(rid.slot_id)) {
        return false;
    }
    buffer_[bitmap_offset(rid.slot_id)] &= static_cast<std::uint8_t>(~bitmap_mask(rid.slot_id));
    std::fill(buffer_.begin() + tuple_offset(rid.slot_id),
              buffer_.begin() + tuple_offset(rid.slot_id) + tuple_size_, 0);
    return true;
}

}  // namespace localdb
