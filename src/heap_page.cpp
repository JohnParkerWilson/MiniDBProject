#include "localdb/heap_page.h"

#include <algorithm>
#include <limits>

namespace localdb {
namespace {

constexpr std::uint8_t kHeapPageMagic[] = {'H', 'P', 'G', '1'};
constexpr std::size_t kMagicOffset = 8;
constexpr std::size_t kVersionOffset = 12;
constexpr std::size_t kTupleWidthOffset = 14;
constexpr std::size_t kSlotCountOffset = 16;
constexpr std::size_t kAllocatedCountOffset = 18;
constexpr std::size_t kReservedOffset = 20;

}  // namespace

HeapPage::HeapPage(PageId page_id, std::size_t tuple_size)
    : page_id_(page_id),
      tuple_size_(tuple_size),
      slot_count_(calculate_slot_count(tuple_size)),
      allocated_count_(0),
      buffer_(kPageSize, 0) {
    Initialize(buffer_, tuple_size_);
}

HeapPage::HeapPage(PageId page_id, std::size_t tuple_size, Buffer buffer,
                   std::size_t slot_count, std::size_t allocated_count)
    : page_id_(page_id),
      tuple_size_(tuple_size),
      slot_count_(slot_count),
      allocated_count_(allocated_count),
      buffer_(std::move(buffer)) {}

void HeapPage::validate_tuple_width(std::size_t tuple_width) {
    if (tuple_width == 0 || tuple_width % 8 != 0 ||
        tuple_width > std::numeric_limits<std::uint16_t>::max()) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page tuple width must be a positive multiple of eight");
    }
}

void HeapPage::set_allocated_count(std::size_t count) {
    if (count > slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Allocated-slot count exceeds slot capacity");
    }
    std::size_t actual_count = 0;
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
        if (is_allocated(static_cast<SlotId>(slot))) {
            ++actual_count;
        }
    }
    if (actual_count != count) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Allocated-slot count does not match allocation bitmap");
    }
    write_u16(buffer_, kAllocatedCountOffset,
              static_cast<std::uint16_t>(count));
    allocated_count_ = count;
}

void HeapPage::set_allocation_bit(SlotId slot_id, bool value) {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    const bool previous = is_allocated(slot_id);
    if (previous == value) {
        return;
    }
    if (!value) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Allocated heap-page slots cannot be cleared");
    }

    buffer_[bitmap_offset(false, slot_id)] |= bitmap_mask(slot_id);
    set_allocated_count(allocated_count_ + 1);
}

void HeapPage::set_deletion_bit(SlotId slot_id, bool value) {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    if (value && !is_allocated(slot_id)) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "An unallocated heap-page slot cannot be deleted");
    }

    const std::uint8_t mask = bitmap_mask(slot_id);
    auto& byte = buffer_[bitmap_offset(true, slot_id)];
    if (value) {
        byte |= mask;
    } else {
        byte &= static_cast<std::uint8_t>(~mask);
    }
}

std::size_t HeapPage::bitmap_bytes(std::size_t slot_count) {
    return ((slot_count + 63) / 64) * sizeof(std::uint64_t);
}

std::size_t HeapPage::calculate_slot_count(std::size_t tuple_width) {
    validate_tuple_width(tuple_width);

    std::size_t slots = (kPageSize - kHeaderSize) / tuple_width;
    while (slots > 0 &&
           kHeaderSize + 2 * bitmap_bytes(slots) + slots * tuple_width > kPageSize) {
        --slots;
    }
    if (slots == 0 || slots > std::numeric_limits<std::uint16_t>::max()) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Tuple width leaves no usable heap-page slots");
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

void HeapPage::Initialize(Buffer& buffer, std::size_t tuple_width) {
    // Validate every argument before changing buffer.
    validate_tuple_width(tuple_width);
    const std::size_t slot_count = calculate_slot_count(tuple_width);
    if (buffer.size() != kPageSize) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page buffer must be exactly 4096 bytes");
    }

    buffer.assign(kPageSize, 0);
    // Bytes 0-7 are the reserved LSN area and remain zero for this project.
    std::copy(std::begin(kHeapPageMagic), std::end(kHeapPageMagic),
              buffer.begin() + kMagicOffset);
    write_u16(buffer, kVersionOffset, kFormatVersion);
    write_u16(buffer, kTupleWidthOffset, static_cast<std::uint16_t>(tuple_width));
    write_u16(buffer, kSlotCountOffset, static_cast<std::uint16_t>(slot_count));
    write_u16(buffer, kAllocatedCountOffset, 0);
    write_u16(buffer, kReservedOffset, 0);
    write_u16(buffer, kReservedOffset + 2, 0);
}

HeapPage HeapPage::Open(PageId page_id, const Buffer& buffer,
                        std::size_t expected_tuple_width) {
    validate_tuple_width(expected_tuple_width);
    if (buffer.size() != kPageSize) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page buffer must be exactly 4096 bytes");
    }
    if (!std::equal(std::begin(kHeapPageMagic), std::end(kHeapPageMagic),
                    buffer.begin() + kMagicOffset)) {
        throw DatabaseError(ErrorCode::Corruption, "Heap page has an invalid magic value");
    }
    if (read_u16(buffer, kVersionOffset) != kFormatVersion) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page has an unsupported format version");
    }

    const std::size_t expected_slots = calculate_slot_count(expected_tuple_width);
    const std::size_t stored_tuple_width = read_u16(buffer, kTupleWidthOffset);
    const std::size_t stored_slots = read_u16(buffer, kSlotCountOffset);
    const std::size_t stored_allocated = read_u16(buffer, kAllocatedCountOffset);
    if (stored_tuple_width != expected_tuple_width || stored_slots != expected_slots) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page metadata does not match the expected layout");
    }
    if (read_u16(buffer, kReservedOffset) != 0 ||
        read_u16(buffer, kReservedOffset + 2) != 0) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page contains nonzero reserved metadata");
    }

    const std::size_t allocation_base = kHeaderSize;
    const std::size_t deletion_base = kHeaderSize + bitmap_bytes(expected_slots);
    const std::size_t tuple_base = deletion_base + bitmap_bytes(expected_slots);
    const std::size_t tuple_region_end = tuple_base + expected_slots * expected_tuple_width;
    if (tuple_base > kPageSize || tuple_region_end > kPageSize) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page regions exceed the page buffer");
    }

    std::size_t allocated_count = 0;
    for (std::size_t slot = 0; slot < expected_slots; ++slot) {
        const std::uint8_t mask = static_cast<std::uint8_t>(1u << (slot % 8));
        const bool allocated = (buffer[allocation_base + slot / 8] & mask) != 0;
        const bool deleted = (buffer[deletion_base + slot / 8] & mask) != 0;
        if (allocated) {
            ++allocated_count;
        }
        if (deleted && !allocated) {
            throw DatabaseError(ErrorCode::Corruption,
                                "Heap page marks an unallocated slot as deleted");
        }
    }

    const std::size_t bitmap_word_count = (expected_slots + 63) / 64;
    for (const std::size_t bitmap_base : {allocation_base, deletion_base}) {
        for (std::size_t bit = expected_slots; bit < bitmap_word_count * 64; ++bit) {
            if ((buffer[bitmap_base + bit / 8] &
                 static_cast<std::uint8_t>(1u << (bit % 8))) != 0) {
                throw DatabaseError(ErrorCode::Corruption,
                                    "Heap page bitmap contains set padding bits");
            }
        }
    }

    if (stored_allocated != allocated_count) {
        throw DatabaseError(ErrorCode::Corruption,
                            "Heap page allocated-slot count is inconsistent");
    }

    return HeapPage(page_id, expected_tuple_width, buffer, expected_slots,
                    allocated_count);
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
                            "Tuple size does not match heap-page tuple width");
    }
}

std::size_t HeapPage::bitmap_offset(bool deletion_bitmap, SlotId slot_id) const {
    return kHeaderSize + (deletion_bitmap ? bitmap_bytes(slot_count_) : 0) + slot_id / 8;
}

std::uint8_t HeapPage::bitmap_mask(SlotId slot_id) const {
    return static_cast<std::uint8_t>(1u << (slot_id % 8));
}

std::size_t HeapPage::tuple_offset(SlotId slot_id) const {
    return kHeaderSize + 2 * bitmap_bytes(slot_count_) +
           static_cast<std::size_t>(slot_id) * tuple_size_;
}

bool HeapPage::is_allocated(SlotId slot_id) const {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    return (buffer_[bitmap_offset(false, slot_id)] & bitmap_mask(slot_id)) != 0;
}

bool HeapPage::is_deleted(SlotId slot_id) const {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    return (buffer_[bitmap_offset(true, slot_id)] & bitmap_mask(slot_id)) != 0;
}

bool HeapPage::occupied(SlotId slot_id) const {
    return is_allocated(slot_id) && !is_deleted(slot_id);
}

std::size_t HeapPage::free_slot_count() const noexcept {
    std::size_t free_slots = 0;
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
        const auto slot_id = static_cast<SlotId>(slot);
        if (!is_allocated(slot_id)) {
            ++free_slots;
        }
    }
    return free_slots;
}

std::optional<RID> HeapPage::insert(const std::vector<std::uint8_t>& tuple) {
    validate_tuple(tuple);
    const auto free_slot = find_free_slot();
    if (!free_slot) {
        return std::nullopt;
    }

    const SlotId slot_id = *free_slot;
    std::copy(tuple.begin(), tuple.end(), buffer_.begin() + tuple_offset(slot_id));
    set_allocation_bit(slot_id, true);
    set_deletion_bit(slot_id, false);
    return RID{page_id_, slot_id};
}

std::optional<SlotId> HeapPage::find_free_slot() const {
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
        const auto slot_id = static_cast<SlotId>(slot);
        if (!is_allocated(slot_id)) {
            return slot_id;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> HeapPage::get(const RID& rid) const {
    validate_rid(rid);
    if (!occupied(rid.slot_id)) {
        throw DatabaseError(ErrorCode::RecordNotFound,
                            "Heap-page slot is free or logically deleted");
    }
    std::vector<std::uint8_t> result(tuple_size_);
    read(rid.slot_id, result);
    return result;
}

void HeapPage::read(SlotId slot_id, std::vector<std::uint8_t>& output) const {
    if (slot_id >= slot_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Heap-page slot is out of range");
    }
    if (output.size() < tuple_size_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Read output buffer is too small for the tuple");
    }
    if (!occupied(slot_id)) {
        throw DatabaseError(ErrorCode::RecordNotFound,
                            "Heap-page slot is free or logically deleted");
    }

    const auto begin = buffer_.begin() + tuple_offset(slot_id);
    std::copy(begin, begin + tuple_size_, output.begin());
}

bool HeapPage::update(const RID& rid, const std::vector<std::uint8_t>& tuple) {
    validate_rid(rid);
    validate_tuple(tuple);
    if (!occupied(rid.slot_id)) {
        throw DatabaseError(ErrorCode::RecordNotFound,
                            "Heap-page slot is free or logically deleted");
    }
    std::copy(tuple.begin(), tuple.end(), buffer_.begin() + tuple_offset(rid.slot_id));
    return true;
}

bool HeapPage::erase(const RID& rid) {
    validate_rid(rid);
    if (!occupied(rid.slot_id)) {
        throw DatabaseError(ErrorCode::RecordNotFound,
                            "Heap-page slot is free or logically deleted");
    }
    // Logical deletion preserves both the allocation bit and tuple bytes.
    set_deletion_bit(rid.slot_id, true);
    return true;
}

}  // namespace localdb
