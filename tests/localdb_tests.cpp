#include "localdb/disk_manager.h"
#include "localdb/tuple.h"

#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
    using namespace localdb;

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

    const auto path = std::filesystem::temp_directory_path() / "localdb_initial_test.db";
    std::filesystem::remove(path);
    {
        DiskManager disk(path);
        const auto page_id = disk.allocate_page();
        std::vector<std::uint8_t> page(kPageSize, 0);
        page[0] = 42;
        disk.write_page(page_id, page);
        std::vector<std::uint8_t> read_back;
        disk.read_page(page_id, read_back);
        assert(read_back == page);
    }
    std::filesystem::remove(path);

    std::cout << "All initial localdb tests passed.\n";
    return 0;
}
