#pragma once

#include "localdb/errors.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace localdb {

class Bitmap {
public:
    explicit Bitmap(std::size_t bit_count);

    std::size_t size() const noexcept { return bit_count_; }

    // Returns the current value of a logical bit.
    bool get(std::size_t index) const;

    // Sets or clears a bit and returns its previous value.
    bool set(std::size_t index);
    bool clear(std::size_t index);

    // Searches [start, size), then [0, start). Returns nullopt if every
    // logical bit is set. For a non-empty bitmap, start must be in range.
    // The search scans uint64_t words rather than testing every bit. Its
    // complexity is O(number of storage words + 64), or O(ceil(n / 64))
    // for a bitmap with n logical bits.
    std::optional<std::size_t> find_first_zero(std::size_t start) const;

private:
    static constexpr std::size_t kWordBits = 64;

    void check_index(std::size_t index) const;
    std::uint64_t valid_mask_for_word(std::size_t word_index) const;
    std::optional<std::size_t> find_zero_in_range(
        std::size_t begin, std::size_t end) const;

    std::size_t bit_count_;
    std::vector<std::uint64_t> words_;
};

}  // namespace localdb
