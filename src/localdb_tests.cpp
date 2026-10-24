#include "localdb/bitmap.h"
#include "localdb/catalog.h"
#include "localdb/disk_manager.h"
#include "localdb/heap_page.h"
#include "localdb/identifiers.h"
#include "localdb/tuple.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

int main() {
    using namespace localdb;

    const auto expect_error = [](auto operation, ErrorCode expected_code) {
        try {
            operation();
        } catch (const DatabaseError& error) {
            assert(error.code() == expected_code);
            return;
        }
        assert(false && "Expected DatabaseError was not thrown");
    };

    Bitmap bitmap(130);
    assert(bitmap.size() == 130);
    assert(!bitmap.get(0));
    assert(!bitmap.set(0));
    assert(bitmap.get(0));
    assert(bitmap.set(0));
    assert(bitmap.clear(0));
    assert(!bitmap.clear(0));

    assert(bitmap.set(129) == false);
    assert(bitmap.find_first_zero(0).value() == 0);
    assert(bitmap.find_first_zero(129).value() == 0);
    assert(bitmap.set(0) == false);
    assert(bitmap.find_first_zero(129).value() == 1);

    for (std::size_t index = 0; index < bitmap.size(); ++index) {
        bitmap.set(index);
    }
    assert(!bitmap.find_first_zero(0).has_value());
    expect_error([&] { bitmap.get(130); }, ErrorCode::InvalidArgument);
    expect_error([&] { bitmap.set(130); }, ErrorCode::InvalidArgument);
    expect_error([&] { bitmap.clear(130); }, ErrorCode::InvalidArgument);
    expect_error([&] { bitmap.find_first_zero(130); }, ErrorCode::InvalidArgument);

    Bitmap empty_bitmap(0);
    assert(!empty_bitmap.find_first_zero(0).has_value());
    expect_error([&] { empty_bitmap.find_first_zero(1); },
                 ErrorCode::InvalidArgument);

    Bitmap padded_bitmap(65);
    for (std::size_t index = 0; index < padded_bitmap.size(); ++index) {
        padded_bitmap.set(index);
    }
    // The remaining 63 storage bits are padding and must not be reported.
    assert(!padded_bitmap.find_first_zero(0).has_value());

    for (const std::size_t length : {std::size_t{1}, std::size_t{63},
                                     std::size_t{64}, std::size_t{65}}) {
        Bitmap boundary_bitmap(length);
        assert(boundary_bitmap.find_first_zero(0).value() == 0);

        const std::size_t last = length - 1;
        assert(!boundary_bitmap.set(last));
        assert(boundary_bitmap.set(last));
        assert(boundary_bitmap.get(last));
        assert(boundary_bitmap.clear(last));
        assert(!boundary_bitmap.clear(last));
        assert(boundary_bitmap.find_first_zero(0).value() == 0);

        // Fill the logical range and verify that padding, if any, is ignored.
        for (std::size_t index = 0; index < length; ++index) {
            boundary_bitmap.set(index);
        }
        assert(!boundary_bitmap.find_first_zero(0).has_value());

        // Repeated updates must preserve the previous-value contract.
        assert(boundary_bitmap.clear(0));
        assert(!boundary_bitmap.clear(0));
        assert(boundary_bitmap.find_first_zero(last).value() == 0);
        assert(!boundary_bitmap.set(0));
        assert(boundary_bitmap.set(0));
        assert(!boundary_bitmap.find_first_zero(0).has_value());
    }

    Bitmap word_boundary_bitmap(65);
    assert(!word_boundary_bitmap.set(63));
    assert(!word_boundary_bitmap.set(64));
    assert(word_boundary_bitmap.get(63));
    assert(word_boundary_bitmap.get(64));
    assert(word_boundary_bitmap.find_first_zero(63).value() == 0);
    assert(word_boundary_bitmap.clear(63));
    assert(word_boundary_bitmap.find_first_zero(63).value() == 63);
    word_boundary_bitmap.set(63);
    assert(word_boundary_bitmap.find_first_zero(64).value() == 0);

    const PageId heap_page_id{7, 3};
    HeapPage heap_page(heap_page_id, 16);
    assert(heap_page.slot_count() > 0);
    assert(heap_page.tuple_width() == 16);
    assert(heap_page.slot_capacity() == heap_page.slot_count());
    assert(heap_page.allocated_slot_count() == 0);
    assert(heap_page.free_slot_count() == heap_page.slot_count());
    const auto empty_page_before_search = heap_page.buffer();
    assert(heap_page.find_free_slot().value() == 0);
    assert(heap_page.buffer() == empty_page_before_search);
    std::vector<std::uint8_t> free_output(16, 0xbb);
    expect_error([&] { heap_page.read(0, free_output); },
                 ErrorCode::RecordNotFound);
    expect_error(
        [&] { heap_page.update(RID{heap_page_id, 0}, std::vector<std::uint8_t>(16, 0x44)); },
        ErrorCode::RecordNotFound);
    for (std::size_t index = 0; index < 8; ++index) {
        assert(heap_page.buffer()[index] == 0);
    }
    assert(heap_page.buffer()[8] == 'H');
    assert(heap_page.buffer()[9] == 'P');
    assert(heap_page.buffer()[10] == 'G');
    assert(heap_page.buffer()[11] == '1');
    assert(heap_page.buffer()[12] == 0x01);
    assert(heap_page.buffer()[13] == 0x00);
    assert(heap_page.buffer()[14] == 0x10);
    assert(heap_page.buffer()[15] == 0x00);
    assert(heap_page.buffer()[16] == static_cast<std::uint8_t>(heap_page.slot_count() & 0xff));
    assert(heap_page.buffer()[17] == static_cast<std::uint8_t>(heap_page.slot_count() >> 8));
    assert(heap_page.buffer()[18] == 0x00);
    assert(heap_page.buffer()[19] == 0x00);

    const std::vector<std::uint8_t> first_tuple(16, 0x11);
    const auto first_rid = heap_page.insert(first_tuple);
    assert(first_rid.has_value());
    assert(first_rid->page_id == heap_page_id);
    assert(heap_page.is_allocated(first_rid->slot_id));
    assert(!heap_page.is_deleted(first_rid->slot_id));
    assert(heap_page.occupied(first_rid->slot_id));
    assert(heap_page.get(*first_rid).value() == first_tuple);
    const auto page_before_read = heap_page.buffer();
    std::vector<std::uint8_t> read_output(20, 0xcc);
    heap_page.read(first_rid->slot_id, read_output);
    assert(std::equal(first_tuple.begin(), first_tuple.end(), read_output.begin()));
    assert(read_output[16] == 0xcc);
    assert(read_output[17] == 0xcc);
    assert(heap_page.buffer() == page_before_read);

    std::vector<std::uint8_t> short_output(15, 0xdd);
    const auto short_output_before = short_output;
    expect_error([&] { heap_page.read(first_rid->slot_id, short_output); },
                 ErrorCode::InvalidArgument);
    assert(short_output == short_output_before);

    const std::vector<std::uint8_t> second_tuple(16, 0x22);
    const auto before_update = heap_page.buffer();
    const auto allocated_before_update = heap_page.allocated_slot_count();
    const auto slot_before_update = first_rid->slot_id;
    assert(heap_page.update(*first_rid, second_tuple));
    assert(first_rid->slot_id == slot_before_update);
    assert(heap_page.allocated_slot_count() == allocated_before_update);
    assert(heap_page.is_allocated(first_rid->slot_id));
    assert(!heap_page.is_deleted(first_rid->slot_id));
    assert(heap_page.get(*first_rid).value() == second_tuple);
    assert(heap_page.buffer().size() == before_update.size());
    for (std::size_t index = 0; index < heap_page.buffer().size(); ++index) {
        const bool is_tuple_byte =
            index >= 24 + 2 * ((heap_page.slot_capacity() + 63) / 64) * 8 +
            static_cast<std::size_t>(first_rid->slot_id) * heap_page.tuple_width() &&
            index < 24 + 2 * ((heap_page.slot_capacity() + 63) / 64) * 8 +
            (static_cast<std::size_t>(first_rid->slot_id) + 1) * heap_page.tuple_width();
        if (!is_tuple_byte) {
            assert(heap_page.buffer()[index] == before_update[index]);
        }
    }
    expect_error(
        [&] { heap_page.update(*first_rid, std::vector<std::uint8_t>(15, 0x55)); },
        ErrorCode::InvalidArgument);
    assert(heap_page.allocated_slot_count() == allocated_before_update);
    const auto before_delete = heap_page.buffer();
    const auto allocated_before_delete = heap_page.allocated_slot_count();
    const std::size_t bitmap_bytes =
        ((heap_page.slot_capacity() + 63) / 64) * sizeof(std::uint64_t);
    const std::size_t deletion_byte =
        24 + bitmap_bytes + first_rid->slot_id / 8;
    const auto deletion_mask =
        static_cast<std::uint8_t>(1u << (first_rid->slot_id % 8));
    auto expected_after_delete = before_delete;
    expected_after_delete[deletion_byte] |= deletion_mask;

    assert(heap_page.erase(*first_rid));
    assert(heap_page.buffer() == expected_after_delete);
    assert(heap_page.allocated_slot_count() == allocated_before_delete);
    assert(heap_page.is_allocated(first_rid->slot_id));
    assert(heap_page.is_deleted(first_rid->slot_id));
    assert(!heap_page.occupied(first_rid->slot_id));
    assert(heap_page.find_free_slot().value() != first_rid->slot_id);
    assert(heap_page.get(*first_rid) == std::nullopt);
    const auto page_before_deleted_read = heap_page.buffer();
    std::vector<std::uint8_t> deleted_output(16, 0xee);
    expect_error([&] { heap_page.read(first_rid->slot_id, deleted_output); },
                 ErrorCode::RecordNotFound);
    assert(heap_page.buffer() == page_before_deleted_read);
    expect_error(
        [&] { heap_page.update(*first_rid, second_tuple); },
        ErrorCode::RecordNotFound);
    assert(heap_page.buffer() == page_before_deleted_read);
    expect_error([&] { heap_page.erase(*first_rid); },
                 ErrorCode::RecordNotFound);
    assert(heap_page.buffer() == page_before_deleted_read);

    const auto reused_rid = heap_page.insert(first_tuple);
    assert(reused_rid.has_value());
    assert(reused_rid->slot_id != first_rid->slot_id);
    assert(!heap_page.is_deleted(reused_rid->slot_id));
    assert(heap_page.occupied(reused_rid->slot_id));

    while (heap_page.find_free_slot()) {
        heap_page.insert(first_tuple);
    }
    assert(!heap_page.find_free_slot());
    const auto full_page_before_insert = heap_page.buffer();
    assert(!heap_page.insert(first_tuple));
    assert(heap_page.buffer() == full_page_before_insert);
    assert(heap_page.erase(*reused_rid));
    assert(!heap_page.find_free_slot());
    const auto deleted_full_page_before_insert = heap_page.buffer();
    assert(!heap_page.insert(first_tuple));
    assert(heap_page.buffer() == deleted_full_page_before_insert);

    const auto persisted_heap_buffer = heap_page.buffer();
    const auto reopened_heap_page =
        HeapPage::from_buffer(heap_page_id, 16, persisted_heap_buffer);
    assert(reopened_heap_page.free_slot_count() == 0);
    assert(reopened_heap_page.allocated_slot_count() == reopened_heap_page.slot_count());
    assert(reopened_heap_page.buffer() == persisted_heap_buffer);

    HeapPage::Buffer invalid_initialize_buffer(kPageSize, 0xa5);
    const auto invalid_before = invalid_initialize_buffer;
    expect_error([&] { HeapPage::Initialize(invalid_initialize_buffer, 10); },
                 ErrorCode::InvalidArgument);
    assert(invalid_initialize_buffer == invalid_before);

    HeapPage::Buffer wrong_size_buffer(12, 0xa5);
    const auto wrong_size_before = wrong_size_buffer;
    expect_error([&] { HeapPage::Initialize(wrong_size_buffer, 16); },
                 ErrorCode::InvalidArgument);
    assert(wrong_size_buffer == wrong_size_before);

    expect_error(
        [&] { heap_page.insert(std::vector<std::uint8_t>(15, 0)); },
        ErrorCode::InvalidArgument);
    assert(heap_page.buffer() == persisted_heap_buffer);
    expect_error(
        [&] { heap_page.get(RID{PageId{7, 4}, 0}); },
        ErrorCode::InvalidArgument);
    expect_error([&] { heap_page.is_allocated(heap_page.slot_capacity()); },
                 ErrorCode::InvalidArgument);
    expect_error([&] { heap_page.is_deleted(heap_page.slot_capacity()); },
                 ErrorCode::InvalidArgument);
    expect_error([&] { heap_page.occupied(heap_page.slot_capacity()); },
                 ErrorCode::InvalidArgument);
    expect_error(
        [&] {
            heap_page.get(RID{heap_page_id,
                              static_cast<SlotId>(heap_page.slot_capacity())});
        },
        ErrorCode::InvalidArgument);
    expect_error(
        [&] { HeapPage::from_buffer(PageId{7, 3}, 16, HeapPage::Buffer{}); },
        ErrorCode::InvalidArgument);
    expect_error([&] { HeapPage(PageId{7, 4}, 0); }, ErrorCode::InvalidArgument);
    expect_error([&] { HeapPage(PageId{7, 4}, 7); }, ErrorCode::InvalidArgument);
    expect_error([&] { HeapPage(PageId{7, 4}, 10); }, ErrorCode::InvalidArgument);
    expect_error([&] { HeapPage(PageId{7, 4}, 4088); }, ErrorCode::InvalidArgument);

    const std::size_t test_tuple_width = 16;
    std::size_t expected_capacity = (kPageSize - 24) / test_tuple_width;
    const auto bitmap_bytes_for = [](std::size_t slots) {
        return ((slots + 63) / 64) * sizeof(std::uint64_t);
    };
    while (24 + 2 * bitmap_bytes_for(expected_capacity) +
               expected_capacity * test_tuple_width > kPageSize) {
        --expected_capacity;
    }
    assert(heap_page.slot_count() == expected_capacity);
    assert(24 + 2 * bitmap_bytes_for(heap_page.slot_count()) +
               heap_page.slot_count() * heap_page.tuple_size() <= kPageSize);
    assert(24 + 2 * bitmap_bytes_for(heap_page.slot_count() + 1) +
               (heap_page.slot_count() + 1) * heap_page.tuple_size() > kPageSize);

    const Schema schema({
        Column("id", TypeId::Int32),
        Column("balance", TypeId::Int64),
        Column("active", TypeId::Boolean),
        Column("code", TypeId::String32),
        Column("name", TypeId::Varchar),
    });
    const Tuple original({
        std::int32_t{7}, std::int64_t{9000}, true,
        std::string("DB-001"), std::string("Ada")});
    const auto encoded = original.serialize(schema);
    const auto decoded = Tuple::deserialize(schema, encoded);
    assert(std::get<std::int32_t>(decoded.value_at(0)) == 7);
    assert(std::get<std::int64_t>(decoded.value_at(1)) == 9000);
    assert(std::get<bool>(decoded.value_at(2)));
    assert(std::get<std::string>(decoded.value_at(3)) == "DB-001");
    assert(std::get<std::string>(decoded.value_at(4)) == "Ada");

    // The first field starts after the 2-byte field count and 1-byte null flag.
    // 0x01020304 must be stored least-significant byte first.
    const Schema endian_schema({Column("number", TypeId::Int32)});
    const Tuple endian_tuple({std::int32_t{0x01020304}});
    const auto endian_bytes = endian_tuple.serialize(endian_schema);
    assert(endian_bytes[3] == 0x04);
    assert(endian_bytes[4] == 0x03);
    assert(endian_bytes[5] == 0x02);
    assert(endian_bytes[6] == 0x01);

    const Schema edge_schema({
        Column("negative_int32", TypeId::Int32),
        Column("negative_int64", TypeId::Int64),
        Column("empty_fixed", TypeId::String32),
        Column("exact_fixed", TypeId::String32),
        Column("empty_varchar", TypeId::Varchar),
        Column("utf8", TypeId::String32),
    });
    const std::string exactly_32_bytes = "12345678901234567890123456789012";
    const std::string utf8_text = "café 世界";
    assert(exactly_32_bytes.size() == 32);
    assert(utf8_text.size() < 32);

    const Tuple edge_original({
        std::int32_t{-123456},
        std::int64_t{-9876543210LL},
        std::string(),
        exactly_32_bytes,
        std::string(),
        utf8_text,
    });
    const auto edge_encoded = edge_original.serialize(edge_schema);
    const auto edge_decoded = Tuple::deserialize(edge_schema, edge_encoded);
    assert(std::get<std::int32_t>(edge_decoded.value_at(0)) == -123456);
    assert(std::get<std::int64_t>(edge_decoded.value_at(1)) == -9876543210LL);
    assert(std::get<std::string>(edge_decoded.value_at(2)).empty());
    assert(std::get<std::string>(edge_decoded.value_at(3)) == exactly_32_bytes);
    assert(std::get<std::string>(edge_decoded.value_at(4)).empty());
    assert(std::get<std::string>(edge_decoded.value_at(5)) == utf8_text);

    const PageId table_one_page_zero{1, 0};
    const PageId table_two_page_zero{2, 0};
    assert(table_one_page_zero != table_two_page_zero);

    std::unordered_map<PageId, int, PageIdHash> buffer_pool_pages;
    buffer_pool_pages[table_one_page_zero] = 10;
    buffer_pool_pages[table_two_page_zero] = 20;
    assert(buffer_pool_pages.size() == 2);
    assert(buffer_pool_pages.at(table_one_page_zero) == 10);
    assert(buffer_pool_pages.at(table_two_page_zero) == 20);

    const RID first_record{table_one_page_zero, 0};
    const RID second_record{table_one_page_zero, 1};
    assert(first_record != second_record);

    expect_error(
        [] {
            Schema({Column("duplicate", TypeId::Int32),
                    Column("duplicate", TypeId::Int64)});
        },
        ErrorCode::InvalidSchema);

    const auto catalog_path = std::filesystem::temp_directory_path() / "localdb_catalog.db";
    std::filesystem::remove(catalog_path);
    {
        Catalog catalog(catalog_path);
        const TableId users_id = catalog.create_table(
            "users",
            Schema({
                Column("id", TypeId::Int32),
                Column("username", TypeId::String32),
            }));
        const TableId logs_id = catalog.create_table(
            "logs",
            Schema({Column("message", TypeId::Varchar, true)}));
        assert(users_id != logs_id);
        assert(catalog.table_count() == 2);
    }
    {
        Catalog reopened_catalog(catalog_path);
        const auto& users = reopened_catalog.table("users");
        const auto& logs = reopened_catalog.table("logs");
        assert(users.id == 1);
        assert(logs.id == 2);
        assert(users.schema.column_count() == 2);
        assert(users.schema.column(0).type == TypeId::Int32);
        assert(users.schema.column(1).type == TypeId::String32);
        assert(logs.schema.column(0).nullable);

        const TableId next_id = reopened_catalog.create_table(
            "sessions", Schema({Column("token", TypeId::String32)}));
        assert(next_id == 3);

        expect_error([&] { reopened_catalog.table("missing"); },
                     ErrorCode::InvalidArgument);
        expect_error([&] { reopened_catalog.table(999); },
                     ErrorCode::InvalidArgument);
    }
    std::filesystem::remove(catalog_path);

    const auto malformed_catalog_path =
        std::filesystem::temp_directory_path() / "localdb_malformed_catalog.db";
    {
        std::ofstream output(malformed_catalog_path, std::ios::binary | std::ios::trunc);
        const std::vector<std::uint8_t> malformed_bytes = {
            'L', 'D', 'B', 'C', 'A', 'T', '0', '1',  // magic
            2, 0, 0, 0,                               // format version
            1, 0, 0, 0,                               // invalid next table ID
            1, 0, 0, 0,                               // one table
            1, 0, 0, 0,                               // table ID 1
            3, 0, 0, 0, 'b', 'a', 'd',                // table name
            0, 0, 0, 0,                               // zero columns
            0, 0, 0, 0                                // zero physical pages
        };
        output.write(reinterpret_cast<const char*>(malformed_bytes.data()),
                     static_cast<std::streamsize>(malformed_bytes.size()));
    }
    expect_error([&] { Catalog malformed(malformed_catalog_path); },
                 ErrorCode::Corruption);
    std::filesystem::remove(malformed_catalog_path);

    const auto path = std::filesystem::temp_directory_path() / "localdb_initial_test.db";
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".catalog");
    {
        DiskManager disk(path);
        const TableId table_id = disk.create_table(
            "pages", Schema({Column("value", TypeId::Int32)}));
        const TableId other_table_id = disk.create_table(
            "other_pages", Schema({Column("value", TypeId::Int32)}));
        const auto page_id = disk.allocate_page(table_id);
        const auto second_page_id = disk.allocate_page(table_id);
        const auto other_page_id = disk.allocate_page(other_table_id);
        assert(page_id.page_number == 0);
        assert(second_page_id.page_number == 1);
        assert(other_page_id.page_number == 0);
        assert(other_page_id != page_id);
        std::vector<std::uint8_t> page(kPageSize, 0);
        std::vector<std::uint8_t> initialized_page;
        disk.read_page(page_id, initialized_page);
        for (const auto byte : initialized_page) {
            assert(byte == 0);
        }
        page[0] = 42;
        disk.write_page(page_id, page);
        std::vector<std::uint8_t> read_back;
        disk.read_page(page_id, read_back);
        assert(read_back == page);
        disk.close();
    }
    {
        DiskManager reopened(path);
        const auto& table = reopened.open_table("pages");
        assert(table.id == 1);
        assert(reopened.page_count(table.id) == 2);
        const auto third_page_id = reopened.allocate_page(table.id);
        assert(third_page_id.page_number == 2);
        expect_error([&] { reopened.open_table("missing"); },
                     ErrorCode::InvalidArgument);
        expect_error(
            [&] {
                std::vector<std::uint8_t> missing_page;
                reopened.read_page(PageId{table.id, 99}, missing_page);
            },
            ErrorCode::PageNotFound);
        expect_error(
            [&] { reopened.write_page(third_page_id, std::vector<std::uint8_t>(1)); },
            ErrorCode::InvalidArgument);
        reopened.sync();
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".catalog");

    const auto truncated_path =
        std::filesystem::temp_directory_path() / "localdb_truncated.db";
    std::filesystem::remove(truncated_path);
    std::filesystem::remove(truncated_path.string() + ".catalog");
    {
        DiskManager disk(truncated_path);
        const TableId table_id = disk.create_table(
            "truncated", Schema({Column("value", TypeId::Int32)}));
        disk.allocate_page(table_id);
        disk.close();
    }
    std::filesystem::resize_file(truncated_path, kPageSize - 1);
    expect_error([&] { DiskManager malformed_database(truncated_path); },
                 ErrorCode::Corruption);
    std::filesystem::remove(truncated_path);
    std::filesystem::remove(truncated_path.string() + ".catalog");

    std::cout << "All initial localdb tests passed.\n";
    return 0;
}
