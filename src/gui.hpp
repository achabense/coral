#pragma once

#include <ctime>
#include <unordered_map>

#include "dear_imgui.hpp"

#include "rule.hpp"

using iso3::cellT;
using iso3::codeT;
using iso3::randT;
using iso3::ruleT;
using iso3::tileT;

// TODO: support specifying colors? (Shouldn't affect `render_code()`.)
// (So for example, may map two values to the same color to see how the 3rd value appear in the pattern.)
inline ImU32 color_for(const cellT c) {
    if constexpr (cellT::states == 2) {
        static constexpr ImU32 colors[3]{IM_COL32_BLACK, IM_COL32_WHITE};
        return colors[c];
    } else {
        static constexpr ImU32 colors[3]{IM_COL32_BLACK, IM_COL32(128, 128, 128, 255), IM_COL32_WHITE};
        return colors[c];
    }
}

extern ImTextureID texture_create(const tileT& /*not-empty*/);
extern void texture_update(ImTextureID /*not-null*/, const tileT& /*same-size*/);
extern void texture_destroy(ImTextureID /*not-null*/);

class tile_with_texture : no_copy {
    tileT m_tile = {};
    ImTextureID m_texture = {};
    bool m_sync = false;

public:
    void swap(tile_with_texture& other) noexcept {
        m_tile.swap(other.m_tile);
        std::swap(m_texture, other.m_texture);
        std::swap(m_sync, other.m_sync);
    }

    tile_with_texture() = default;
    tile_with_texture(tile_with_texture&& other) noexcept { swap(other); }
    tile_with_texture& operator=(tile_with_texture&& other) noexcept {
        swap(other);
        return *this;
    }
    ~tile_with_texture() {
        if (m_texture) {
            texture_destroy(m_texture);
        }
    }

    ImTextureID texture() {
        assert(!m_tile.empty());
        if (!m_sync) {
            m_sync = true;
            if (m_texture) {
                texture_update(m_texture, m_tile);
            } else {
                m_texture = texture_create(m_tile);
            }
        }
        return m_texture;
    }

    void assign(const tileT& tile) {
        m_sync = false;
        if (m_texture && m_tile.size() != tile.size()) { // Cannot reuse.
            texture_destroy(std::exchange(m_texture, {}));
        }
        m_tile = tile;
    }
    void assign(tileT&&) = delete; // Currently not needed.

    bool empty() const { return m_tile.empty(); }
    void clear() {
        m_sync = false;
        if (m_texture) {
            texture_destroy(std::exchange(m_texture, {}));
        }
        m_tile.clear();
    }

    void run(const ruleT& rule, int step = 1) {
        assert(!m_tile.empty() && step >= 0);
        if (step > 0) {
            m_sync = false;
            for (int i = 0; i < step; ++i) {
                if constexpr (cellT::states == 3) {
                    m_tile.run_ex(rule);
                } else {
                    m_tile.run(rule);
                }
            }
        }
    }
};

// Note: SDL is unable to create texture with height = 9 * codeT::states (19683 when cellT::states = 3).
template <bool add_rect>
inline void render_code(const codeT code, const int scale, const ImVec2 min, ImDrawList& draw) {
    const auto env = iso3::decode(code);
    for (int y = 0, i = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const ImVec2 p_min = min + ImVec2(x, y) * scale;
            const ImVec2 p_max = min + ImVec2(x + 1, y + 1) * scale;
            draw.AddRectFilled(p_min, p_max, color_for(env.data[i++]));
            if constexpr (add_rect) {
                // TODO: make thickness of inner border -> 1?
                // TODO: IM_COL32(180, 180, 180, 255) -> ImGuiCol_Border?
                draw.AddRect(p_min, p_max, IM_COL32(180, 180, 180, 255));
            }
        }
    }
}

inline void code_image(const codeT code, const int scale = 7) {
    constexpr ImVec2 border = {1, 1};
    ImGui::Dummy(ImVec2(scale * 3, scale * 3) + border * 2);
    if (ImGui::IsItemVisible()) {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        render_code<false>(code, scale, min + border, draw);
        draw.AddRect(min, max, IM_COL32(180, 180, 180, 255));
    }
}

inline int code_image_width(const int scale = 7) { return scale * 3 + 2 /*border*/; }

template <class T>
class ring_buffer : no_copy {
    // There seems no way to enforce std::vector to allocate memory for exactly n objects...
    std::unique_ptr<T[]> m_data{};
    int m_capacity{};
    int m_0 = 0; // Position of [0].
    int m_size = 0;

public:
    // All {}-initialized.
    explicit ring_buffer(int capacity) : m_data(new T[capacity]{}), m_capacity(capacity) { assert(capacity > 0); }

    T& at(int pos) { return m_data[(m_0 + pos) % m_capacity]; }
    const T& at(int pos) const { return m_data[(m_0 + pos) % m_capacity]; }

    int capacity() const { return m_capacity; }
    int size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    // Not responsible for values (should be assigned by callers).
    // void clear() { m_size = 0; }
    void resize(int size) {
        assert(0 <= size && size <= m_capacity);
        m_size = std::clamp(size, 0, m_capacity);
    }
    T& emplace_back() {
        if (m_size < m_capacity) {
            ++m_size;
        } else if (++m_0 == m_capacity) { // Discard the oldest value.
            m_0 = 0;
        }
        assert(1 <= m_size && m_size <= m_capacity && 0 <= m_0 && m_0 < m_capacity);
        return at(m_size - 1);
    }
};

// Always non-empty. (Initially contains a single {}.)
template <class T>
class record_for : no_copy {
    ring_buffer<T> m_data;
    int m_pos = 0; // Current position.

public:
    explicit record_for(int capacity) : m_data(capacity) { m_data.emplace_back(); }

    bool has_next() const { return m_pos < m_data.size() - 1; }
    bool has_prev() const { return m_pos > 0; }
    void to_next() { m_pos = std::min(m_pos + 1, m_data.size() - 1); }
    void to_prev() { m_pos = std::max(m_pos - 1, 0); }

    const T& get() { return m_data.at(m_pos); }
    void set(const T& val) {
        assert(0 <= m_pos && m_pos < m_data.size());
        if (m_data.at(m_pos) != val) {
            m_data.resize(m_pos + 1); // Discard values after the current pos.
            m_data.emplace_back() = val;
            m_pos = m_data.size() - 1;
        }
    }
};

enum class ctrl_mode { ctrl, no_ctrl };
enum class repeat_mode { repeat, no_repeat, down };
inline bool test_key(ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat) {
    return ((ctrl == ctrl_mode::ctrl) == ImGui::GetIO().KeyCtrl) &&
           (repeat == repeat_mode::down ? ImGui::IsKeyDown(key)
                                        : ImGui::IsKeyPressed(key, repeat == repeat_mode::repeat));
}

// TODO: improve; -> class?
inline auto create_shortcut(bool enabled) {
    return [enabled](ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat, ImGuiID highlight = ImGui::GetItemID()) mutable {
        if (enabled && test_key(ctrl, key, repeat) && !imgui_IsItemDisabled()) {
            enabled = false; // Only one key can be triggered.
            if (highlight) {
                imgui_HighlightItem(highlight);
            }
            return true;
        }
        return false;
    };
}

class preview_group : no_copy {
    struct blobT {
        tile_with_texture tile{};
        bool active = false;
    };

    std::unordered_map<int /*id*/, blobT> m_blobs{};
    const tileT m_init{iso3::rand_tile({256, 196}, randT{0})};

    ImVec2 texture_size() const { return ImVec2(m_init.size().x, m_init.size().y); }

public:
    void begin() {
        assert(std::ranges::all_of(m_blobs, [](const auto& blob) { return !blob.second.active; }));
    }
    void end() {
        // I've no clue whether this is truly allowed...
        // When used by algorithms, a "Predicate" is not allowed to modify input.
        // https://eel.is/c++draft/algorithms.requirements
        // std::erase_if(map) uses the same term, but it's defined by equivalent behavior (which allows modification).
        // https://eel.is/c++draft/unord.map.erasure
        std::erase_if(m_blobs, [](auto& blob) { return !std::exchange(blob.second.active, false); });
    }

    // !!TODO: fragile workaround for restarting sub-groups.
    // (Should either be stack-like (push/pop) or controlled by speedT (should rename).)
    bool restart_all = false;

    struct speedT {
        bool pause = false;
        int step = 1;
        int interval = 0; // Frame based. TODO: -> time based?

        // Using a multiple of lcm(2,3) to prevent trivial strobing. (For 3-state rules, both 2 and 3 can be strobing period.)
        static constexpr int max_step = 12;
        bool tick() const { return interval == 0 || (ImGui::GetFrameCount() % (interval + 1)) == 0; }
    };

    ImVec2 image_size() const { return texture_size() + ImVec2{2, 2} /*border*/; }

    // TODO: uncertain about the design (condition & op).
    // ctrl + left-click -> select
    // ctrl + c -> copy
    // ctrl/press + scroll -> change zoom level
    // right-click -> op menu (select/copy)
    // no-ctrl + s/d/f -> extra step
    // `id` must be unique per group & imgui's id stack (for unique `GetItemID()`).
    void image(const ruleT& rule, const int id, const speedT& speed, shared_popup& m_popup, extra_message& m_message,
               std::optional<ruleT>* to_rule = nullptr) {
        constexpr ImVec2 border = {1, 1};
        // TODO: is it possible to distinguish items with no id from the background (e.g. IsBgHovered())?
        // ImGui::Dummy(texture_size() + border * 2);
        if (!imgui_DummyEx(texture_size() + border * 2, ":|", id)) {
            return;
        }
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        if (!imgui_IsItemVisibleEx(0.15f)) {
            draw.AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
            return;
        }

        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = hovered && ImGui::IsItemActive();
        const char op = (hovered && (pressed || !ImGui::IsAnyItemActive())) ? test_op() : '\0';
        if (pressed) {
            imgui_LockScroll(); // Ctrl also disables scrolling.
        }

        blobT& blob = m_blobs[id];
        assert(!blob.active);
        blob.active = true;
        tile_with_texture& tile = blob.tile;
        // TODO: whether to run before displaying?
        // TODO: whether to always skip the init state?
        if (tile.empty() || restart_all) {
            tile.assign(m_init);
            tile.run(rule, speed.step);
        } else if (op && (op == 'S' || op == 'D' || op == 'F')) {
            tile.run(rule, op == 'S' ? speed.step : op == 'D' ? 1 : /*'F'*/ speed.max_step);
        } else {
            const bool pause = speed.pause || (!ctrl && pressed);
            if (!pause && speed.tick()) {
                tile.run(rule, speed.step);
            }
        }

        // TODO: the current ownership works, but is still risky...
        // `tile` must not be cleared / resized after this (in this frame).
        const ImTextureID texture = tile.texture();
        draw.AddImage(texture, min + border, max - border);
        if (hovered && ImGui::IsMousePosValid() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            display_in_tooltip(texture, texture_size(), ImGui::GetMousePos() - min - border,
                               ctrl || pressed /*-> scroll to change zoom level*/);
        }
        // TODO: working but need to use a separate id for popup in the future.
        draw.AddRect(min, max, hovered || m_popup.opened(id) ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Border));

        const bool can_select = to_rule;
        bool select = false, copy = op == 'C';
        if (hovered && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_popup.open(id);
        }
        if (m_popup.begin_popup(id, /*lock-scroll*/ true)) {
            select |= can_select && ImGui::Selectable("Select");
            copy |= ImGui::Selectable("Copy");
            m_popup.end_popup();
        }
        if (can_select && hovered && ctrl) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            select |= ImGui::IsItemActivated(); // Clicked.
        }
        if (select) {
            *to_rule = rule;
            m_message.set("Selected.");
        }
        if (copy) {
            ImGui::SetClipboardText(iso3::to_string(rule).c_str());
            m_message.set("Copied.");
        }
    }

    void dummy() const {
        ImGui::Dummy(image_size());
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(ImGuiCol_Border));
    }

private:
    static char test_op() {
        assert(!imgui_IsItemDisabled());
        return test_key(ctrl_mode::no_ctrl, ImGuiKey_S, repeat_mode::repeat)   ? 'S'
               : test_key(ctrl_mode::no_ctrl, ImGuiKey_D, repeat_mode::repeat) ? 'D'
               : test_key(ctrl_mode::no_ctrl, ImGuiKey_F, repeat_mode::down)   ? 'F'
               : test_key(ctrl_mode::ctrl, ImGuiKey_C, repeat_mode::no_repeat) ? 'C'
                                                                               : '\0';
    }

    static void display_in_tooltip(ImTextureID texture, ImVec2 texture_size, ImVec2 center, bool can_scale) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {1, 1}); // {0, 0} will overlap.
        if (ImGui::BeginTooltip()) {
            static int scale = 3;
            if (can_scale) {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0) {
                    scale = std::clamp(wheel < 0 /*down*/ ? scale - 1 : scale + 1, 2, 4);
                }
            }

            // 12 ~ lcm(2,3,4).
            const ImVec2 size = imgui_Min(ImVec2(16 * 12, 12 * 12) / scale, texture_size);
            const ImVec2 min = imgui_Max({0, 0}, imgui_Floor(center - size / 2));
            ImGui::Dummy(size * scale);
            if (ImGui::IsItemVisible()) {
                ImGui::GetWindowDrawList()->AddImage(texture, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                     min / texture_size, (min + size) / texture_size);
                // No need for border (will be rendered by the window).
            }
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar();
    }
};

// !!TODO: (& rule_loader) unable to restart this window with shortcuts. (Each window should have their own pause-state / shortcuts.)
// TODO: support configurable page size.
// TODO: support more generating modes.
// TODO: support fixing target rule (e.g. fix to all-0 rule).
class rule_generator : no_copy {
    static constexpr int page_x = 3, page_y = 2, page_size = page_x * page_y;

    ring_buffer<ruleT> m_rules{page_size * 20};
    int m_pos = 0; // Position for the first rule in the page.

    enum class rand_mode { p, c };
    rand_mode m_mode = rand_mode::p;
    int m_dist = 10; // c ~ dist, p ~ dist / 100

    bool restart = false;

public:
    void display(bool& open, const ruleT& rel, randT& m_rand, preview_group& m_preview, const int starting_id,
                 const preview_group::speedT& speed, shared_popup& m_popup, extra_message& m_message,
                 std::optional<ruleT>& to_rule) {
        if (!open) {
            return;
        }

        // TODO: using fixed name for convenience (technically should be specified per object).
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        if (ImGui::Begin("Generate", &open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            header(rel, m_rand);
            ImGui::Separator();

            m_preview.restart_all = std::exchange(restart, false);
            const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
            constexpr int per_line = page_x;
            for (int i = 0; i < page_size; ++i) {
                if (i % per_line != 0) {
                    ImGui::SameLine(0, item_spacing);
                } else if (i != 0) {
                    // ImGui::Separator();
                }
                if (m_pos + i < m_rules.size()) {
                    m_preview.image(m_rules.at(m_pos + i), starting_id + i, speed, m_popup, m_message, &to_rule);
                } else {
                    m_preview.dummy();
                }
            }
            m_preview.restart_all = false;
        }
        ImGui::End();
    }

private:
    void header(const ruleT& rel, randT& m_rand) {
        assert(m_rules.empty() ? m_pos == 0 : 0 <= m_pos && m_pos < m_rules.size());
        auto shortcut =
            create_shortcut(!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                                                                ImGuiFocusedFlags_NoPopupHierarchy));

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.resize(0); // Won't actually free up memory.
            m_pos = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_pos == 0);
        if (ImGui::Button("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
            restart = true;
            m_pos = std::max(0, m_pos - page_size);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(">>>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
            restart = true;
            const int page_end = m_pos + page_size;
            if (m_rules.empty() || m_rules.size() <= page_end) {
                const int num = m_rules.size() < page_end ? page_end - m_rules.size() : page_size;
                for (int i = 0; i < num; ++i) {
                    // iso3::rand_rule(m_rules.emplace_back(), m_rand, {64, 4, 1});
                    if (m_mode == rand_mode::c) {
                        iso3::randomize_c(m_rules.emplace_back() = rel, m_rand, m_dist);
                    } else {
                        iso3::randomize_p(m_rules.emplace_back() = rel, m_rand, m_dist / 100.0);
                    }
                }
                assert(m_rules.size() >= page_size);
                m_pos = m_rules.size() - page_size;
            } else {
                m_pos += page_size;
            }
        }
        // !!TODO: should explain in UI...
        ImGui::SameLine();
        if (ImGui::RadioButton("P", m_mode == rand_mode::p)) {
            m_mode = rand_mode::p;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("C", m_mode == rand_mode::c)) {
            m_mode = rand_mode::c;
        }
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "##Dist", m_dist, 0, 100, true);
    }
};

// TODO: support loading from file.
// TODO: support resizing the window.
class rule_loader : no_copy {
    std::vector<ruleT> m_rules{};

    bool restart = false;

public:
    void display(bool& open, preview_group& m_preview, const int starting_id, const preview_group::speedT& speed,
                 shared_popup& m_popup, extra_message& m_message, std::optional<ruleT>& to_rule) {
        if (!open) {
            return;
        }

        // TODO: using fixed name for convenience (technically should be specified per object).
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        if (ImGui::Begin("Load", &open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            header(m_message);
            ImGui::Separator();

            const ImVec2 item_spacing = ImGui::GetStyle().ItemSpacing;
            const ImVec2 child_size =
                m_preview.image_size() * ImVec2(2, 2) + item_spacing + ImVec2(ImGui::GetStyle().ScrollbarSize, 0);
            if (ImGui::BeginChild("Rules", child_size) && !m_rules.empty()) {
                m_preview.restart_all = std::exchange(restart, false);
                const int total = m_rules.size();
                constexpr int per_line = 2;
                for (int i = 0; i < total; ++i) {
                    if (i % per_line != 0) {
                        ImGui::SameLine(0, item_spacing.x);
                    } else if (i != 0) {
                        // ImGui::Separator();
                    }
                    m_preview.image(m_rules[i], starting_id + i, speed, m_popup, m_message, &to_rule);
                }
                m_preview.restart_all = false;
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

private:
    void header(extra_message& m_message) {
        auto shortcut =
            create_shortcut(!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                                                                ImGuiFocusedFlags_NoPopupHierarchy));

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.clear();
            m_rules.shrink_to_fit();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // TODO: support multiple lists (instead of replacing the existing one).
        if (imgui_DoubleClickButton("Paste") || shortcut(ctrl_mode::ctrl, ImGuiKey_V, repeat_mode::no_repeat)) {
            std::vector<ruleT> rules = extract_rules(ImGui::GetClipboardText());
            if (!rules.empty()) {
                restart = true;
                m_rules.swap(rules);
            } else {
                m_message.set("No rules.");
            }
        }
    }

    static std::vector<ruleT> extract_rules(std::string_view str, int reserve = 8, int max = 100) {
        std::vector<ruleT> rules{};
        rules.reserve(reserve);
        for (int i = 0; i < max; ++i) {
            if (!iso3::from_string(rules, str)) {
                break;
            }
        }
        return rules;
    }
};

// TODO: support setting to game-of-life.
// TODO: support configurable init state.
// TODO: support adding to temp list.
class main_data : no_copy {
    using isotropic = iso3::isotropic;

    record_for<ruleT> m_rule{20};
    preview_group m_preview{};
    rule_loader m_loader{};
    rule_generator m_generator{};
    randT m_rand{uint32_t(std::time(0))}; // TODO: can be static var.
    shared_popup m_popup{};
    extra_message m_message{};

    // Too large to be stack-allocated.
    main_data() { iso3::test_all(m_rand); }

public:
    static auto make_unique() { return std::unique_ptr<main_data>(new main_data{}); }

    // TODO: should not be public.
    preview_group::speedT speed{.pause = false, .step = 1, .interval = 0};

    std::optional<ruleT> to_rule = std::nullopt; // TODO: use ruleT + bool instead?
    bool reset = false;
    bool restart = false;

    bool open_load = false;
    bool open_generate = false;

    bool has_prev() const { return m_rule.has_prev(); }
    bool has_next() const { return m_rule.has_next(); }
    bool to_prev = false;
    bool to_next = false;

    void display() {
        flush();
        m_popup.set_popup_id("Popup");
        m_popup.begin();
        m_preview.begin();

        m_loader.display(open_load, m_preview, 20000, speed, m_popup, m_message, to_rule);
        m_generator.display(open_generate, m_rule.get(), m_rand, m_preview, 10000, speed, m_popup, m_message, to_rule);

        // TODO: slightly wasteful. (Can reuse memory by making `record_for::get()` return non-const ref, but that's risky.)
        std::unique_ptr<ruleT> temp_rule(new ruleT{m_rule.get()});
        ruleT& rule = *temp_rule;
        m_preview.restart_all = std::exchange(restart, false);

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        int preview_index = 0;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_image_width() + item_spacing); // For alignment.
        m_preview.image(rule, preview_index++, speed, m_popup, m_message, nullptr);

        ImGui::Separator();

        // Note: should not be set globally, as this also affects sliders...
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18); // To make the scrollbar easier to click.
        const bool child = ImGui::BeginChild("Groups");
        ImGui::PopStyleVar();
        if (child) {
            // std::optional<codeT> to_locate = std::nullopt;
            int to_locate = -1;
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_popup.open(-100);
            }
            if (m_popup.opened(-100) /*micro optimization*/ &&
                m_popup.begin_popup(-100, !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                                  ImGuiHoveredFlags_AllowWhenBlockedByPopup))) {
                select_group(to_locate, m_rand);
                m_popup.end_popup();
            }

            const int group_spacing = item_spacing * 3;
            const int group_width = code_image_width() + item_spacing + m_preview.image_size().x;
            const int avail_width = ImGui::GetContentRegionAvail().x;
            const int per_line = std::max((avail_width + group_spacing) / (group_width + group_spacing), 1);
            // TODO: workaround for displaying values; doesn't look very nice & the meaning is not obvious...
            constexpr int cell_width = 6;
            const auto render_cell = [&draw = *ImGui::GetWindowDrawList()](const cellT v, const ImVec2 min) {
                const ImVec2 max = min + ImVec2(cell_width, cell_width);
                if (ImGui::IsRectVisible(min, max)) {
                    draw.AddRectFilled(min, max, color_for(v));
                    draw.AddRect(min, max, IM_COL32(180, 180, 180, 255));
                }
            };

            // TODO: -> separate pages (instead of a single scrollable page)?
            int group_index = 0;
            for (const auto& group : isotropic::get().groups()) {
                if (group_index % per_line != 0) {
                    ImGui::SameLine(0, group_spacing);
                } else if (group_index != 0) {
                    ImGui::Separator();
                }
                ++group_index;

                const codeT group_0 = group[0];
                code_image(group_0);
                const ImVec2 code_image_max = ImGui::GetItemRectMax();
                render_cell(rule[group_0], {code_image_max.x - cell_width, code_image_max.y + cell_width});
                if (to_locate == group_0) {
                    ImGui::SetScrollHereY(0);
                }
                if (ImGui::BeginItemTooltip()) {
                    for (bool first = true; const codeT c : group) {
                        if (!std::exchange(first, false)) {
                            ImGui::SameLine(0, item_spacing);
                        }
                        code_image(c);
                    }
                    ImGui::EndTooltip();
                }
                ImGui::SameLine(0, item_spacing);
                ImGui::BeginGroup();
                for (int i = 0; i < cellT::states - 1; ++i) {
                    iso3::increase(rule, group);
                    m_preview.image(rule, preview_index++, speed, m_popup, m_message, &to_rule);
                    if (ImGui::IsItemVisible()) {
                        const ImVec2 image_min = ImGui::GetItemRectMin();
                        const ImVec2 image_max = ImGui::GetItemRectMax();
                        render_cell(rule[group_0],
                                    {code_image_max.x - cell_width,
                                     image_min.y + std::floor((image_max.y - image_min.y - cell_width) / 2) - 1});
                    }
                }
                ImGui::EndGroup();
                iso3::increase(rule, group); // Restored.
            }
        }
        ImGui::EndChild();

        m_preview.restart_all = false;
        assert(rule == m_rule.get());

        m_preview.end();
        m_popup.end();
        m_message.display();
    }

private:
    void flush() {
        // TODO: whether to compare the rule (same -> no restart)?
        if (std::exchange(reset, false)) {
            to_rule.emplace();
        }
        if (to_rule) {
            m_rule.set(*to_rule);
            to_rule.reset();
            restart = true;
        }
        if (std::exchange(to_prev, false)) {
            m_rule.to_prev();
            restart = true;
        }
        if (std::exchange(to_next, false)) {
            m_rule.to_next();
            restart = true;
        }
    }

    static void select_group(int& to_locate, randT& m_rand) {
        auto shortcut =
            create_shortcut(!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                                                                ImGuiFocusedFlags_NoPopupHierarchy));

        // TODO: working but technically should belong to object.
        static iso3::envT cells{};
        static record_for<iso3::envT> record{10}; // Only for "Locate" and "Random".
        {
            ImGui::BeginDisabled(!record.has_prev());
            if (ImGui::SmallButton("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
                record.to_prev();
                cells = record.get();
                to_locate = iso3::encode(cells);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            // TODO: enhance to ">>>"?
            ImGui::BeginDisabled(!record.has_next());
            if (ImGui::SmallButton(">>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
                record.to_next();
                cells = record.get();
                to_locate = iso3::encode(cells);
            }
            ImGui::EndDisabled();

            ImGui::Separator();
        }
        const int scale = ImGui::GetFrameHeight();
        ImGui::InvisibleButton("Cells", ImVec2(scale * 3, scale * 3),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemVisible()) {
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            render_code<true>(iso3::encode(cells), scale, min, *ImGui::GetWindowDrawList());
            if (ImGui::IsItemActive() && ImGui::IsItemHovered() && ImGui::IsMousePosValid()) {
                const ImVec2 pos = imgui_Floor((ImGui::GetMousePos() - min) / scale);
                if (const int i = pos.y * 3 + pos.x; 0 <= i && i < 9) {
                    cellT& cell = cells.data[i];
                    // Left-click -> change value; right-click -> use clicked value.
                    static cellT v = {};
                    if (ImGui::IsItemActivated()) {
                        v = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? iso3::increase(cell) : /*rclick*/ cell;
                    }
                    cell = v;
                }
            }
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button("Locate")) {
            const codeT group_0 = isotropic::get().group_for(iso3::encode(cells))[0];
            cells = iso3::decode(group_0);
            record.set(cells);
            to_locate = group_0;
        }
        if (ImGui::Button("Random")) {
            const auto& groups = isotropic::get().groups();
            const codeT group_0 = groups[m_rand() % groups.size()][0];
            cells = iso3::decode(group_0);
            record.set(cells);
            to_locate = group_0;
        }
        ImGui::EndGroup();
    }
};

inline void frame_main(main_data& data) {
    // TODO: document scrolling behavior & support scrolling with up/down.
    // (Drag from scrollbar or press ctrl to scroll to position; otherwise will scroll by page size.)
    // Related: https://github.com/ocornut/imgui/issues/8002
    ImGui::GetIO().ConfigScrollbarScrollByPage = !ImGui::GetIO().KeyCtrl;
    ImGui::GetStyle().HoverDelayShort = 0.20f; // For `ImGuiHoveredFlags_ForTooltip` (0.15s by default).
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(24, 24, 24, 255));
    ImGui::GetStyle().Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(12, 12, 12, 255));

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    if (ImGui::Begin("Main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        // TODO: uncertain about the design (condition & op).
        auto shortcut =
            create_shortcut(!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                                                                ImGuiFocusedFlags_NoPopupHierarchy));

        ImGui::Checkbox("Load", &data.open_load);
        ImGui::SameLine();
        ImGui::Checkbox("Generate", &data.open_generate);
        ImGui::SameLine();
        data.reset |= imgui_DoubleClickButton("Reset");
        ImGui::SameLine();
        ImGui::Text("%d fps", (int)std::round(ImGui::GetIO().Framerate));
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_prev());
        data.to_prev |= ImGui::Button("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_next());
        data.to_next |=
            ImGui::Button(">>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat);
        ImGui::EndDisabled();
        ImGui::SameLine();
        // TODO: support pausing/restarting individual windows.
        data.restart |= ImGui::Button("Restart") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_R, repeat_mode::no_repeat);
        ImGui::SameLine();
        ImGui::Checkbox("Pause", &data.speed.pause);
        if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_Space, repeat_mode::no_repeat)) {
            data.speed.pause = !data.speed.pause;
        }
        constexpr int step_min = 1, step_max = decltype(data.speed)::max_step;
        constexpr int interval_min = 0, interval_max = 20;
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "Step", data.speed.step, step_min, step_max, true);
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "Interval", data.speed.interval, interval_min, interval_max, true);
        {
            ImGui::PushID("Step"); // Workaround to highlight the step buttons.
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_1, repeat_mode::repeat, ImGui::GetID("-"))) {
                --data.speed.step;
            }
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_2, repeat_mode::repeat, ImGui::GetID("+"))) {
                ++data.speed.step;
            }
            ImGui::PopID();
            ImGui::PushID("Interval");
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_3, repeat_mode::repeat, ImGui::GetID("-"))) {
                --data.speed.interval;
            }
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_4, repeat_mode::repeat, ImGui::GetID("+"))) {
                ++data.speed.interval;
            }
            ImGui::PopID();
            data.speed.step = std::clamp(data.speed.step, step_min, step_max);
            data.speed.interval = std::clamp(data.speed.interval, interval_min, interval_max);
        }

        ImGui::Separator();
        data.display();
    };
    ImGui::End();
}
