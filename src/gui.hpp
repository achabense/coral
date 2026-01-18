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

#define static_var static // For tracking non-const static variables.

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
            m_data.resize_ex(m_pos + 1); // Discard values after the current pos.
            m_data.emplace_back_ex() = val;
            m_pos = m_data.size() - 1;
        }
    }
};

// To replace std::optional<ruleT> (for stack allocation).
class opt_rule : no_copy {
    std::unique_ptr<ruleT> m_rule{new ruleT{}};
    bool has_value = false;

public:
    explicit operator bool() const { return has_value; }
    const ruleT& operator*() const {
        assert(has_value);
        return *m_rule;
    }
    void reset() { has_value = false; }

    // Not responsible for the value.
    ruleT& emplace_ex() {
        has_value = true;
        return *m_rule;
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

inline bool no_active_and_window_focused() {
    return !ImGui::IsAnyItemActive() &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
}

inline bool no_active_and_window_hovered() {
    return !ImGui::IsAnyItemActive() &&
           ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_NoPopupHierarchy);
}

// TODO: support pausing/restarting individual windows & applying s/d/f to group.
class preview_settings : no_copy {
    bool restart = false;
    bool pause = false;
    int step = 1;
    int interval = 0; // Frame based. (0 ~ each frame.)
    int counter = 0;  // 0 ~ tick.

public:
    friend class preview_group;

    void restart_all() { restart = true; }
    void begin() { counter = counter == 0 ? interval : counter - 1; }
    void end() { restart = false; }

    // Using a multiple of lcm(2,3) to prevent trivial strobing. (For 3-state rules, both 2 and 3 can be strobing period.)
    static constexpr int max_step = 12;
    bool tick() const { return counter == 0; }

    // `counter` is not strictly necessary, but makes ticking more stable when `interval` changes.
    // bool tick() const { return interval == 0 || (ImGui::GetFrameCount() % (interval + 1)) == 0; }

    void header() {
        auto shortcut = create_shortcut(no_active_and_window_hovered()); // Instead of focused.

        if (ImGui::Button("Restart") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_R, repeat_mode::no_repeat)) {
            restart = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Pause", &pause);
        if (shortcut(ctrl_mode::no_ctrl, ImGuiKey_Space, repeat_mode::no_repeat)) {
            pause = !pause;
        }
        constexpr int step_min = 1, step_max = max_step;
        constexpr int interval_min = 0, interval_max = 20;
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
class preview_group : no_copy {
    struct blobT {
        tile_with_texture tile{};
        bool active = false;
        bool skip_tick = false;
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

    // TODO: uncertain about the design (condition & op).
    // ctrl + left-click -> select
    // ctrl + c -> copy
    // ctrl/press + scroll -> change zoom level
    // right-click -> op menu (select/copy)
    // no-ctrl + s/d/f -> extra step
    // `id` must be unique per group & imgui's id stack (for unique `GetItemID()`).
    void image(const ruleT& rule, const int id, const preview_settings& m_settings, shared_popup& m_popup,
               extra_message& m_message, opt_rule* to_rule = nullptr) {
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
        if (tile.empty() || m_settings.restart) {
            tile.assign(m_init);
            tile.run(rule, m_settings.step);
            blob.skip_tick = true;
        } else if (op && (op == 'S' || op == 'D' || op == 'F')) {
            tile.run(rule, op == 'S' ? m_settings.step : op == 'D' ? 1 : /*'F'*/ m_settings.max_step);
            blob.skip_tick = true;
        } else if (m_settings.pause || (!ctrl && pressed)) {
            blob.skip_tick = true;
        } else if (m_settings.tick() && !std::exchange(blob.skip_tick, false)) {
            tile.run(rule, m_settings.step);
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
        if (hovered) {
            m_popup.open_on_idle_rclick(id);
        }
        if (m_popup.begin_popup(id, /*lock-scroll*/ true)) {
            copy |= ImGui::Selectable("Copy rule");
            select |= can_select && ImGui::Selectable("Select");
            m_popup.end_popup();
        }
        if (can_select && hovered && ctrl) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            select |= ImGui::IsItemActivated(); // Clicked.
        }
        if (select) {
            to_rule->emplace_ex() = rule;
            m_message.set("Selected.");
        }
        if (copy) {
            ImGui::SetClipboardText(iso3::to_string(rule).c_str());
            m_message.set("Copied.");
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
// TODO: support fixing target rule (e.g. fix to all-0 rule).
class rule_generator : no_copy {
    static constexpr int page_x = 3, page_y = 2, page_size = page_x * page_y;

    ring_buffer<ruleT> m_rules{page_size * 20};
    int m_pos = 0; // Position for the first rule in the page.

    enum class rand_mode { p, c };
    rand_mode m_mode = rand_mode::p;
    int m_dist = 10; // c ~ dist, p ~ dist / 100

    preview_settings m_settings{};

public:
    bool open = false;
    void display_if_open(const char* window_name, const ruleT& rel, preview_group& m_preview, const int starting_id,
                         shared_popup& m_popup, extra_message& m_message, opt_rule& to_rule) {
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
                    m_preview.image(m_rules.at(m_pos + i), starting_id + i, m_settings, m_popup, m_message, &to_rule);
                } else {
                    m_preview.dummy();
                }
            }
            m_settings.end();
        }
        ImGui::End();
    }

private:
    void header(const ruleT& rel) {
        assert(m_rules.empty() ? m_pos == 0 : 0 <= m_pos && m_pos < m_rules.size());
        auto shortcut = create_shortcut(no_active_and_window_focused());

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.resize_ex(0); // Won't actually free up memory.
            m_pos = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_pos == 0);
        if (ImGui::Button("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
            m_settings.restart_all();
            m_pos = std::max(0, m_pos - page_size);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(">>>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
            m_settings.restart_all();
            const int page_end = m_pos + page_size;
            if (m_rules.empty() || m_rules.size() <= page_end) {
                randT& rand = get_rand();
                const int num = m_rules.size() < page_end ? page_end - m_rules.size() : page_size;
                for (int i = 0; i < num; ++i) {
                    // iso3::rand_rule(m_rules.emplace_back_ex(), rand, {64, 4, 1});
                    if (m_mode == rand_mode::c) {
                        iso3::randomize_c(m_rules.emplace_back_ex() = rel, rand, m_dist);
                    } else {
                        iso3::randomize_p(m_rules.emplace_back_ex() = rel, rand, m_dist / 100.0);
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
        if (ImGui::Button("|>")) {
            m_settings.restart_all();
            m_pos = m_rules.size() - page_size;
        }
        ImGui::EndDisabled();
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
        // TODO: the label is not accurate enough. (randomize_c() uses exact dist, while randomize_p() uses possibility.)
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "##Dist", m_dist, 0, 100, true,
                          m_mode == rand_mode::c ? "Dist: %d" : "Dist: %d%%");

        ImGui::Separator();
        m_settings.header();
    }
};

// TODO: support resizing the window.
class rule_loader : no_copy {
    std::vector<ruleT> m_rules{};
    preview_settings m_settings{};

    file_loader m_loader{};
    bool reset_scroll = false;

public:
    bool open = false;
    void display_if_open(const char* window_name, preview_group& m_preview, const int starting_id,
                         shared_popup& m_popup, extra_message& m_message, opt_rule& to_rule) {
        if (!open) {
            return;
        }

        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        if (ImGui::Begin(window_name, &open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            header(m_message);
            ImGui::Separator();
            if (std::exchange(reset_scroll, false)) {
                ImGui::SetNextWindowScroll({0, 0});
            }

            // TODO: -> separate pages (instead of a single scrollable page)?
            // TODO: relying on header size not exceeding the width here.
            const ImVec2 item_spacing = ImGui::GetStyle().ItemSpacing;
            const ImVec2 child_size =
                m_preview.image_size() * ImVec2(2, 2) + item_spacing + ImVec2(ImGui::GetStyle().ScrollbarSize, 0);
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
                    m_preview.image(m_rules[i], starting_id + i, m_settings, m_popup, m_message, &to_rule);
                }
                m_settings.end();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

private:
    void header(extra_message& m_message) {
        auto shortcut = create_shortcut(no_active_and_window_focused());

        ImGui::BeginDisabled(m_rules.empty());
        if (imgui_DoubleClickButton("Clear")) {
            m_rules.clear();
            m_rules.shrink_to_fit();
            reset_scroll = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // TODO: support multiple lists (instead of replacing the existing one).
        if (imgui_DoubleClickButton("Paste") || shortcut(ctrl_mode::ctrl, ImGuiKey_V, repeat_mode::no_repeat)) {
            const char* str = ImGui::GetClipboardText(); // May be nullptr.
            extract_rules(str ? str : "", m_message);
        }
        ImGui::SameLine();
        if (imgui_DoubleClickButton("Open") || shortcut(ctrl_mode::ctrl, ImGuiKey_O, repeat_mode::no_repeat)) {
            m_loader.open = true;
        }
        if (m_loader.open) /*micro optimization*/ {
            std::string str{};
            if (m_loader.display_if_open(str, 1024 * 1024)) {
                extract_rules(str, m_message);
            }
        }

        ImGui::Separator();
        m_settings.header();
    }

    void extract_rules(std::string_view str, extra_message& m_message, int reserve = 8, int max = 100) {
        std::vector<ruleT> rules{};
        rules.reserve(reserve);
        for (int i = 0; i < max; ++i) {
            if (!iso3::from_string(rules, str)) {
                break;
            }
        }
        if (!rules.empty()) {
            m_settings.restart_all();
            m_rules.swap(rules);
            reset_scroll = true;
            // TODO: slightly wasteful. (`extra_message` stores std::string internally.)
            const int num = m_rules.size();
            m_message.set(num == 1 ? "1 rule." : (std::to_string(num) + " rules.").c_str());
        } else {
            m_message.set("No rules.");
        }
    }
};

// TODO: support adding to temp list.
class main_data : no_copy {
    using isotropic = iso3::isotropic;
    record_for<ruleT> m_rule{20};
    preview_settings m_settings{};
    opt_rule to_rule{};

    rule_loader m_loader{};
    rule_generator m_generator{};

    preview_group m_preview{};
    shared_popup m_popup{};
    extra_message m_message{};

public:
    void display() {
        header();
        ImGui::Separator();

        m_popup.set_popup_id("Popup");
        m_popup.begin();
        m_preview.begin();

        // TODO: using fixed names for convenience (technically should be specified per object).
        m_loader.display_if_open("Load", m_preview, 20000, m_popup, m_message, to_rule);
        m_generator.display_if_open("Generate", m_rule.get(), m_preview, 10000, m_popup, m_message, to_rule);

        // TODO: slightly wasteful. (Can reuse memory by making `record_for::get()` return non-const ref, but that's risky.)
        std::unique_ptr<ruleT> temp_rule(new ruleT{m_rule.get()});
        ruleT& rule = *temp_rule;
        m_settings.begin();

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        int preview_index = 0;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_image_width() + item_spacing); // For alignment.
        m_preview.image(rule, preview_index++, m_settings, m_popup, m_message, nullptr);

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
                    // More accurate than `ImGui::SetScrollHereY(0)`.
                    ImGui::SetScrollFromPosY(ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y, 0);
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
                    m_preview.image(rule, preview_index++, m_settings, m_popup, m_message, &to_rule);
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

        m_settings.end();
        assert(rule == m_rule.get());

        m_preview.end();
        m_popup.end();
        m_message.display_if_present();
    }

private:
    void header() {
        // TODO: uncertain about the design (condition & op).
        auto shortcut = create_shortcut(no_active_and_window_focused());

        ImGui::Checkbox("Load", &m_loader.open);
        ImGui::SameLine();
        ImGui::Checkbox("Generate", &m_generator.open);
        ImGui::SameLine();
        const bool to_zero = imgui_DoubleClickButton("Zero");
        ImGui::SameLine();
        const bool to_identity = imgui_DoubleClickButton("Identity");
        ImGui::SameLine();
        const bool to_life = imgui_DoubleClickButton("Life");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_rule.has_prev());
        // TODO: also support ctrl+Z/Y? (Convenient for undoing ctrl+click selection.)
        const bool to_prev =
            ImGui::Button("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_rule.has_next());
        const bool to_next =
            ImGui::Button(">>") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat);
        ImGui::SameLine();
        const bool to_last = ImGui::Button("|>");
        ImGui::EndDisabled();
        ImGui::SameLine();
        m_settings.header();
        ImGui::SameLine();
        ImGui::Text("%d fps", (int)std::round(ImGui::GetIO().Framerate));

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
        auto shortcut = create_shortcut(no_active_and_window_focused());

        // TODO: working but technically should belong to object.
        static_var iso3::envT cells{};
        static_var record_for<codeT> record{10}; // Only for "Locate" and ">>>".
        const auto sync_from_record = [&] {
            cells = iso3::decode(record.get());
            to_locate = record.get();
        };
        {
            ImGui::BeginDisabled(!record.has_prev());
            if (ImGui::SmallButton("<<") || shortcut(ctrl_mode::no_ctrl, ImGuiKey_LeftArrow, repeat_mode::no_repeat)) {
                record.to_prev();
                sync_from_record();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton(">>>") ||
                shortcut(ctrl_mode::no_ctrl, ImGuiKey_RightArrow, repeat_mode::no_repeat)) {
                if (record.has_next()) {
                    record.to_next();
                    sync_from_record();
                } else {
                    const auto& groups = isotropic::get().groups();
                    const codeT group_0 = groups[get_rand()() % groups.size()][0];
                    record.set(group_0);
                    sync_from_record();
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!record.has_next());
            if (ImGui::SmallButton("|>")) {
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
                    cell = v;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Locate")) {
            const codeT group_0 = isotropic::get().group_for(iso3::encode(cells))[0];
            record.set(group_0);
            sync_from_record();
        }
    }
};

static_assert(sizeof(main_data) < 1000); // Suitable to be stack-allocated.

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
        data.display();
    };
    ImGui::End();
}
