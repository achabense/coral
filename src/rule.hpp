#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Isotropic 3-state rules.
namespace iso3 {
    inline void verify(bool v) {
        if (!v) {
            assert(false);
            std::abort();
        }
    }

    // To prevent adding enumerators to the namespace.
    namespace _cellT_ {
        enum /*class*/ cellT : uint8_t { states = 3, min = 0, max = states - 1 };
    }
    using _cellT_::cellT;

    inline cellT next(const cellT c) { return cellT(c == cellT::max ? 0 : c + 1); }

    inline cellT prev(const cellT c) { return cellT(c == 0 ? cellT::max : c - 1); }

    struct envT {
        cellT data[9] = {}; // {q, w, e, a, s, d, z, x, c}

        friend bool operator==(const envT& a, const envT& b) { return !std::memcmp(a.data, b.data, sizeof(data)); }

        cellT& s() { return data[4]; }
        const cellT& s() const { return data[4]; }

        envT diag() const {
            const auto& [q, w, e, a, s, d, z, x, c] = data;
            return {q, a, z, w, s, x, e, d, c};
        }
        envT rotate() const {
            const auto& [q, w, e, a, s, d, z, x, c] = data;
            return {z, a, q, x, s, w, c, d, e};
        }
    };

    consteval int pow_cs(int pow) {
        assert(0 <= pow && pow <= 9);
        int v = 1;
        for (int p = 0; p < pow; ++p) {
            v *= cellT::states;
        }
        return v;
    }

    namespace _codeT_ {
        enum /*class*/ codeT : uint16_t { states = pow_cs(9), min = 0, max = states - 1 };
    }
    using _codeT_::codeT;

    // Note: the encoding must not be changed, as `to_string(ruleT)` depends on it (actually the order of `isotropic().groups()`).
    // TODO: remove the dependency by defining a separate sampling sequence?
    inline codeT encode(const envT env) {
        const auto& [q, w, e, a, s, d, z, x, c] = env.data;
        const int v = q * pow_cs(0) + w * pow_cs(1) + e * pow_cs(2) + a * pow_cs(3) + s * pow_cs(4) + d * pow_cs(5) +
                      z * pow_cs(6) + x * pow_cs(7) + c * pow_cs(8);
        assert(0 <= v && v <= codeT::max);
        return codeT(v);
    }

    inline envT decode(const codeT c) {
        assert(0 <= c && c <= codeT::max);
        envT env{};
        for (int i = 0, v = c; i < 9; ++i) {
            env.data[i] = cellT(v % cellT::states);
            v /= cellT::states;
        }
        return env;
    }

    inline cellT decode(const codeT c, const int index) {
        assert(0 <= c && c <= codeT::max);
        assert(0 <= index && index <= 8);
        static constexpr int pows[9]{pow_cs(0), pow_cs(1), pow_cs(2), pow_cs(3), pow_cs(4),
                                     pow_cs(5), pow_cs(6), pow_cs(7), pow_cs(8)};
        return cellT((c / pows[index]) % cellT::states);
    }

    inline cellT decode_s(const codeT c) { return decode(c, 4); }

    inline void for_each_code(const auto fn)
        requires(requires { fn(codeT(0)); })
    {
        for (int i = 0; i < codeT::states; ++i) {
            fn(codeT(i));
        }
    }

    inline void test_encoding() {
        for_each_code([](const codeT c) {
            const envT env = decode(c);
            verify(encode(env) == c);
            for (int i = 0; i < 9; ++i) {
                verify(env.data[i] == decode(c, i));
            }
        });
    }

    using groupT = std::span<const codeT>; // Non-empty.

    template <class T>
    class codeT_to {
        T m_data[codeT::states]{};

    public:
        void fill(const T v) { std::ranges::fill(m_data, v); }
        void fill(const groupT group, const T v) {
            for (const codeT c : group) {
                m_data[c] = v;
            }
        }

        T& operator[](const codeT c) { return m_data[c]; }
        const T& operator[](const codeT c) const { return m_data[c]; }

        std::span<T> data() { return m_data; }
        std::span<const T> data() const { return m_data; }

        // Note: defaulted operator== for C-arrays is buggy in MSVC.
        friend bool operator==(const codeT_to& a, const codeT_to& b)
            requires(std::is_same_v<T, cellT>)
        {
            return !std::memcmp(a.m_data, b.m_data, sizeof(m_data));
        }
    };

    // Never returned by value (too large to be stack-allocated).
    using ruleT = codeT_to<cellT>;

    // TODO: actually just partition. (`contains()` assumes same value for each group; working for now.)
    class setT {
        codeT_to<uint16_t> m_map{};
        codeT m_data[codeT::states]{}; // Permutation of all codeT.
        std::vector<groupT> m_groups{};

    public:
        setT(const setT&) = delete;
        setT& operator=(const setT&) = delete;

        int k() const { return m_groups.size(); }
        std::span<const groupT> groups() const { return m_groups; }
        groupT group_for(const codeT c) const { return m_groups[m_map[c]]; }

        bool contains(const ruleT& rule) const {
            for (const groupT group : m_groups) {
                const cellT v = rule[group[0]];
                for (const codeT code : group.subspan(1)) {
                    if (v != rule[code]) {
                        return false;
                    }
                }
            }
            return true;
        }

        template <void (&fill_map)(codeT_to<uint16_t>&)>
        static const setT& global() {
            static const setT set(fill_map);
            return set;
        }

    private:
        explicit setT(void (&fill_map)(codeT_to<uint16_t>&)) {
            fill_map(m_map);
            int k = 0;
            std::vector<int> count{}; // [m_map[code]]
            {
                const int max = *std::ranges::max_element(m_map.data());
                verify(max <= 5000);
                count.resize(max + 1);
                std::vector<uint16_t> normalize(max + 1, UINT16_MAX /*unmapped*/);
                for_each_code([&](const codeT code) {
                    uint16_t& v = normalize[m_map[code]];
                    if (v == UINT16_MAX) {
                        v = k++;
                    }
                    m_map[code] = v;
                    ++count[v];
                });
                // `k` ~ number of groups
            }
            m_groups.resize(k);
            for (int prefix = 0, i = 0; i < k; ++i) {
                m_groups[i] = {m_data + prefix, (size_t)count[i]};
                count[i] = std::exchange(prefix, prefix + count[i]);
                // `count` ~ pos to fill
            }
            for_each_code([&](const codeT code) { m_data[count[m_map[code]]++] = code; });

            // Related: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3136r1.html ("Retiring niebloids")
            verify(std::ranges::all_of(m_groups, std::ranges::is_sorted /*see above*/));
            verify(std::ranges::is_sorted(m_groups, [](const groupT a, const groupT b) { return a[0] < b[0]; }));
            for (const groupT group : m_groups) {
                for (const codeT c : group) {
                    verify(group_for(c).data() == group.data());
                }
            }
        }
    };

    inline const setT& isotropic() {
        return setT::global<*+[](codeT_to<uint16_t>& m_map) {
            m_map.fill(UINT16_MAX /*unmapped*/);
            int k = 0;
            for_each_code([&](const codeT code) {
                if (m_map[code] == UINT16_MAX) {
                    const int v = k++;
                    envT env = decode(code);
                    for (int i = 0; i < 4; ++i) {
                        m_map[encode(env)] = m_map[encode(env.diag())] = v;
                        env = env.rotate();
                    }
                }
            });
        }>();
    }

    // Count-based.
    inline const setT& totalistic() {
        return setT::global<*+[](codeT_to<uint16_t>& m_map) {
            for_each_code([&](const codeT code) {
                static_assert(cellT::states == 3);
                const envT env = decode(code);
                const cellT s = env.s();
                int count[3]{};
                for (const cellT c : env.data) {
                    ++count[c];
                }
                --count[s];
                // No need for [2] (<-> [0]+[1]).
                m_map[code] = s + count[0] * cellT::states + count[1] * cellT::states * 9;
            });
        }>();
    }

    // TODO: whether to support sum-based set (sum < count < iso)?
    [[deprecated]] inline const setT& totalitsic_opt() {
        return setT::global<*+[](codeT_to<uint16_t>& m_map) {
            for_each_code([&](const codeT code) {
                const envT env = decode(code);
                const cellT s = env.s();
                int sum = 0;
                for (const cellT c : env.data) {
                    sum += c;
                }
                sum -= s;
                m_map[code] = s + sum * cellT::states;
            });
        }>();
    }

    inline void test_iso() {
        static_assert(cellT::states == 3);
        verify(isotropic().k() == 2862); // 954*3
        // verify(totalistic().k() == 135); // 45*3
    }

    inline bool is_isotropic(const ruleT& rule) { return isotropic().contains(rule); }

    inline void to_zero(ruleT& rule) { rule.fill(cellT(0)); }

    inline void to_identity(ruleT& rule) {
        for_each_code([&rule](const codeT c) { rule[c] = decode_s(c); });
    }

    // TODO: support generating "clean" / random rules equivalent to gol / arbitrary isotropic 2-state rules?
    // For an arbitrary 2-state rule (like gol), there are a huge number of 3-state rules equivalent to it.
    // For example, by treating cellT(0) & (1) as "0" and (2) as "1", a 3-state rule is equivalent to a 2-state rule as long as it maps cells to either (0) or (1) for cases where "0" is expected (and (2) for cases where "1" is expected).
    // (The same applies to other state mappings like cellT(0) ~ "0", (1) & (2) ~ "1".)

    // This is more special as it emulates gol at two levels (cellT(2) ~ "living", but also emulates another level of gol in the "dead" area (cellT(0) ~ "truly dead")).
    inline void to_life(ruleT& rule) {
        static_assert(cellT::states == 3); // (Designed for 3 but also works for 2.)
        for (const groupT group : isotropic().groups()) {
            const envT env = decode(group[0]);
            const cellT s = env.s();
            const int count_2 = std::ranges::count(env.data, 2) - (s == 2);
            const int count_12 = count_2 + std::ranges::count(env.data, 1) - (s == 1);
            rule.fill(group, count_2 == 3 || (count_2 == 2 && s == 2)     ? cellT(2)
                             : count_12 == 3 || (count_12 == 2 && s != 0) ? cellT(1) // TODO: vs `s == 1`?
                                                                          : cellT(0));
        }
    }

    inline void to_next(ruleT& rule, const groupT group) {
        // rule.fill(group, next(rule[group[0]])); // Not unconditionally reversible.
        for (const codeT c : group) {
            rule[c] = next(rule[c]);
        }
    }

    inline void to_prev(ruleT& rule, const groupT group) {
        for (const codeT c : group) {
            rule[c] = prev(rule[c]);
        }
    }

    // Note: sizeof(std::mt19937) = 5000 in MSVC (about twice the necessary size)...
    // Related: https://github.com/microsoft/STL/issues/5198
    using randT = std::mt19937;

    using freqT = std::array<int, cellT::states>; // [i] for cellT(i).

    inline auto rand_cell_from(randT& rand) {
        return [&rand] { return cellT(rand() % cellT::states); };
    }

    inline auto rand_cell_from(randT& rand, const freqT freq) {
        // Also, the sum should not be too large.
        static_assert(cellT::states == 3);
        assert(freq[0] >= 0 && freq[1] >= 0 && freq[2] >= 0 && (freq[0] + freq[1] + freq[2] > 0));
        return [&rand, c0 = freq[0], c1 = freq[0] + freq[1], c2 = freq[0] + freq[1] + freq[2]] {
            const int i = rand() % c2;
            return cellT(i < c0 ? 0 : i < c1 ? 1 : 2);
        };
    }

    // (Isotropic but too random.)
    inline void rand_rule(ruleT& rule, randT& rand) {
        const auto rand_cell = rand_cell_from(rand);
        for (const groupT group : isotropic().groups()) {
            rule.fill(group, rand_cell());
        }
    }

    inline void rand_rule(ruleT& rule, randT& rand, const freqT freq, const std::span<const groupT> groups) {
        const auto rand_cell = rand_cell_from(rand, freq);
        for (const groupT group : groups) {
            rule.fill(group, rand_cell());
        }
    }

    // Note: ranges::shuffle doesn't work with vector<bool> (though std::shuffle does).
    inline void randomize_n(ruleT& rule, randT& rand, const int n, const std::span<const groupT> groups) {
        const int k = groups.size();
        std::vector<char> chosen(k, false);
        std::ranges::fill_n(chosen.data(), std::clamp(n, 0, k), true);
        std::ranges::shuffle(chosen, rand);
        for (int i = 0; i < k; ++i) {
            if (chosen[i]) {
                const groupT group = groups[i];
                static_assert(cellT::states == 3);
                if (rand() & 1) {
                    to_next(rule, group);
                } else {
                    to_prev(rule, group);
                }
            }
        }
    }

    inline void randomize_p(ruleT& rule, randT& rand, const double p, const std::span<const groupT> groups) {
        const int n = std::binomial_distribution<int>(groups.size(), std::clamp(p, 0.0, 1.0))(rand);
        randomize_n(rule, rand, n, groups);
    }

    namespace _misc_ {
        constexpr bool is_012(const char ch) { return ch == '0' || ch == '1' || ch == '2'; }

        constexpr char to_char(const std::array<cellT, 3> arr) {
            static_assert(cellT::states == 3 && pow_cs(3) == 27);
            const int val = arr[0] * pow_cs(0) + arr[1] * pow_cs(1) + arr[2] * pow_cs(2);
            assert(0 <= val && val < pow_cs(3));
            return "0123456789abcdefghijklmnopqrstuvwxyz"[val]; // Longer than necessary.
        }

        constexpr bool is_char(const char ch) {
            constexpr char max_ch = to_char({cellT::max, cellT::max, cellT::max});
            return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= max_ch);
        }

        constexpr std::array<cellT, 3> from_char(const char ch) {
            int val = '0' <= ch && ch <= '9'   ? ch - '0' //
                      : 'a' <= ch && ch <= 'z' ? 10 + (ch - 'a')
                                               : 100;
            assert(0 <= val && val < pow_cs(3));
            std::array<cellT, 3> arr{};
            for (int i = 0; i < 3; ++i) {
                arr[i] = cellT(val % cellT::states);
                val /= cellT::states;
            }
            return arr;
        }

        inline constexpr int required_size = (2862 /*isotropic().k()*/ + 2) / 3;

        // Due to the huge size, there's no plan to support saving arbitrary 3-state rules.
        // '(' and ')' are not part of the rule-string, but for easier recovery (e.g. when pasted into another string by accident).
        // TODO: support additional prefix / postfix (e.g. "\n")?
        inline std::string to_string(const ruleT& rule) {
            assert(is_isotropic(rule));
            std::string str(required_size + 2, '\0');
            str.front() = '(';
            str.back() = ')';
            std::array<cellT, 3> arr{};
            int pos = 1, cnt = 0;
            for (const groupT group : isotropic().groups()) {
                arr[cnt] = rule[group[0]];
                if (++cnt == 3) {
                    str[pos++] = to_char(arr);
                    arr.fill(cellT(0));
                    cnt = 0;
                }
            }
            if (cnt != 0) {
                str[pos++] = to_char(arr);
            }
            assert(pos == str.size() - 1);
            return str;
        }

        inline void from_string_unchecked(ruleT& rule, const std::string_view str) {
            assert(str.size() == required_size);
            assert(std::ranges::all_of(str, is_char));
            std::array<cellT, 3> arr{};
            int pos = 0, cnt = 3;
            for (const groupT group : isotropic().groups()) {
                if (cnt == 3) {
                    arr = from_char(str[pos++]);
                    cnt = 0;
                }
                rule.fill(group, arr[cnt++]);
            }
        }

        inline std::string_view extract_string(std::string_view& str) {
            const char *pos = str.data(), *end = pos + str.size();
            int len = 0;
            while (pos != end) {
                len = is_char(*pos++) ? len + 1 : 0;
                if (len == required_size) {
                    break;
                }
            }
            str = {pos, end};
            if (len == required_size) {
                return {pos - required_size, pos};
            } else {
                return {};
            }
        }
    } // namespace _misc_

    using _misc_::to_string;

    inline bool extract_rule(ruleT& rule, std::string_view& str) {
        const auto extr = _misc_::extract_string(str);
        if (!extr.empty()) {
            _misc_::from_string_unchecked(rule, extr);
            return true;
        };
        return false;
    }

    inline bool extract_rule(std::vector<ruleT>& vec, std::string_view& str) {
        const auto extr = _misc_::extract_string(str);
        if (!extr.empty()) {
            _misc_::from_string_unchecked(vec.emplace_back(), extr);
            return true;
        };
        return false;
    }

    inline bool extract_rule(ruleT& rule, std::string_view&& str) { return extract_rule(rule, str); }

    inline void test_saving(randT& rand) {
        std::unique_ptr<ruleT[]> rules(new ruleT[2]{});
        ruleT &a = rules[0], &b = rules[1];
        rand_rule(a, rand);
        const std::string str1 = to_string(a);
        const std::string str2 = "abc   " + str1 + "     defg";
        verify(extract_rule(b, str1) && b == a);
        verify(extract_rule(b, str2) && b == a);
        // TODO: should also test with predefined rule string.
        // a = ...; const char* str3 = ...; verify(extract_rule(b, str3) && b == a);
    }

    // Format: [012*abc]{9}|[012i] (*~0/1/2, a~1/2, b~0/2, c~0/1, i~center cell)
    // Multiple assignments are applied in order without checking for contradictions.
    inline bool extract_values(ruleT& rule, const std::string_view str) {
        using _misc_::is_012;
        const auto apply = [&rule, &iso = isotropic()](const char* str) {
            const auto fill = [&rule, ch = str[10]](const groupT group) {
                rule.fill(group, ch == 'i' ? decode_s(group[0]) : cellT(ch - '0'));
            };
            if (std::ranges::all_of(str, str + 9, is_012)) { // For performance.
                envT env{};
                for (int i = 0; i < 9; ++i) {
                    env.data[i] = cellT(str[i] - '0');
                }
                fill(iso.group_for(encode(env)));
            } else {
                const auto match = [str](const codeT code) {
                    // `ranges::equal` is inconvenient here. (Why is there no `std::equal_n`...)
                    return std::equal(str, str + 9, decode(code).data, [](const char ch, const cellT cell) {
                        return ch == '*' ? true : is_012(ch) ? cell == ch - '0' : cell != ch - 'a' /*abc*/;
                    });
                };
                for (const groupT group : iso.groups()) {
                    if (std::ranges::any_of(group, match)) {
                        fill(group);
                    }
                }
            }
        };

        bool written = false;
        const char *pos = str.data(), *end = pos + str.size();
        int len = 0;
        while (pos != end) {
            // Note: will ignore "valid" substrings like "00120120120|1" ("120120120|1" is valid, but the counter has been reset earlier.)
            const char ch = *pos++;
            if (len <= 8   ? is_012(ch) || ch == '*' || ch == 'a' || ch == 'b' || ch == 'c'
                : len == 9 ? ch == '|'
                           : is_012(ch) || ch == 'i' /*len == 10*/) {
                ++len;
            } else {
                len = 0;
            }
            if (len == 11) {
                len = 0;
                written = true;
                apply(pos - 11);
            }
        }
        return written;
    }

    struct sizeT {
        int x, y;

        friend bool operator==(const sizeT&, const sizeT&) = default;

        int xy() const {
            assert((x == 0 && y == 0) || (x > 0 && y > 0));
            return x * y;
        }
    };

    class tileT {
        std::unique_ptr<cellT[]> m_data = {};
        sizeT m_size = {};

    public:
        bool _prepare(const sizeT size) {
            assert((size.x == 0 && size.y == 0) || (size.x > 0 && size.y > 0));
            assert(!m_data && m_size.x == 0 && m_size.y == 0);
            if (size.x > 0 && size.y > 0) {
                m_data.reset(new cellT[size.xy()] /*uninitialized*/);
                m_size = size;
                return true;
            }
            return false;
        }

        void swap(tileT& other) noexcept {
            std::swap(m_data, other.m_data);
            std::swap(m_size, other.m_size);
        }
        void assign(tileT&& other) noexcept { swap(other); }

        tileT() noexcept = default;
        tileT(tileT&& other) noexcept { swap(other); }
        tileT(const tileT& other) {
            if (_prepare(other.m_size)) {
                std::ranges::copy_n(other.m_data.get(), other.m_size.xy(), m_data.get());
            }
        }

        explicit tileT(const sizeT size, const cellT c = cellT(0)) {
            if (_prepare(size)) {
                std::ranges::fill_n(m_data.get(), m_size.xy(), c);
            }
        }

        tileT& operator=(tileT&& other) noexcept {
            swap(other);
            return *this;
        }
        tileT& operator=(const tileT& other) {
            if (m_size != other.m_size) {
                assign(tileT(other));
            } else if (m_data) {
                std::ranges::copy_n(other.m_data.get(), other.m_size.xy(), m_data.get());
            }
            return *this;
        }

        bool empty() const { return !m_data; }
        sizeT size() const { return m_size; }
        int area() const { return m_size.xy(); }

        std::span<const cellT> data() const { return std::span<const cellT>(m_data.get(), m_size.xy()); }
        std::span<cellT> data() { return std::span<cellT>(m_data.get(), m_size.xy()); }

        friend bool operator==(const tileT& a, const tileT& b) {
            // Note: memcmp is not guaranteed to support (nullptr, nullptr, 0).
            static_assert(sizeof(cellT) == 1);
            return a.m_size == b.m_size && (!a.m_data || !std::memcmp(a.m_data.get(), b.m_data.get(), a.m_size.xy()));
        }

        void clear() noexcept {
            m_data = {};
            m_size = {};
        }
        void fill(const cellT c = cellT(0)) {
            if (m_data) {
                std::ranges::fill_n(m_data.get(), m_size.xy(), c);
            }
        }
        void resize(const sizeT size, const cellT c = cellT(0)) {
            if (m_size != size) {
                assign(tileT(size, c));
            }
        }

        // As torus space.
        void run(const ruleT& rule) {
            assert(m_data);
            if (!m_data) {
                return;
            }

            std::unique_ptr<cellT[]> buffer(new cellT[m_size.x * 3]{});
            cellT* const first_line = buffer.get();
            cellT* prev = first_line + m_size.x; // Relative to `line` in the loop.
            cellT* curr = prev + m_size.x;

            cellT* const data = m_data.get();
            std::ranges::copy_n(data, m_size.x, first_line);
            std::ranges::copy_n(data + m_size.x * (m_size.y - 1), m_size.x, prev);
            for (int y = 0; y < m_size.y; ++y) {
                cellT* const line = data + m_size.x * y;
                cellT* const next = y == m_size.y - 1 ? first_line : line + m_size.x;
                std::ranges::copy_n(line, m_size.x, curr);
                for (int x = 0; x < m_size.x; ++x) {
                    const int xl = x == 0 ? m_size.x - 1 : x - 1;
                    const int xr = x == m_size.x - 1 ? 0 : x + 1;
                    line[x] = rule[encode({prev[xl], prev[x], prev[xr], //
                                           curr[xl], curr[x], curr[xr], //
                                           next[xl], next[x], next[xr]})];
                }
                std::swap(prev, curr); // Then prev contains the data of this line.
            }
        }

        // 2025/12/25 & 2026/2/22.
        // Optimized version of run(); hope nothing can be worse than this in this project...
        void run_ex(const ruleT& rule) {
            static_assert(cellT::states == 3); // && encoding ~ q*1 + w*3 + e*9 + a*27 + ...
            assert(m_data);
            if (!m_data) {
                return;
            }

            // l*1 + (q*3 + w*9) <- l (q*1 + w*3 + e*9) r -> (w*1 + e*3) + r*9
            // [0] -> [x-1]: (q*1 + w*3 + e*9) -> (w*1 + e*3) ~ "/3" (old impl)
            // [0] <- [x-1]: (q*1 + w*3 + e*9) -> (q*3 + w*9) ~ "%9*3" (current impl; slightly faster)
            using packT = uint8_t;
            static constexpr packT slide[27]{0, 3, 6, 9, 12, 15, 18, 21, 24, //
                                             0, 3, 6, 9, 12, 15, 18, 21, 24, //
                                             0, 3, 6, 9, 12, 15, 18, 21, 24};

            std::unique_ptr<cellT[]> buffer_a(new cellT[m_size.x]{});
            std::unique_ptr<packT[]> buffer_b(new packT[m_size.x * 3]{});
            cellT* const first_line = buffer_a.get();
            packT* p3_prev = buffer_b.get(); // Relative to `line` in the loop.
            packT* p3_curr = p3_prev + m_size.x;
            packT* p3_next = p3_curr + m_size.x;

            cellT* const data = m_data.get();
            std::ranges::copy_n(data, m_size.x, first_line);
            const int xm1 = m_size.x - 1;
            auto fill_p3 = [xm1](packT* pack, const cellT* line) {
                const cellT left = line[xm1], right = line[0];
                packT p3 = line[xm1] + right * 3; // [0, 27)
                for (int x = xm1; x > 0; --x) {
                    pack[x] = p3 = slide[p3] + line[x - 1];
                }
                pack[0] = slide[p3] + left;
            };
            fill_p3(p3_prev, data + m_size.x * (m_size.y - 1));
            fill_p3(p3_curr, data);
            for (int y = 0; y < m_size.y; ++y) {
                cellT* const line = data + m_size.x * y;
                cellT* const next = y == m_size.y - 1 ? first_line : line + m_size.x;
                const cellT left = next[xm1], right = next[0];
                packT p3 = next[xm1] + right * 3; // [0, 27)
                for (int x = xm1; x > 0; --x) {
                    p3_next[x] = p3 = slide[p3] + next[x - 1];
                    line[x] = rule[codeT(p3_prev[x] + p3_curr[x] * 27 + p3_next[x] * 729)];
                }
                p3_next[0] = slide[p3] + left;
                line[0] = rule[codeT(p3_prev[0] + p3_curr[0] * 27 + p3_next[0] * 729)];

                packT* tmp = p3_prev; // Prepare for next line.
                p3_prev = p3_curr;
                p3_curr = p3_next;
                p3_next = tmp;
            }
        }
    };

    // TODO: support frequency for cellT.
    inline tileT rand_tile(const sizeT size, randT& rand) {
        tileT tile{};
        if (tile._prepare(size)) {
            std::ranges::generate(tile.data(), rand_cell_from(rand));
        }
        return tile;
    }

    inline tileT rand_tile(const sizeT size, randT&& rand) { return rand_tile(size, rand); }

    inline void test_run(randT& rand) {
        std::unique_ptr<ruleT> identity(new ruleT{});
        to_identity(*identity);

        const tileT a = rand_tile({123, 123}, rand);
        tileT b = a;
        b.run(*identity);
        verify(b == a);
        b.run_ex(*identity);
        verify(b == a);
    }

    inline void test_all(randT& rand) {
        test_encoding();
        test_iso();
        test_saving(rand);
        test_run(rand);
    }

} // namespace iso3
