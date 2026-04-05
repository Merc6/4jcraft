#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <limits>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace compression {

template <typename T>
concept PaletteValue =
    std::equality_comparable<T> and requires(T val) { std::hash<T>{}(val); };

template <std::size_t INLINE_PALETTE_THRESHOLD, PaletteValue T>
class HybridPalette {
public:
    struct PaletteEntry {
        T value;
        std::size_t count;
    };

    constexpr HybridPalette() noexcept
        : m_storage(std::array<std::optional<PaletteEntry>,
                               INLINE_PALETTE_THRESHOLD>{}) {}

    [[nodiscard]]
    constexpr std::size_t len() const noexcept {
        return m_real_entries;
    }

    [[nodiscard]]
    constexpr bool is_empty() const noexcept {
        return m_real_entries == 0;
    }

    [[nodiscard]]
    constexpr std::size_t index_size() const noexcept {
        return m_index_size;
    }

    constexpr void mark_as_unused(std::size_t index) {
        --m_real_entries;

        if (auto* array =
                std::get_if<std::array<std::optional<PaletteEntry>,
                                       INLINE_PALETTE_THRESHOLD>>(&m_storage)) {
            (*array)[index] = std::nullopt;
        } else {
            HashMapStorage& hms = std::get<HashMapStorage>(m_storage);
            hms.free_indices.push_back(index);

            auto node = hms.index_map.extract(index);
            assert(node);
            assert(node.mapped().count == 0);
            hms.value_map.erase(node.mapped().value);
        }
    }

    std::optional<std::pair<PaletteEntry*, std::size_t>> get_mut_by_value(
        const T& value) noexcept {
        if (auto* arr =
                std::get_if<std::array<std::optional<PaletteEntry>,
                                       INLINE_PALETTE_THRESHOLD>>(&m_storage)) {
            for (const auto& [idx, entry] : std::ranges::views::enumerate(*arr)) {
                if (entry.has_value() && entry->value == value) {
                    return std::pair{&entry.value(), idx};
                }
            }
            return std::nullopt;
        }

        HashMapStorage& hms = std::get<HashMapStorage>(m_storage);
        auto iter = hms.value_map.find(value);
        if (iter == hms.value_map.end()) {
            return std::nullopt;
        }

        std::size_t index = iter->second;
        auto entry_it = hms.index_map.find(index);
        if (entry_it == hms.index_map.end()) {
            return std::nullopt;
        }

        return std::pair{&entry_it->second, index};
    }

    const PaletteEntry* get_by_index(const std::size_t index) const noexcept {
        if (auto* arr =
                std::get_if<std::array<std::optional<PaletteEntry>,
                                       INLINE_PALETTE_THRESHOLD>>(&m_storage)) {
            const auto& entry = (*arr)[index];
            if (!entry.has_value()) {
                return nullptr;
            }
            return &entry.value();
        }

        const HashMapStorage& hms = std::get<HashMapStorage>(m_storage);
        auto iter = hms.index_map.find(index);
        if (iter == hms.index_map.end()) {
            return nullptr;
        }

        return &iter->second;
    }

    PaletteEntry* get_mut_by_index(const std::size_t index) noexcept {
        // clang-format off
        return std::visit(overload {
            [&](ArrayStorage& ars) -> PaletteEntry* {
                const auto& entry = ars[index];

                if (!entry.has_value()) {
                    return nullptr;
                }

                return &(ars[index].value());
            },
            [&](HashMapStorage& hms) -> PaletteEntry* {
                auto iter = hms.index_map.find(index);

                if (iter == hms.index_map.end()) {
                    return nullptr;
                }

                return &iter->second;
            },
        }, m_storage);
        // clang-format on
    }

    std::pair<std::size_t, std::optional<std::size_t>> insert_new(
        const PaletteEntry entry) noexcept {
        // clang-format off
        return std::visit(overload {
            [&](ArrayStorage& ars) -> std::pair<std::size_t, std::optional<std::size_t>> {
                for (const auto& [i, old_entry] : ars | std::ranges::views::enumerate) {
                    if (!old_entry.has_value() or old_entry->count == 0) {
                        old_entry = entry;
                        ++m_real_entries;

                        const std::size_t new_index_size = std::bit_width(m_real_entries);
                        std::optional<std::size_t> actual_new_index_size = std::nullopt;

                        if (new_index_size > m_index_size) {
                            m_index_size = new_index_size;
                            actual_new_index_size = new_index_size;
                        }

                        return std::pair{i, actual_new_index_size};
                    }
                }

                switch_to_hashmap();
                return insert_new(entry);
            },
            [&](HashMapStorage& hms) -> std::pair<std::size_t, std::optional<std::size_t>> {
                if (!hms.free_indices.empty()) {
                    const std::size_t index = hms.free_indices.back();
                    hms.free_indices.pop_back();
                    hms.value_map.emplace(entry.value, index);
                    hms.index_map.emplace(index, entry);
                    ++m_real_entries;
                    return std::pair{index, std::nullopt};
                }

                const std::size_t index = hms.index_map.size();
                hms.value_map.emplace(entry.value, index);
                hms.index_map.emplace(index, entry);
                ++m_real_entries;

                const std::size_t new_index_size = std::bit_width(m_real_entries);
                std::optional<std::size_t> actual_new_index_size = std::nullopt;

                if (new_index_size > m_index_size) {
                    m_index_size = new_index_size;
                    actual_new_index_size = new_index_size;
                }

                return std::pair{index, actual_new_index_size};
            }
        }, m_storage);
        // clang-format on
    }

    std::optional<std::unordered_map<std::size_t, std::size_t>>
    optimize() noexcept {
        m_index_size = std::bit_width(m_real_entries);

        // clang-format off
        return std::visit(overload{
            [&](ArrayStorage& ars) -> std::optional<std::unordered_map<std::size_t, std::size_t>> {
                std::unordered_map<T, std::size_t> old_mapping;

                for (auto& [idx, entry] : std::ranges::views::enumerate(ars)
                                        | std::ranges::views::filter([](auto& entry) { return entry.has_value(); }))
                {
                    old_mapping.emplace(entry->value, idx);
                }

                std::ranges::sort(ars, [](const auto& min, const auto& max) {
                    if (min.has_value() != max.has_value()) {
                        return min.has_value() > max.has_value();
                    }

                    if (!min.has_value()) {
                        return false;
                    }

                    return min->count > max->count;
                });

                std::unordered_map<std::size_t, std::size_t> new_mapping;
                bool needs_new_mapping = false;

                for (auto& [new_index, entry] : std::ranges::views::enumerate(ars)) {
                    if (!entry.has_value()) {
                        break;
                    }

                    std::size_t old_index = old_mapping.at(entry->value);

                    if (new_index != old_index) {
                        needs_new_mapping = true;
                    }

                    new_mapping.emplace(old_index, new_index);
                }

                if (needs_new_mapping) {
                    return new_mapping;
                }

                return std::nullopt;
            },
            [&](HashMapStorage& hms) -> std::optional<std::unordered_map<std::size_t, std::size_t>> {
                assert(hms.index_map.size() == hms.value_map.size());

                if (hms.index_map.size() <= INLINE_PALETTE_THRESHOLD) {
                    return switch_to_array();
                }

                if (hms.free_indices.empty()) {
                    return std::nullopt;
                }

                using Entry = std::pair<std::size_t, PaletteEntry>;
                std::vector<Entry> entries(
                    hms.index_map.begin(),
                    hms.index_map.end()
                );

                std::ranges::sort(entries, [](const Entry& min, const Entry& max) {
                    if (min.second.count != max.second.count) {
                        return min.second.count > max.second.count;
                    }

                    return min.first < max.first;
                });

                std::unordered_map<std::size_t, std::size_t> new_mapping;
                std::unordered_map<std::size_t, PaletteEntry> new_index_map;
                std::unordered_map<T, std::size_t> new_value_map;

                for (auto& [new_index, kv] : std::ranges::views::enumerate(entries)) {
                    auto& [old_index, entry] = kv;
                    new_value_map.emplace(entry.value, new_index);
                    new_mapping.emplace(old_index, new_index);
                    new_index_map.emplace(new_index, std::move(entry));
                }

                m_storage = HashMapStorage{
                    .free_indices = {},
                    .index_map = std::move(new_index_map),
                    .value_map = std::move(new_value_map),
                };

                return new_mapping;
            },
        }, m_storage);
        // clang-format on
    }

    std::generator<const T&> iter() const {
        // clang-format off
        return std::visit(overload {
            [](ArrayStorage ars) -> std::generator<const T&> {
                for (const auto& entry : ars) {
                    if (entry.has_value()) {
                        co_yield entry->value;
                    }
                }
            },
            [](HashMapStorage hms) -> std::generator<const T&> {
                for (const auto& [idx, entry] : hms.index_map) {
                    co_yield entry.value;
                }
            },
        }, m_storage);
        // clang-format on
    }

    std::generator<T&> iter_mut() {
        // clang-format off
        return std::visit(overload {
            [](ArrayStorage ars) -> std::generator<T&> {
                for (auto& entry : ars) {
                    if (entry.has_value()) {
                        co_yield entry->value;
                    }
                }
            },
            [](HashMapStorage hms) -> std::generator<T&> {
                for (auto& [idx, entry] : hms.index_map) {
                    co_yield entry.value;
                }
            },
        }, m_storage);
        // clang-format on
    }

    std::vector<T> to_vec() const noexcept {
        std::vector<T> collection;
        collection.reserve(m_real_entries);

        for (const T& entry : iter()) {
            collection.push_back(entry);
        }

        return collection;
    }

private:
    void switch_to_hashmap() noexcept {
        auto arr = std::get<
            std::array<std::optional<PaletteEntry>, INLINE_PALETTE_THRESHOLD>>(
            m_storage);

        std::vector<std::size_t> free_indices;
        std::unordered_map<std::size_t, PaletteEntry> index_map;
        std::unordered_map<T, std::size_t> value_map;

        for (auto [idx, entry] : arr | std::ranges::views::enumerate) {
            if (entry.has_value()) {
                assert(entry->count > 0);
                value_map.emplace(entry->value, idx);
                index_map.emplace(idx, *entry);
            } else {
                free_indices.push_back(idx);
            }
        }

        assert(index_map.size() == value_map.size());
        assert(index_map.size() == INLINE_PALETTE_THRESHOLD);

        m_storage = HashMapStorage{
            .free_indices = free_indices,
            .index_map = index_map,
            .value_map = value_map,
        };
    }

    std::optional<std::unordered_map<std::size_t, std::size_t>>
    switch_to_array() noexcept {
        auto& hms = std::get<HashMapStorage>(m_storage);
        assert(hms.index_map.size() == hms.value_map.size());
        assert(hms.index_map.size() <= INLINE_PALETTE_THRESHOLD);

        using Entry = std::pair<const std::size_t, PaletteEntry>;
        std::vector<const Entry*> sorted;
        sorted.reserve(hms.index_map.size());
        for (const auto& entry : hms.index_map) {
            sorted.push_back(&entry);
        }

        std::ranges::sort(sorted, [](const Entry* lhs, const Entry* rhs) {
            if (lhs->second.count != rhs->second.count) {
                return lhs->second.count > rhs->second.count;
            }

            return lhs->first < rhs->first;
        });

        std::array<std::optional<PaletteEntry>, INLINE_PALETTE_THRESHOLD>
            array{};
        std::unordered_map<std::size_t, std::size_t> new_mapping;
        bool needs_new_mapping = false;

        for (auto [new_index, entry] : std::ranges::views::enumerate(sorted)) {
            assert(entry->second.count > 0);

            if (new_index != entry->first) {
                needs_new_mapping = true;
            }

            new_mapping.emplace(entry->first, new_index);
            array[new_index] = entry->second;
        }

        m_storage = array;

        if (needs_new_mapping) {
            return new_mapping;
        }

        return std::nullopt;
    }

    template <class... Ts>
    struct overload : Ts... {
        using Ts::operator()...;
    };

    using ArrayStorage =
        std::array<std::optional<PaletteEntry>, INLINE_PALETTE_THRESHOLD>;

    struct HashMapStorage {
        std::vector<std::size_t> free_indices;
        std::unordered_map<std::size_t, PaletteEntry> index_map;
        std::unordered_map<T, std::size_t> value_map;
    };

    std::variant<ArrayStorage, HashMapStorage> m_storage;

    std::size_t m_index_size{};
    std::size_t m_real_entries{};
};

class AlignedIndexBuffer {
public:
    constexpr AlignedIndexBuffer() = default;

    void zeroed(const std::size_t len) noexcept {
        if (m_index_size == 0) {
            assert(m_storage.empty());
            m_len = len;
            return;
        }

        const std::size_t indices_per_u64 = 64 / m_index_size;
        m_indices_per_u64 = indices_per_u64;
        const std::size_t needed_u64 =
            (len + indices_per_u64 - 1) / indices_per_u64;
        m_mask = (1U << m_index_size) - 1;
        m_storage.resize(needed_u64, 0);
        std::fill(m_storage.begin(), m_storage.end(), 0);
        m_len = len;
    }

    [[nodiscard]]
    std::size_t len() const noexcept {
        return m_len;
    }

    [[nodiscard]]
    bool is_empty() const noexcept {
        return m_len == 0;
    }

    void set_index_size(
        const std::size_t new_size,
        const std::optional<std::unordered_map<std::size_t, std::size_t>>&
            new_mapping) noexcept {
        if (new_size > m_index_size) {
            const std::size_t new_indices_per_u64 = 64 / new_size;
            const std::size_t needed_u64 =
                (m_len + new_indices_per_u64 - 1) / new_indices_per_u64;
            m_storage.resize(needed_u64, 0);

            if (new_mapping.has_value()) {
                const auto& mapping = *new_mapping;

                for (std::size_t offset :
                     std::views::iota(0U, m_len) | std::views::reverse) {
                    const auto old_index = get_index(offset);
                    const auto new_index = mapping.at(old_index);
                    set_index_with_index_size(offset, new_size,
                                              new_indices_per_u64, new_index);
                }
            } else {
                for (std::size_t offset :
                     std::views::iota(0U, m_len) | std::views::reverse) {
                    const auto old_index = get_index(offset);
                    set_index_with_index_size(offset, new_size,
                                              new_indices_per_u64, old_index);
                }
            }

            m_indices_per_u64 = static_cast<uint8_t>(new_indices_per_u64);
            m_mask = (1U << new_size) - 1;
        } else if (new_size < m_index_size) {
            if (new_size == 0) {
                if (new_mapping.has_value()) {
                    assert(new_mapping->size() == 1);
                    assert(new_mapping->contains(0));
                }
                m_index_size = 0;
                m_storage.clear();
                return;
            }

            const std::size_t new_indices_per_u64 = 64 / new_size;

            if (new_mapping.has_value()) {
                const auto& mapping = *new_mapping;

                for (std::size_t offset : std::views::iota(0U, m_len)) {
                    const auto old_index = get_index(offset);
                    const auto new_index = mapping.at(old_index);
                    set_index_with_index_size(offset, new_size,
                                              new_indices_per_u64, new_index);
                }
            } else {
                for (std::size_t offset : std::views::iota(0U, m_len)) {
                    const auto index = get_index(offset);
                    set_index_with_index_size(offset, new_size,
                                              new_indices_per_u64, index);
                }
            }

            m_indices_per_u64 = static_cast<uint8_t>(new_indices_per_u64);
            m_mask = (1U << new_size) - 1;
            const auto needed_u64 =
                (m_len + new_indices_per_u64 - 1) / new_indices_per_u64;
            m_storage.resize(needed_u64);
        } else if (new_mapping.has_value()) {
            const auto& mapping = *new_mapping;

            for (std::size_t offset : std::views::iota(0U, m_len)) {
                const auto old_index = get_index(offset);
                const auto new_index = mapping.at(old_index);
                set_index(offset, new_index);
            }
        }

        m_index_size = new_size;
    }

    void push_index(const std::size_t index) noexcept {
        if (m_index_size == 0) {
            ++m_len;
            return;
        }

        const std::size_t indices_per_u64 = m_indices_per_u64;

        if (m_len % indices_per_u64 == 0) {
            m_storage.push_back(index);
            ++m_len;
            return;
        }

        ++m_len;
        set_index(m_len - 1, index);
    }

    std::optional<std::size_t> pop_index() noexcept {
        if (m_len == 0) {
            return std::nullopt;
        }

        if (m_index_size == 0) {
            --m_len;
            return 0;
        }

        const std::size_t indices_per_u64 = m_indices_per_u64;
        const auto index = get_index(m_len - 1);

        if (m_len % indices_per_u64 == 0) {
            m_storage.pop_back();
        }

        return index;
    }

    std::size_t set_index(const std::size_t offset,
                          const std::size_t index) noexcept {
        assert(m_index_size > 0);
        assert(offset < m_len);
        return _set_index(offset, index);
    }

    [[nodiscard]]
    std::size_t get_index(const std::size_t offset) const noexcept {
        assert(offset < m_len);
        return _get_index(offset);
    }

    [[nodiscard]]
    std::generator<const std::size_t> iter() const noexcept {
        std::size_t offset = 0;

        while (true) {
            if (offset >= m_len) {
                break;
            }

            const auto index = get_index(offset);
            ++offset;

            co_yield index;
        }
    }

private:
    std::size_t set_index_with_index_size(const std::size_t offset,
                                          const std::size_t index_size,
                                          const std::size_t indices_per_u64,
                                          const std::size_t index) noexcept {
        assert(index_size > 0);
        assert(64 / index_size == indices_per_u64);

        std::size_t& target_u64 = m_storage[offset / indices_per_u64];

        const auto target_offset = (offset % indices_per_u64) * index_size;
        const auto mask =
            std::numeric_limits<std::size_t>::max() >> (64 - index_size);
        const auto old_index = (target_u64 >> target_offset) & mask;

        target_u64 &= ~(mask << target_offset);
        target_u64 |= index << target_offset;

        return old_index;
    }

    std::size_t _set_index(const std::size_t offset,
                           const std::size_t index) noexcept {
        const std::size_t indices_per_u64 = m_indices_per_u64;
        std::size_t& target_u64 = m_storage[offset / indices_per_u64];

        const auto target_offset = (offset % indices_per_u64) * m_index_size;
        const auto old_index = (target_u64 >> target_offset) & m_mask;

        target_u64 &= ~(m_mask << target_offset);
        target_u64 |= index << target_offset;

        return old_index;
    }

    [[nodiscard]]
    std::size_t _get_index(const std::size_t offset) const noexcept {
        if (m_index_size == 0) {
            return 0;
        }

        const std::size_t indices_per_u64 = m_indices_per_u64;
        const std::size_t target_u64 = m_storage[offset / indices_per_u64];

        const auto target_offset = (offset % indices_per_u64) * m_index_size;
        return (target_u64 >> target_offset) & m_mask;
    }

    std::vector<uint64_t> m_storage;
    uint64_t m_mask{};
    std::size_t m_index_size{};
    std::size_t m_len{};
    uint8_t m_indices_per_u64{};
};

template <std::size_t INLINE_PALETTE_THRESHOLD, PaletteValue T>
class PaletteVec {
public:
    PaletteVec() = default;

    PaletteVec(const T value, const std::size_t len) {
        HybridPalette<INLINE_PALETTE_THRESHOLD, T> palette{};

        const typename HybridPalette<INLINE_PALETTE_THRESHOLD, T>::PaletteEntry
            entry{
                .value = value,
                .count = len,
            };

        const auto& [index, index_size] = palette.insert_new(entry);

        assert(index == 0);
        AlignedIndexBuffer buffer{};

        if (index_size.has_value()) {
            buffer.set_index_size(*index_size, std::nullopt);
        }
        buffer.zeroed(len);

        m_palette = palette;
        m_buffer = buffer;
    }

    explicit PaletteVec(const std::vector<T>& vec) noexcept {
        for (const auto& value : vec) {
            push(value);
        }
    }

    [[nodiscard]]
    std::size_t len() const noexcept {
        return m_buffer.len();
    }

    [[nodiscard]]
    bool is_empty() const noexcept {
        return m_buffer.is_empty();
    }

    [[nodiscard]]
    std::size_t unique_values() const noexcept {
        return m_palette.len();
    }

    void push(const T& value) noexcept {
        const auto& pair = m_palette.get_mut_by_value(value);

        if (pair.has_value()) {
            const auto& [entry, index] = *pair;
            ++entry->count;
            m_buffer.push_index(index);
        } else {
            const typename HybridPalette<INLINE_PALETTE_THRESHOLD,
                                         T>::PaletteEntry entry{
                .value = value,
                .count = 1,
            };

            const auto& [index, new_index_size] = m_palette.insert_new(entry);

            if (new_index_size.has_value()) {
                m_buffer.set_index_size(*new_index_size, std::nullopt);
            }

            m_buffer.push_index(index);
        }
    }

    [[nodiscard]]
    std::optional<T> pop() noexcept {
        const auto index = m_buffer.pop_index();
        if (!index.has_value()) {
            return std::nullopt;
        }

        const auto entry = m_palette.get_mut_by_index(*index);
        if (entry == nullptr) {
            return std::nullopt;
        }

        --entry->count;

        if (entry->count == 0) {
            m_palette.mark_as_unused(*index);
        }

        const auto& value = entry->value;
        return value;
    }

    void set(const std::size_t offset, const T& value) noexcept {
        const auto old_index_size = m_palette.index_size();
        const auto& pair = m_palette.get_mut_by_value(value);

        if (pair.has_value()) {
            if (old_index_size == 0) {
                return;
            }

            const auto& [entry, index] = *pair;
            const auto old_index = m_buffer.set_index(offset, index);

            if (old_index != index) {
                ++entry->count;
                auto old_entry = *m_palette.get_mut_by_index(old_index);
                --old_entry.count;

                if (old_entry.count == 0) {
                    m_palette.mark_as_unused(old_index);
                }
            }

            return;
        }

        const typename HybridPalette<INLINE_PALETTE_THRESHOLD, T>::PaletteEntry
            entry{
                .value = value,
                .count = 1,
            };

        const auto& [new_index, new_index_size] = m_palette.insert_new(entry);

        if (new_index_size.has_value()) {
            m_buffer.set_index_size(*new_index_size, std::nullopt);
        }

        const auto old_index = m_buffer.set_index(offset, new_index);
        auto old_entry = *m_palette.get_mut_by_index(old_index);

        --old_entry.count;
        if (old_entry.count == 0) {
            m_palette.mark_as_unused(old_index);
        }
    }

    const T* get(const std::size_t offset) const noexcept {
        if (offset >= m_buffer.len()) {
            return nullptr;
        }

        const auto index = m_buffer.get_index(offset);
        return &m_palette.get_by_index(index)->value;
    }

    void optimize() noexcept {
        const auto& mapping = m_palette.optimize();
        const auto new_index_size = m_palette.index_size();
        m_buffer.set_index_size(new_index_size, mapping);
    }

    std::generator<const T&> iter() const noexcept {
        for (const auto idx : m_buffer.iter()) {
            const auto& entry = *m_palette.get_by_index(idx);
            co_yield entry.value;
        }
    }

private:
    HybridPalette<INLINE_PALETTE_THRESHOLD, T> m_palette{};
    AlignedIndexBuffer m_buffer;
};

}  // namespace compression
