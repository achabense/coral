#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
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
            throw 0;
        }
    }

    // To prevent adding enumerators to the namespace.
    namespace _cellT_ {
        enum /*class*/ cellT : uint8_t { states = 3, min = 0, max = states - 1 };
    }
    using _cellT_::cellT;

    // TODO: this is causing trouble. (states == 2 is only used for early testing but have to be considered in many algos...)
    // 2 for debugging.
    static_assert(cellT::states == 2 || cellT::states == 3);

    struct envT {
        cellT data[9] = {}; // {q, w, e, a, s, d, z, x, c}

        friend bool operator==(const envT& a, const envT& b) { return !std::memcmp(a.data, b.data, sizeof(data)); }

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

    // Note: the encoding must not be changed, as `to_string(ruleT)` depends on it (actually the order of `isotropic::groups()`).
    // (It's possible to remove the dependency by defining a separate sampling order, but that's costly...)
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

        // Note: defaulted operator== for C-arrays is buggy in MSVC.
        friend bool operator==(const codeT_to& a, const codeT_to& b)
            requires(std::is_same_v<T, cellT>)
        {
            return !std::memcmp(a.m_data, b.m_data, sizeof(m_data));
        }
    };

    // Never returned by value (too large to be stack-allocated).
    using ruleT = codeT_to<cellT>;

    inline cellT increase(const cellT c) { return cellT((c + 1) % cellT::states); }

    inline void increase(ruleT& rule, const groupT group) {
        assert(!group.empty());
        rule.fill(group, increase(rule[group[0]]));
    }

    // TODO: also support totalistic rules.
    class isotropic {
        codeT_to<uint16_t> m_map{};
        codeT m_data[codeT::states]{}; // Permutation of all codeT.
        std::vector<groupT> m_groups{};

    public:
        isotropic(const isotropic&) = delete;
        isotropic& operator=(const isotropic&) = delete;

        // int k() const { return m_groups.size(); }
        static constexpr int k = cellT::states == 2 ? 102 : 2862;

        std::span<const groupT> groups() const { return m_groups; }
        groupT group_for(const codeT c) const { return m_groups[m_map[c]]; }
        codeT head_for(const codeT c) const { return m_groups[m_map[c]][0]; }

        static const isotropic& get() {
            static isotropic iso{};
            return iso;
        }

    private:
        explicit isotropic() noexcept /*terminates (supposed to be impossible)*/ {
            m_groups.reserve(k);

            m_map.fill(UINT16_MAX);
            int data_pos = 0;
            for_each_code([&](const codeT code) {
                if (m_map[code] == UINT16_MAX) {
                    const envT env1 = decode(code), env2 = env1.rotate();
                    const envT env3 = env2.rotate(), env4 = env3.rotate();
                    codeT group[8]{encode(env1), encode(env1.diag()), encode(env2), encode(env2.diag()),
                                   encode(env3), encode(env3.diag()), encode(env4), encode(env4.diag())};
                    std::ranges::sort(group); // v `ranges::unique()` returns range to be excluded.
                    const std::ranges::subrange unique(group, std::ranges::unique(group).begin());
                    assert(!unique.empty());

                    m_groups.push_back(groupT(m_data + data_pos, unique.size()));
                    const int index = m_groups.size() - 1;
                    for (const codeT c : unique) {
                        m_map[c] = index;
                        m_data[data_pos++] = c;
                    }
                    assert(m_map[code] != UINT16_MAX);
                }
            });

            // const int _k_ = m_groups.size(); // 2 ~ 102, 3 ~ 2862.
            verify(data_pos == codeT::states);
            verify(m_groups.size() == k);
        }
    };

    inline void test_iso(const isotropic& iso = isotropic::get()) {
        for (const groupT group : iso.groups()) {
            for (const codeT c : group) {
                verify(iso.group_for(c).data() == group.data());
            }
        }
    }

    // Note: sizeof(std::mt19937) = 5000 in MSVC (about twice the necessary size)...
    // Related: https://github.com/microsoft/STL/issues/5198
    using randT = std::mt19937;

    using freqT = std::array<int, cellT::states>; // [i] for cellT(i).

    inline cellT rand_cell_other_than(const cellT c, randT& rand) {
        if constexpr (cellT::states == 2) {
            return cellT(!c);
        } else {
            static constexpr cellT values[3][2]{{cellT(1), cellT(2)}, {cellT(0), cellT(2)}, {cellT(0), cellT(1)}};
            return values[c][rand() & 1];
        }
    }

    inline auto rand_cell_from(randT& rand) {
        return [&rand] { return cellT(rand() % cellT::states); };
    }

    inline auto rand_cell_from(randT& rand, const freqT freq) {
        // Also, the sum should not be too large.
        if constexpr (cellT::states == 2) {
            assert(freq[0] >= 0 && freq[1] >= 0 && (freq[0] + freq[1] > 0));
            return [&rand, c0 = freq[0], c1 = freq[0] + freq[1]] {
                const int i = rand() % c1;
                return cellT(i < c0 ? 0 : 1);
            };
        } else {
            assert(freq[0] >= 0 && freq[1] >= 0 && freq[2] >= 0 && (freq[0] + freq[1] + freq[2] > 0));
            return [&rand, c0 = freq[0], c1 = freq[0] + freq[1], c2 = freq[0] + freq[1] + freq[2]] {
                const int i = rand() % c2;
                return cellT(i < c0 ? 0 : i < c1 ? 1 : 2);
            };
        }
    }

    inline auto rand_p_from(randT& rand, const double p) {
        assert(0.0 <= p && p <= 1.0);
        return [&rand, cp = int(65536 * std::clamp(p, 0.0, 1.0))] { return (rand() & 65535) < cp; };
    }

    inline void rand_rule(ruleT& rule, randT& rand, const isotropic& iso = isotropic::get()) {
        const auto rand_cell = rand_cell_from(rand);
        for (const groupT group : iso.groups()) {
            rule.fill(group, rand_cell());
        }
    }

    inline void rand_rule(ruleT& rule, randT& rand, const freqT freq, const isotropic& iso = isotropic::get()) {
        const auto rand_cell = rand_cell_from(rand, freq);
        for (const groupT group : iso.groups()) {
            rule.fill(group, rand_cell());
        }
    }

    inline void rand_rule_totalistic(ruleT& rule, randT& rand, const freqT freq,
                                     const isotropic& iso = isotropic::get()) {
        const auto rand_cell = rand_cell_from(rand, freq);
        // TODO: workaround for lack of totalistic groups (necessary for random-access editing in the gui).
        std::optional<cellT> values[(cellT::states - 1) * 8 + 1][cellT::states]{};
        for (const groupT group : iso.groups()) {
            envT env = decode(group[0]);
            const cellT s = std::exchange(env.data[4], cellT(0));
            int sum = 0;
            for (const cellT c : env.data) {
                sum += c;
            }
            auto& v = values[sum][s];
            rule.fill(group, v ? *v : *(v = rand_cell()));
        }
    }

    inline void randomize_p(ruleT& rule, randT& rand, const double p, const isotropic& iso = isotropic::get()) {
        const auto rand_cell = rand_cell_from(rand);
        const auto rand_p = rand_p_from(rand, p);
        for (const groupT group : iso.groups()) {
            if (rand_p()) {
                rule.fill(group, rand_cell());
            }
        }
    }

    // Note: ranges::shuffle doesn't work with vector<bool> (though std::shuffle does).
    inline void randomize_c(ruleT& rule, randT& rand, const int c, const isotropic& iso = isotropic::get()) {
        std::vector<char> chosen(isotropic::k, false);
        std::ranges::fill_n(chosen.data(), std::clamp(c, 0, isotropic::k), true);
        std::ranges::shuffle(chosen, rand);
        for (int i = 0; const groupT group : iso.groups()) {
            if (chosen[i++]) {
                rule.fill(group, rand_cell_other_than(rule[group[0]], rand));
            }
        }
    }

    namespace _misc_ {
        constexpr char to_char(const std::array<cellT, 3> arr) {
            static_assert(pow_cs(3) <= 27);
            const int val = arr[0] * pow_cs(0) + arr[1] * pow_cs(1) + arr[2] * pow_cs(2);
            assert(0 <= val && val < pow_cs(3));
            return "0123456789abcdefghijklmnopqrstuvwxyz"[val]; // Longer than necessary.
        }

        constexpr bool is_char(const char ch) {
            if constexpr (cellT::states == 2) {
                return '0' <= ch && ch <= '7';
            } else {
                constexpr char max_ch = to_char({cellT::max, cellT::max, cellT::max});
                return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= max_ch);
            }
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

        inline constexpr int required_size = (isotropic::k + 2) / 3;

        // Due to the huge size, there's no plan to support saving arbitrary 3-state rules.
        // '(' and ')' are not part of the rule-string, but for easier recovery (e.g. when pasted into another string by accident).
        inline std::string to_string(const ruleT& rule, const isotropic& iso = isotropic::get()) {
            std::string str(required_size + 2, '\0');
            str.front() = '(';
            str.back() = ')';
            std::array<cellT, 3> arr{};
            int pos = 1, cnt = 0;
            for (const groupT group : iso.groups()) {
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

        inline void from_string_unchecked(ruleT& rule, const std::string_view str,
                                          const isotropic& iso = isotropic::get()) {
            assert(str.size() == required_size);
            assert(std::ranges::all_of(str, is_char));
            std::array<cellT, 3> arr{};
            int pos = 0, cnt = 3;
            for (const groupT group : iso.groups()) {
                if (cnt == 3) {
                    arr = from_char(str[pos++]);
                    cnt = 0;
                }
                rule.fill(group, arr[cnt++]);
            }
        }

        inline std::string_view extract_string(std::string_view& str) {
            while (str.size() >= required_size) {
                if (!is_char(str[0])) {
                    str.remove_prefix(1);
                } else {
                    int j = 1;
                    while (j < required_size && is_char(str[j])) {
                        ++j;
                    }
                    if (j == required_size) {
                        const char* pos = str.data();
                        str.remove_prefix(required_size);
                        return std::string_view(pos, required_size);
                    } else {
                        str.remove_prefix(j);
                    }
                }
            }
            return {};
        }
    } // namespace _misc_

    using _misc_::to_string;

    inline bool from_string(ruleT& rule, std::string_view& str, const isotropic& iso = isotropic::get()) {
        const auto extr = _misc_::extract_string(str);
        if (!extr.empty()) {
            _misc_::from_string_unchecked(rule, extr, iso);
            return true;
        };
        return false;
    }

    inline bool from_string(std::vector<ruleT>& rule, std::string_view& str, const isotropic& iso = isotropic::get()) {
        const auto extr = _misc_::extract_string(str);
        if (!extr.empty()) {
            _misc_::from_string_unchecked(rule.emplace_back(), extr, iso);
            return true;
        };
        return false;
    }

    inline bool from_string(ruleT& rule, std::string_view&& str, const isotropic& iso = isotropic::get()) {
        return from_string(rule, str, iso);
    }

    inline void test_saving(randT& rand, const isotropic& iso = isotropic::get()) {
        std::unique_ptr<ruleT[]> rules(new ruleT[2]{});
        ruleT &a = rules[0], &b = rules[1];
        rand_rule(a, rand, iso);
        const std::string str1 = to_string(a, iso);
        const std::string str2 = "abc   " + str1 + "     defg";
        verify(from_string(b, str1, iso) && b == a);
        verify(from_string(b, str2, iso) && b == a);
        // TODO: should also test with predefined rule string.
        // a = ...; const char* str3 = ...; verify(from_string(b, str3, iso) && b == a);
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

        // 2025/12/25.
        // Optimized version of run(); hope nothing can be worse than this in this project...
        void run_ex(const ruleT& rule) {
            assert(cellT::states == 3); // && encoding ~ q*1 + w*3 + e*9 + a*27 + ...
            assert(m_data);
            if (!m_data) {
                return;
            }

            // l*1 + (q*3 + w*9) <- l (q*1 + w*3 + e*9) r -> (w*1 + e*3) + r*9
            // [0] -> [x-1]: (q*1 + w*3 + e*9) -> (w*1 + e*3) ~ "/3" (current impl)
            // [0] <- [x-1]: (q*1 + w*3 + e*9) -> (q*3 + w*9) ~ "%9*3" (also a single mapping; maybe more efficient?)
            using packT = uint8_t;
            static constexpr packT div3[27]{0, 0, 0, 1, 1, 1, 2, 2, 2, //
                                            3, 3, 3, 4, 4, 4, 5, 5, 5, //
                                            6, 6, 6, 7, 7, 7, 8, 8, 8};

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
                packT p3 = left * 3 + line[0] * 9; // [0, 27)
                for (int x = 0; x < xm1; ++x) {
                    pack[x] = p3 = div3[p3] + line[x + 1] * 9;
                }
                pack[xm1] = div3[p3] + right * 9;
            };
            fill_p3(p3_prev, data + m_size.x * (m_size.y - 1));
            fill_p3(p3_curr, data);
            for (int y = 0; y < m_size.y; ++y) {
                cellT* const line = data + m_size.x * y;
                cellT* const next = y == m_size.y - 1 ? first_line : line + m_size.x;
                const cellT left = next[xm1], right = next[0];
                packT p3 = left * 3 + next[0] * 9; // [0, 27)
                for (int x = 0; x < xm1; ++x) {
                    p3_next[x] = p3 = div3[p3] + next[x + 1] * 9;
                    line[x] = rule[codeT(p3_prev[x] + p3_curr[x] * 27 + p3_next[x] * 729)];
                }
                p3_next[xm1] = div3[p3] + right * 9;
                line[xm1] = rule[codeT(p3_prev[xm1] + p3_curr[xm1] * 27 + p3_next[xm1] * 729)];

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
        for_each_code([&identity = *identity](const codeT c) { identity[c] = decode(c, 4); });

        const tileT a = rand_tile({123, 123}, rand);
        tileT b = a;
        b.run(*identity);
        verify(b == a);
        if constexpr (cellT::states == 3) {
            b.run_ex(*identity);
            verify(b == a);
        }
    }

    inline void test_all(randT& rand, const isotropic& iso = isotropic::get()) {
        test_encoding();
        test_iso(iso);
        test_saving(rand, iso);
        test_run(rand);
    }

} // namespace iso3
