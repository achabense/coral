#pragma once

#include <ctime>
#include <unordered_map>

#include "dear_imgui.hpp"

#include "rule.hpp"

#ifndef NDEBUG
inline constexpr bool debug_mode = true;
#else
inline constexpr bool debug_mode = false;
#endif // !NDEBUG

using iso3::cellT;
using iso3::codeT;
using iso3::randT;
using iso3::ruleT;
using iso3::tileT;

inline randT& get_rand() {
    static_var randT rand{uint32_t(std::time(0))};
    return rand;
}

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

extern void set_framerate();

extern ImTextureID texture_create(const tileT& /*not-empty*/);
extern void texture_update(ImTextureID /*not-null*/, const tileT& /*same-size*/);
extern void texture_destroy(ImTextureID /*not-null*/);

// TODO: fragile (working in `space_group` but relying on current use pattern); should explicitly deal with texture sharing...
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

// Note: SDL is unable to create texture with height == 9 * codeT::states (19683 when cellT::states == 3).
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
    void resize_ex(int size) {
        assert(0 <= size && size <= m_capacity);
        m_size = std::clamp(size, 0, m_capacity);
    }
    T& emplace_back_ex() {
        if (m_size < m_capacity) {
            ++m_size;
        } else if (++m_0 == m_capacity) { // Discard the oldest value.
            m_0 = 0;
        }
        assert(1 <= m_size && m_size <= m_capacity && 0 <= m_0 && m_0 < m_capacity);
        return at(m_size - 1);
    }

    void truncate(int max_size) { m_size = std::clamp(m_size, 0, max_size); }
    T& emplace_back(const T& val) { return emplace_back_ex() = val; }
};

// Always non-empty. (Initially contains a single {}.)
template <class T>
class record_for : no_copy {
    ring_buffer<T> m_data;
    int m_pos = 0; // Current position.

public:
    explicit record_for(int capacity) : m_data(capacity) { m_data.emplace_back_ex(); }

    bool has_prev() const { return m_pos > 0; }
    bool has_next() const { return m_pos < m_data.size() - 1; }
    void to_prev() { m_pos = std::max(m_pos - 1, 0); }
    void to_next() { m_pos = std::min(m_pos + 1, m_data.size() - 1); }
    void to_last() { m_pos = m_data.size() - 1; }

    const T& get() { return m_data.at(m_pos); }
    void set(const T& val) {
        assert(0 <= m_pos && m_pos < m_data.size());
        if (m_data.at(m_pos) != val) {
            m_data.truncate(m_pos + 1); // Discard values after the current pos.
            m_data.emplace_back(val);
            m_pos = m_data.size() - 1;
        }
    }
};

// To replace std::optional<ruleT> (for stack allocation).
class opt_rule : no_copy {
    std::unique_ptr<ruleT> m_rule{new ruleT{}};
    bool m_assigned = false;

public:
    explicit operator bool() const { return m_assigned; }
    const ruleT& operator*() const {
        assert(m_assigned);
        return *m_rule;
    }
    bool has_value() const { return m_assigned; }
    void reset() { m_assigned = false; }

    // Not responsible for the value.
    ruleT& emplace_ex() {
        m_assigned = true;
        return *m_rule;
    }

    ruleT& emplace(const ruleT& rule) { return emplace_ex() = rule; }
};

enum class ctrl_mode { ctrl, no_ctrl };
enum class repeat_mode { repeat, no_repeat, down };
inline bool test_key(ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat) {
    return ((ctrl == ctrl_mode::ctrl) == ImGui::GetIO().KeyCtrl) &&
           (repeat == repeat_mode::down ? ImGui::IsKeyDown(key)
                                        : ImGui::IsKeyPressed(key, repeat == repeat_mode::repeat));
}

// TODO: key filtering is under-specified. (Implicitly filtered per object, but ideally should be globally filtered.)
class shortcut_group : no_copy {
    bool m_enabled{};

public:
    explicit shortcut_group(bool enabled) : m_enabled(enabled) {}
    bool operator()(ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat, ImGuiID highlight = ImGui::GetItemID()) {
        if (m_enabled && test_key(ctrl, key, repeat) && !imgui_IsItemDisabled()) {
            m_enabled = false; // Only one key can be triggered.
            if (highlight) {
                imgui_HighlightItem(highlight);
            }
            return true;
        }
        return false;
    }
};

inline bool no_active_and_window_focused() {
    return !ImGui::IsAnyItemActive() &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
}

inline bool no_active_and_window_hovered() {
    return !ImGui::IsAnyItemActive() &&
           ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_NoPopupHierarchy);
}

// TODO: support pausing/restarting individual windows & applying s/d/f to group.
// (The shortcut conditions are intentionally not mentioned in UI.)
class space_settings : no_copy {
    bool restart = false;
    bool pause = false;
    int step = 1;
    int interval = 1; // Frame based. (1 ~ each frame.)
    int counter = 0;  // 0 ~ tick.

public:
    friend class space_group;

    void restart_all() { restart = true; }
    void begin() { counter = counter == 0 ? interval - 1 : counter - 1; }
    void end() { restart = false; }

    // Using a multiple of lcm(2,3) to prevent trivial strobing. (For 3-state rules, both 2 and 3 can be strobing period.)
    static constexpr int max_step = 12;
    bool tick() const { return counter == 0; }

    // `counter` is not strictly necessary, but makes ticking more stable when `interval` changes.
    // bool tick() const { return interval == 1 || (ImGui::GetFrameCount() % interval) == 0; }

    void header() {
        shortcut_group shortcut{no_active_and_window_hovered()}; // Instead of focused.

        if (ImGui::Button("Restart") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_R, repeat_mode::no_repeat)) {
            restart = true;
        }
        item_tooltip(
            "Restart  (R    ) ~ restart all spaces in this window.\n"
            "Pause    (Space) ~ turn on/off auto mode.\n"
            "Step     (1/2  ) ~ control the step of auto run.\n"
            "Interval (3/4  ) ~ control the freq of auto run (1 ~ each frame).\n\n"
            "(Try the shortcuts to see how these work.)");
        ImGui::SameLine();
        ImGui::Checkbox("Pause", &pause);
        if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_Space, repeat_mode::no_repeat)) {
            pause = !pause;
        }
        constexpr int step_min = 1, step_max = max_step;
        constexpr int interval_min = 1, interval_max = 20;
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "##Step", step, step_min, step_max, true, "Step: %d");
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "##Interval", interval, interval_min, interval_max, true,
                          "Interval: %d");
        {
            ImGui::PushID("##Step"); // Workaround for highlighting the step buttons.
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_1, repeat_mode::repeat, ImGui::GetID("-"))) {
                --step;
            }
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_2, repeat_mode::repeat, ImGui::GetID("+"))) {
                ++step;
            }
            ImGui::PopID();
            ImGui::PushID("##Interval");
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_3, repeat_mode::repeat, ImGui::GetID("-"))) {
                --interval;
            }
            if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_4, repeat_mode::repeat, ImGui::GetID("+"))) {
                ++interval;
            }
            ImGui::PopID();
            step = std::clamp(step, step_min, step_max);
            interval = std::clamp(interval, interval_min, interval_max);
        }
    }
};

// TODO: support configurable init state.
class space_group : no_copy {
    struct blobT {
        tile_with_texture tile{};
        bool active = false;
        bool skip_tick = false;
    };

    shared_popup m_popup{};
    std::unordered_map<int /*id*/, blobT> m_blobs{};
    const tileT m_init{iso3::rand_tile({256, 196}, randT{0})};

    ImVec2 texture_size() const { return ImVec2(m_init.size().x, m_init.size().y); }

public:
    // TODO: whether to begin/end popup?
    void begin(const char* str_id) {
        m_popup.begin(str_id);
        assert(std::ranges::all_of(m_blobs, [](const auto& blob) { return !blob.second.active; }));
    }
    void end() {
        // I've no clue whether this is truly allowed...
        // When used by algorithms, a "Predicate" is not allowed to modify input.
        // https://eel.is/c++draft/algorithms.requirements
        // std::erase_if(map) uses the same term, but it's defined by equivalent behavior (which allows modification).
        // https://eel.is/c++draft/unord.map.erasure
        std::erase_if(m_blobs, [](auto& blob) { return !std::exchange(blob.second.active, false); });
        m_popup.end();
    }

    shared_popup& popup() { return m_popup; }

    // (Shortcuts work when hovered && this-or-none-active.)
    // ctrl + left-click -> select
    // ctrl + c -> copy
    // ctrl/press + scroll -> change zoom level
    // right-click -> op menu (select/copy)
    // no-ctrl + s/d/f -> extra step
    // `id` (also used as popup id) must be unique per group & imgui's id stack (for unique `GetItemID()`).
    void image(const ruleT& rule, const int id, const space_settings& m_settings, opt_rule* to_rule = nullptr) {
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
        if (tile.empty() || m_settings.restart) {
            tile.assign(m_init);
            tile.run(rule, m_settings.step); // TODO: support setting init step?
            blob.skip_tick = true;
        } else if (op && (op == 'S' || op == 'D' || op == 'F')) {
            tile.run(rule, op == 'S' ? m_settings.step : op == 'D' ? 1 : /*'F'*/ m_settings.max_step);
            blob.skip_tick = true;
        } else if (m_settings.pause || (!ctrl && pressed)) {
            blob.skip_tick = true;
        } else if (m_settings.tick() && !std::exchange(blob.skip_tick, false)) {
            tile.run(rule, m_settings.step);
        }

        // TODO: relying on `tile` not be cleared / resized after AddImage() in this frame. Somewhat risky.
        // (The texture is also owned by imgui for this frame.)
        const ImTextureID texture = tile.texture();
        draw.AddImage(texture, min + border, max - border);
        if (hovered && ImGui::IsMousePosValid() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            display_in_tooltip(texture, texture_size(), ImGui::GetMousePos() - min - border,
                               ctrl || pressed /*-> scroll to change zoom level*/);
        }
        draw.AddRect(min, max, hovered || m_popup.opened(id) ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Border));
        // draw.AddRect(min, max, ImGui::GetColorU32(hovered || m_popup.opened(id) ? ImGuiCol_Text : ImGuiCol_Border));

        const bool can_select = to_rule;
        bool select = false, copy = op == 'C';
        if (hovered) {
            m_popup.open_on_idle_rclick(id);
        }
        if (m_popup.begin_popup(id, /*lock-scroll*/ true)) {
            select |= can_select && ImGui::Selectable("Select");
            item_tooltip("Select for editing.");
            copy |= ImGui::Selectable("Copy");
            item_tooltip("Copy the rule.");
            m_popup.end_popup();
        }
        if (can_select && hovered && ctrl) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            select |= ImGui::IsItemActivated(); // Clicked.
        }
        if (select) {
            to_rule->emplace(rule);
            set_message("Selected.");
        }
        if (copy) {
            ImGui::SetClipboardText(iso3::to_string(rule).c_str());
            set_message("Copied.");
        }
    }

    ImVec2 image_size() const { return texture_size() + ImVec2{2, 2} /*border*/; }

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
            static_var int scale = 3;
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

// TODO: support configurable page size.
// TODO: support more generating modes.
class rule_generator : no_copy {
    static constexpr int page_x = 3, page_y = 2, page_size = page_x * page_y;

    ring_buffer<ruleT> m_rules{page_size * 20};
    int m_pos = 0; // Position for the first rule in the page.

    enum class rand_mode { p, n };
    rand_mode m_mode = rand_mode::p;
    int m_dist = 10;  // p ~ possibility (percentage), n ~ exact dist
    opt_rule m_rel{}; // Relative to `m_rel ? *m_rel : rel`.

    space_settings m_settings{};

public:
    bool open = false;
    void display_if_open(const char* window_name, const ruleT& rel, space_group& m_spaces, const int starting_id,
                         opt_rule& to_rule) {
        if (!open) {
            return;
        }

        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        if (ImGui::Begin(window_name, &open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            header(rel);
            ImGui::Separator();

            m_settings.begin();
            const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
            constexpr int per_line = page_x;
            for (int i = 0; i < page_size; ++i) {
                if (i % per_line != 0) {
                    ImGui::SameLine(0, item_spacing);
                } else if (i != 0) {
                    // ImGui::Separator();
                }
                if (m_pos + i < m_rules.size()) {
                    m_spaces.image(m_rules.at(m_pos + i), starting_id + i, m_settings, &to_rule);
                } else {
                    m_spaces.dummy();
                }
            }
            m_settings.end();
        }
        ImGui::End();
    }

private:
    void header(const ruleT& rel) {
        assert(m_rules.empty() ? m_pos == 0 : 0 <= m_pos && m_pos < m_rules.size());
        shortcut_group shortcut{no_active_and_window_focused()};

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.truncate(0); // Won't actually free up memory.
            m_pos = 0;
        }
        // item_tooltip("When there are too many rules, the oldest rules will be cleared automatically.");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_pos == 0);
        if (ImGui::Button("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
            m_settings.restart_all();
            m_pos = std::max(0, m_pos - page_size);
        }
        ImGui::EndDisabled();
        item_tooltip(
            "<<  (Left      ) ~ get to the previous page.\n"
            ">>> (Right     ) ~ get to the next page or generate a new page.\n"
            "|>  (Ctrl+Right) ~ get to the last page.\n\n"
            "\">>>\" generates new pages of random rules when at the last page (or when there are no rules). See the tooltip for \"P\" for more details.\n\n"
            "(Try the shortcuts to see how these work.)");
        ImGui::SameLine();
        if (ImGui::Button(">>>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
            m_settings.restart_all();
            const int page_end = m_pos + page_size;
            if (m_rules.empty() || m_rules.size() <= page_end) {
                randT& rand = get_rand();
                rand.discard(uint32_t(std::time(0)) % 8); // Additional entropy.
                const int num = m_rules.size() < page_end ? page_end - m_rules.size() : page_size;
                for (int i = 0; i < num; ++i) {
                    // iso3::rand_rule(m_rules.emplace_back_ex(), rand, {64, 4, 1});
                    ruleT& rule = m_rules.emplace_back(m_rel ? *m_rel : rel);
                    if (m_mode == rand_mode::p) {
                        iso3::randomize_p(rule, rand, m_dist / 100.0);
                    } else {
                        iso3::randomize_n(rule, rand, m_dist);
                    }
                }
                assert(m_rules.size() >= page_size);
                m_pos = m_rules.size() - page_size;
            } else {
                m_pos += page_size;
            }
        }
        ImGui::SameLine(); // TODO: should redesign (conditions/+-m_pos) when page-resizing is supported.
        ImGui::BeginDisabled(m_rules.empty() || m_rules.size() <= m_pos + page_size);
        if (ImGui::Button("|>") || shortcut(ctrl_mode::ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
            m_settings.restart_all();
            m_pos = m_rules.size() - page_size;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::RadioButton("P", m_mode == rand_mode::p)) {
            m_mode = rand_mode::p;
        }
        item_tooltip(
            "The \"distance\" (number of groups with different values) is relative to the selected rule.\n\n"
            "P ~ around p% groups will have different values.\n"
            "N ~ exactly n groups will have different values.\n\n"
            "(P is suitable for making random discoveries; N is suitable for searching around specific rules.)");
        ImGui::SameLine();
        if (ImGui::RadioButton("N", m_mode == rand_mode::n)) {
            m_mode = rand_mode::n;
        }
        ImGui::SameLine();
        // TODO: the label is not accurate enough. (randomize_n() uses exact dist, while randomize_p() uses possibility.)
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "##Dist", m_dist, 0, 100, true,
                          m_mode == rand_mode::n ? "Dist: %d" : "Dist: %d%%");
        // TODO: should be able to visualize the rule...
        // (Ideally the locked rule should be shown in a separate window, but it's hard to control z-order in imgui...)
        if constexpr (debug_mode) {
            ImGui::SameLine();
            if (bool locked = bool(m_rel); ImGui::Checkbox("Lock", &locked)) {
                if (!m_rel) {
                    m_rel.emplace(rel);
                    set_message("Locked.");
                } else {
                    m_rel.reset();
                }
            }
            // item_tooltip("See the tooltip for \"P\" for details.");
        }

        ImGui::Separator();
        m_settings.header();
    }
};

// TODO: support resizing the window.
// TODO: support example rules ("Example" button)?
class rule_loader : no_copy {
    std::vector<ruleT> m_rules{};
    space_settings m_settings{};

    file_loader m_loader{};
    bool reset_scroll = false;

public:
    bool open = false;
    void display_if_open(const char* window_name, space_group& m_spaces, const int starting_id, opt_rule& to_rule) {
        if (!open) {
            return;
        }

        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        if (ImGui::Begin(window_name, &open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            header();
            ImGui::Separator();
            if (std::exchange(reset_scroll, false)) {
                ImGui::SetNextWindowScroll({0, 0});
            }

            // TODO: -> separate pages (instead of a single scrollable page)?
            // TODO: relying on header size not exceeding the width here.
            const ImVec2 item_spacing = ImGui::GetStyle().ItemSpacing;
            const ImVec2 child_size =
                m_spaces.image_size() * ImVec2(2, 2) + item_spacing + ImVec2(ImGui::GetStyle().ScrollbarSize, 0);
            if (ImGui::BeginChild("Rules", child_size) && !m_rules.empty()) {
                m_settings.begin();
                const int total = m_rules.size();
                constexpr int per_line = 2;
                for (int i = 0; i < total; ++i) {
                    if (i % per_line != 0) {
                        ImGui::SameLine(0, item_spacing.x);
                    } else if (i != 0) {
                        // ImGui::Separator();
                    }
                    m_spaces.image(m_rules[i], starting_id + i, m_settings, &to_rule);
                }
                m_settings.end();

                imgui_SetScrollWithUpDown(m_spaces.image_size().y + item_spacing.y);
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

private:
    void header() {
        shortcut_group shortcut{no_active_and_window_focused()};

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.clear();
            m_rules.shrink_to_fit();
            // TODO: is it possible to tell the child window the contents are cleared (so the slider won't appear for an extra frame)?
            reset_scroll = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // TODO: support multiple lists (instead of replacing the existing one).
        if (imgui_DoubleClickButton("Paste") || shortcut(ctrl_mode::ctrl, ImGuiKey_V, repeat_mode::no_repeat)) {
            const char* str = ImGui::GetClipboardText(); // May be nullptr.
            extract_rules(str ? str : "");
        }
        item_tooltip("Load rules from the clipboard. Shortcut: Ctrl+V.");
        ImGui::SameLine();
        if (imgui_DoubleClickButton("Open") /*|| shortcut(ctrl_mode::ctrl, ImGuiKey_O, repeat_mode::no_repeat)*/) {
            m_loader.open = true;
        }
        item_tooltip("Load rules from files.");
        if (m_loader.open) /*micro optimization*/ {
            std::string str{};
            if (m_loader.display_if_open(str, 1024 * 1024)) {
                extract_rules(str);
            }
        }

        ImGui::Separator();
        m_settings.header();
    }

    void extract_rules(std::string_view str, int reserve = 8, int max = 100) {
        std::vector<ruleT> rules{};
        rules.reserve(reserve);
        for (int i = 0; i < max; ++i) {
            if (!iso3::extract_rule(rules, str)) {
                break;
            }
        }
        if (!rules.empty()) {
            m_settings.restart_all();
            m_rules.swap(rules);
            reset_scroll = true;
            // TODO: slightly wasteful. (`extra_message` stores std::string internally.)
            const int num = m_rules.size();
            set_message(num == 1 ? "1 rule." : (std::to_string(num) + " rules.").c_str());
        } else {
            set_message("No rules.");
        }
    }
};

// TODO: support adding to temp list.
// TODO: whether to support ctrl shortcuts for spaces (ctrl+scroll/click/Z/Y/C)? (Intentionally undocumented in UI.)
class main_data : no_copy {
    using isotropic = iso3::isotropic;
    record_for<ruleT> m_rule{20};
    space_settings m_settings{};
    opt_rule to_rule{};

    rule_loader m_loader{};
    rule_generator m_generator{};

    space_group m_spaces{};
    shared_popup& m_popup = m_spaces.popup(); // Shared from `m_spaces`. (Misc popups use negative id.)

    // TODO: support grouping / sorting / (more generalized) filtering / value-constraints etc.
    // TODO: can be enhanced to [2862][3] by extending assignment format.
    bool misc_skip[3][3]{};
    bool misc_temp[3][3]{};
    bool skip(const codeT code, const cellT v) const { return misc_skip[iso3::decode(code, 4 /*center*/)][v]; }

public:
    void display() {
        // m_popup.begin();
        m_spaces.begin("Popup");

        header();
        ImGui::Separator();

        // TODO: using fixed names for convenience (technically should be specified per object).
        m_loader.display_if_open("Load", m_spaces, 20000, to_rule);
        m_generator.display_if_open("Generate", m_rule.get(), m_spaces, 10000, to_rule);

        // TODO: slightly wasteful. (Can reuse memory by making `record_for::get()` return non-const ref, but that's risky.)
        std::unique_ptr<ruleT> temp_rule(new ruleT{m_rule.get()});
        ruleT& rule = *temp_rule;
        assert(iso3::is_isotropic(rule));
        m_settings.begin();

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        int space_index = 0;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_image_width() + item_spacing); // For alignment.
        m_spaces.image(rule, space_index++, m_settings, nullptr);

        ImGui::SameLine();
        ImGui::BeginGroup();
        if constexpr (debug_mode) { // Experimental features.
            m_popup.button_to_open("Filter", -300);
            if (m_popup.begin_popup(-300, true)) {
                if (ImGui::IsWindowAppearing()) {
                    std::memcpy(misc_temp, misc_skip, sizeof(misc_skip));
                }
                if (imgui_DoubleClickButton("Apply")) {
                    std::memcpy(misc_skip, misc_temp, sizeof(misc_skip));
                    set_message("Applied.");
                }
                for (int f = 0; f < 3; ++f) {
                    for (int t = 0; t < 3; ++t) {
                        if (t != 0) {
                            ImGui::SameLine();
                        }
                        const char label[]{char('0' + f), '-', '>', char('0' + t), '\0'};
                        bool n = !misc_temp[f][t];
                        ImGui::Checkbox(label, &n);
                        misc_temp[f][t] = !n;
                    }
                }

                m_popup.end_popup();
            }

            shortcut_group shortcut{no_active_and_window_focused()};
            if (shortcut(ctrl_mode::ctrl, ImGuiKey_V, repeat_mode::no_repeat, 0)) {
                const char* str = ImGui::GetClipboardText();
                if (!str) {
                    str = "";
                }
                assert(!to_rule);
                if (iso3::extract_rule(to_rule.emplace_ex(), str)) {
                    // set_message("...");
                } else if (iso3::extract_values(to_rule.emplace(m_rule.get()), str)) {
                    // Format ~ [012*abc]{9}|[012i], e.g. *********|0 ****aa*a0|i ***aaaa00|i aaaa00a00|0 100000000|2
                    // set_message("...");
                } else {
                    to_rule.reset();
                    set_message("No rules.");
                }
            }
        }
        if (item_tooltip_enabled) {
            const auto text_with_tooltip = [](const char* text, const char* tooltip) {
                imgui_TextDisabled(text);
                item_tooltip(tooltip, false /*no highlight*/);
            };

            text_with_tooltip( // TODO: support opening link in browser.
                "About this program",
                "Coral v0.9 (c) 2025-2026 achabense (https://github.com/achabense/coral)\n\n"
                "This program is for exploring isotropic 3-state CA rules in the range-1 Moore neighborhood.");
            text_with_tooltip( // "Space windows" sounds good, but may confuse with regular windows.
                "Spaces",
                "Rules are emulated in torus spaces.\n\n" // Can be imagined as periodic unit in the infinite space.
                "For each space:\n"
                "Hold        to pause.\n"
                "Hold+scroll to zoom in/out.\n"
                "Right-click to open menu (to copy/select the rule).\n\n"
                "To run manually (regardless of whether paused):\n"
                "S ~ run by the step (\"Step\").\n"
                "D ~ run by 1.\n"
                "F ~ run by the largest step at each frame.\n\n"
                "(Also see the tooltip for \"Restart\".)");
            text_with_tooltip( // TODO: maintain a file for recently-copied rules (e.g. recently_copied.txt)?
                "Saving and loading rules",
                "Rules can be saved and loaded in a custom plain-text format.\n\n"
                "Open menu to copy rule to the clipboard. (Remember to paste the rule elsewhere.)\n\n"
                "Use \"Load\" to load rules from files or the clipboard.");
            //  "(Copy this tooltip (Ctrl+C) to verify the clipboard works.)");
            text_with_tooltip( //
                "Rule editing",
                "Isotropic rules must map cell to the same value for all equivalent cases (a \"group\").\n\n"
                "For the \"selected rule\", the list below displays all rules with only one group having different value. You can \"edit\" by selecting rules in the list. Starting from any rule, you can get to any other rule in the set.\n\n"
                "There are 2862 groups, each with 3 possible values, which means there are 3^2862 isotropic rules in total (and 2 * 2862 rules in the list).\n\n"
                "(The program is also able to generate random rules. See \"Generate\" for details.)");
            text_with_tooltip( //
                "Preventing strobing effect",
                "Rules with certain values may have large spans of area strobing among colors.\n\n"
                "To prevent the strobing effect, you can change step to 2, 3, etc. (A multiple of 6 works for most rules.)");
            text_with_tooltip( //
                "Scrolling",
                "To scroll with scrollbar:\n"
                "Click      to scroll towards position.\n"
                "Ctrl+click to scroll to position.\n\n" // Or drag from scroll button.
                "To scroll with shortcuts:\n"
                "Up/Down      ~ scroll up/down.\n"
                "Ctrl+Up/Down ~ scroll to top/bottom.");

            imgui_Text("");
            imgui_Text("<- the \"selected rule\"");
        } else {
            // TODO: is empty group guaranteed to be valid (and consume SameLine())?
            ImGui::Dummy({1, 1});
        }
        ImGui::EndGroup();

        ImGui::Separator();

        // Note: should not be set globally, as this also affects sliders...
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18); // To make the scrollbar easier to click.
        const bool child = ImGui::BeginChild("Groups");
        ImGui::PopStyleVar();
        if (child) {
            // std::optional<codeT> to_locate = std::nullopt;
            int to_locate = -1;
            // TODO: support ctrl+F?
            if (ImGui::IsWindowHovered() /*child only*/ && !ImGui::IsAnyItemHovered() /*bg only*/) {
                m_popup.open_on_idle_rclick(-100);
            }
            if (m_popup.opened(-100) /*micro optimization*/ &&
                m_popup.begin_popup(-100, !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                                  ImGuiHoveredFlags_AllowWhenBlockedByPopup))) {
                select_group(to_locate);
                m_popup.end_popup();
            }

            const int group_spacing = item_spacing * 3;
            const int group_width = code_image_width() + item_spacing + m_spaces.image_size().x;
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
            for (const auto& group : isotropic::groups()) {
                const codeT group_0 = group[0];
                if (skip(group_0, rule[group_0])) {
                    space_index += cellT::states - 1;
                    continue;
                }

                if (group_index % per_line != 0) {
                    ImGui::SameLine(0, group_spacing);
                } else if (group_index != 0) {
                    ImGui::Separator();
                }
                ++group_index;

                code_image(group_0);
                // TODO: where to open popup? (Window bg or group button?)
                // if (ImGui::IsItemHovered()) { m_popup.open_on_idle_rclick(-100); }
                const ImVec2 code_image_max = ImGui::GetItemRectMax();
                render_cell(rule[group_0], {code_image_max.x - cell_width, code_image_max.y + cell_width});
                if (to_locate == group_0) {
                    // More accurate than `ImGui::SetScrollHereY(0)`.
                    ImGui::SetScrollFromPosY(ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y, 0);
                }
                if (item_tooltip_enabled && group_index == 1) {
                    item_tooltip(
                        "The first small block (below group) represents value of the selected rule.\n\n" // Other blocks...
                        "Right-click to locate groups.");
                } else if (ImGui::BeginItemTooltip()) {
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
                    m_spaces.image(rule, space_index++, m_settings, &to_rule);
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

            imgui_SetScrollWithUpDown(m_spaces.image_size().y * 2 + ImGui::GetStyle().ItemSpacing.y * 3);
        }
        ImGui::EndChild();

        m_settings.end();
        assert(rule == m_rule.get());

        m_spaces.end();
        // m_popup.end();

        // TODO: working but technically should be called in a "wider" context.
        set_message_obj.display_if_present();
    }

private:
    void header() {
        shortcut_group shortcut{no_active_and_window_focused()};

        ImGui::Checkbox("Tooltips", &item_tooltip_enabled);
        // TODO: should take part in filtering.
        if (!ImGui::IsAnyItemActive() && test_key(ctrl_mode::no_ctrl, ImGuiKey_H, repeat_mode::no_repeat)) {
            item_tooltip_enabled = !item_tooltip_enabled;
        }
        item_tooltip(
            "Turn on/off tooltips. Shortcut: H.\n\n"
            "(Press Ctrl+C to copy tooltip text.)");
        ImGui::SameLine();
        ImGui::Checkbox("Load", &m_loader.open);
        item_tooltip("Load rules from files or the clipboard.");
        ImGui::SameLine();
        ImGui::Checkbox("Generate", &m_generator.open);
        item_tooltip("Generate random rules.");
        ImGui::SameLine();
        const bool to_zero = imgui_DoubleClickButton("Zero");
        item_tooltip(
            "Reset to the following rules:\n"
            "Zero     ~ the rule that maps cell to 0 in all cases (~ the initial rule).\n"
            "Identity ~ the rule that preserves cell's value in all cases.\n"
            "Life     ~ a special 3-state version of the Game of Life rule.\n\n"
            "(Purple buttons like these require double-clicking.)");
        ImGui::SameLine();
        const bool to_identity = imgui_DoubleClickButton("Identity");
        ImGui::SameLine();
        const bool to_life = imgui_DoubleClickButton("Life");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_rule.has_prev());
        // (Left/right as regular shortcuts for <</>>, ctrl+Z/Y as a convenient way to undo/redo ctrl+click selection.)
        const bool to_prev = ImGui::Button("<<") ||
                             shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat) ||
                             shortcut(ctrl_mode::ctrl, ImGuiKey_Z, repeat_mode::no_repeat);
        ImGui::EndDisabled();
        item_tooltip(
            // "(Left/Right/Ctrl+Right work for similar items in other windows.)\n\n"
            "Undo/redo selection:\n"
            "<< (Left      ) ~ get to the previous rule.\n"
            ">> (Right     ) ~ get to the next rule.\n"
            "|> (Ctrl+Right) ~ get to the last rule.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_rule.has_next());
        const bool to_next = ImGui::Button(">>") ||
                             shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat) ||
                             shortcut(ctrl_mode::ctrl, ImGuiKey_Y, repeat_mode::no_repeat);
        ImGui::SameLine();
        const bool to_last =
            ImGui::Button("|>") || shortcut(ctrl_mode::ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat);
        ImGui::EndDisabled();
        ImGui::SameLine();
        m_settings.header();
        ImGui::SameLine();
        ImGui::Text("%d fps", (int)std::round(ImGui::GetIO().Framerate));
        m_popup.open_for_text(-200, ImGui::IsItemHovered());
        if (m_popup.begin_popup(-200, true)) {
            set_framerate();
            m_popup.end_popup();
        }
        item_tooltip("Right-click to set frame rate.");

        if (to_zero || to_identity || to_life) {
            set_message("Selected.");
        }
        if (to_zero) {
            assert(!to_rule);
            iso3::to_zero(to_rule.emplace_ex());
        }
        if (to_identity) {
            assert(!to_rule);
            iso3::to_identity(to_rule.emplace_ex());
        }
        if (to_life) {
            assert(!to_rule);
            iso3::to_life(to_rule.emplace_ex());
        }
        if (to_rule) {
            m_rule.set(*to_rule);
            to_rule.reset();
            m_settings.restart_all();
        }
        if (to_prev) {
            m_rule.to_prev();
            m_settings.restart_all();
        }
        if (to_next) {
            m_rule.to_next();
            m_settings.restart_all();
        }
        if (to_last) {
            m_rule.to_last();
            m_settings.restart_all();
        }
    }

    static void select_group(int& to_locate) {
        shortcut_group shortcut{no_active_and_window_focused()};

        // TODO: working but technically should belong to object.
        static_var iso3::envT cells{};
        static_var record_for<codeT> record{10}; // Only for "Locate" and "Random".
        const auto sync_from_record = [&] {
            cells = iso3::decode(record.get());
            to_locate = record.get();
        };
        // TODO: whether to support record?
        if constexpr (debug_mode) {
            ImGui::BeginDisabled(!record.has_prev());
            if (ImGui::SmallButton("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
                record.to_prev();
                sync_from_record();
            }
            ImGui::EndDisabled();
            item_tooltip(
                "Record for located groups (via \"Locate\" or \"Random\"):\n"
                "<< (Left      ) ~ get to the previous group.\n"
                ">> (Right     ) ~ get to the next group.\n"
                "|> (Ctrl+Right) ~ get to the last group.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!record.has_next());
            // Intentionally not >>> (end -> random group).
            if (ImGui::SmallButton(">>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
                record.to_next();
                sync_from_record();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("|>") || shortcut(ctrl_mode::ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
                record.to_last();
                sync_from_record();
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
                    static_var cellT v = {};
                    if (ImGui::IsItemActivated()) {
                        v = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? iso3::increase(cell) : /*rclick*/ cell;
                    }
                    // TODO: support undoing cell values as well (with a separate record & ctrl+Z/Y)?
                    cell = v;
                }
            }
        }
        item_tooltip(
            "Left-click  to change value.\n"
            "Right-click to take the current value.\n"
            "Drag        to apply to multiple cells.");
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button("Locate")) {
            const codeT group_0 = isotropic::group_for(iso3::encode(cells))[0];
            record.set(group_0);
            sync_from_record();
        }
        if (ImGui::Button("Random")) {
            const auto& groups = isotropic::groups();
            const codeT group_0 = groups[get_rand()() % groups.size()][0];
            // TODO: whether to locate automatically?
            record.set(group_0);
            sync_from_record();
        }
        ImGui::EndGroup();
    }
};

static_assert(sizeof(main_data) < 1000); // Suitable to be stack-allocated.

inline void frame_main(main_data& data) {
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
        data.display();
    };
    ImGui::End();
}
