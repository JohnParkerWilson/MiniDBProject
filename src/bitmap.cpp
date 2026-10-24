#include "localdb/bitmap.h"

#include <limits>

namespace localdb {

Bitmap::Bitmap(std::size_t bit_count)
    : bit_count_(bit_count),
      words_(bit_count / kWordBits + (bit_count % kWordBits == 0 ? 0 : 1), 0) {}

// Logical bit i is stored in words_[i / 64], at bit position i % 64.
// Bit position zero is the least-significant bit of each uint64_t word.

void Bitmap::check_index(std::size_t index) const {
    if (index >= bit_count_) {
        throw DatabaseError(ErrorCode::InvalidArgument,
                            "Bitmap index is out of range");
    }
}

std::uint64_t Bitmap::valid_mask_for_word(std::size_t word_index) const {
    const std::size_t first_bit = word_index * kWordBits;
    if (first_bit >= bit_count_) {
        return 0;
    }
    const std::size_t remaining_bits = bit_count_ - first_bit;
    if (remaining_bits >= kWordBits) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << remaining_bits) - 1;
}

bool Bitmap::get(std::size_t index) const {
    check_index(index);
    const std::size_t word_index = index / kWordBits;
    const std::size_t bit_index = index % kWordBits;
    return (words_[word_index] & (std::uint64_t{1} << bit_index)) != 0;
}

bool Bitmap::set(std::size_t index) {
    check_index(index);
    const std::size_t word_index = index / kWordBits;
    const std::uint64_t mask = std::uint64_t{1} << (index % kWordBits);
    const bool previous = (words_[word_index] & mask) != 0;
    words_[word_index] |= mask;
    return previous;
}

bool Bitmap::clear(std::size_t index) {
    check_index(index);
    const std::size_t word_index = index / kWordBits;
    const std::uint64_t mask = std::uint64_t{1} << (index % kWordBits);
    const bool previous = (words_[word_index] & mask) != 0;
    words_[word_index] &= ~mask;
    return previous;
}

std::optional<std::size_t> Bitmap::find_zero_in_range(
    std::size_t begin, std::size_t end) const {
    if (begin >= end) {
        return std::nullopt;
    }

    std::size_t word_index = begin / kWordBits;
    while (word_index < words_.size()) {
        const std::size_t word_begin = word_index * kWordBits;
        const std::size_t word_end = word_begin + kWordBits;
        const std::size_t range_begin = begin > word_begin ? begin : word_begin;
        const std::size_t range_end = end < word_end ? end : word_end;

        if (range_begin < range_end) {
            const std::size_t first_bit = range_begin - word_begin;
            const std::size_t bit_count = range_end - range_begin;
            const std::uint64_t range_mask =
                bit_count == kWordBits
                    ? std::numeric_limits<std::uint64_t>::max()
                    : ((std::uint64_t{1} << bit_count) - 1) << first_bit;
            const std::uint64_t zero_bits =
                (~words_[word_index]) & range_mask & valid_mask_for_word(word_index);

            if (zero_bits != 0) {
                std::size_t offset = 0;
                while ((zero_bits & (std::uint64_t{1} << offset)) == 0) {
                    ++offset;
                }
                return word_begin + offset;
            }
        }

        ++word_index;
    }
    return std::nullopt;
}

std::optional<std::size_t> Bitmap::find_first_zero(std::size_t start) const {
    if (bit_count_ == 0) {
        if (start != 0) {
            throw DatabaseError(ErrorCode::InvalidArgument,
                                "Bitmap search hint is out of range");
        }
        return std::nullopt;
    }
    check_index(start);

    if (const auto result = find_zero_in_range(start, bit_count_)) {
        return result;
    }
    return find_zero_in_range(0, start);
}

}  // namespace localdb
