


#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace localdb {

    using TableId = std::uint32_t;
    using PageNumber = std::uint32_t;
    using SlotId = std::uint16_t;
    using DiskPageId = std::uint32_t;

    // A logical page identity is scoped by table. Therefore, page zero in two
    // different tables has two different PageId values.
    struct PageId {
        TableId table_id;
        PageNumber page_number;

        friend constexpr bool operator==(const PageId& left, const PageId& right) noexcept {
            return left.table_id == right.table_id &&
                left.page_number == right.page_number;
        }

        friend constexpr bool operator!=(const PageId& left, const PageId& right) noexcept {
            return !(left == right);
        }
    };

    struct RID {
        PageId page_id;
        SlotId slot_id;

        friend constexpr bool operator==(const RID& left, const RID& right) noexcept {
            return left.page_id == right.page_id && left.slot_id == right.slot_id;
        }

        friend constexpr bool operator!=(const RID& left, const RID& right) noexcept {
            return !(left == right);
        }
    };

    struct PageIdHash {
        std::size_t operator()(const PageId& page_id) const noexcept {
            const auto table_hash = std::hash<TableId>{}(page_id.table_id);
            const auto page_hash = std::hash<PageNumber>{}(page_id.page_number);
            return table_hash ^ (page_hash + static_cast<std::size_t>(0x9e3779b9) +
                (table_hash << 6) + (table_hash >> 2));
        }
    };

    struct RIDHash {
        std::size_t operator()(const RID& rid) const noexcept {
            const auto page_hash = PageIdHash{}(rid.page_id);
            const auto slot_hash = std::hash<SlotId>{}(rid.slot_id);
            return page_hash ^ (slot_hash + static_cast<std::size_t>(0x9e3779b9) +
                (page_hash << 6) + (page_hash >> 2));
        }
    };

}  // namespace localdb

