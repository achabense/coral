#pragma once

#include <ctime>
#include <unordered_map>

#include "dear_imgui.hpp"

#include "rule.hpp"

using iso3::cellT;
using iso3::codeT;
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

class tile_with_texture {
    tileT m_tile = {};
    ImTextureID m_texture = {};
    bool m_sync = false;

public:
    tile_with_texture() = default;
    tile_with_texture(const tile_with_texture&) = delete;
    tile_with_texture& operator=(const tile_with_texture&) = delete;
    void swap(tile_with_texture& other) noexcept {
        m_tile.swap(other.m_tile);
        std::swap(m_texture, other.m_texture);
        std::swap(m_sync, other.m_sync);
    }
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
class ring_buffer {
    // There seems no way to enforce std::vector to allocate memory for exactly n objects...
    std::unique_ptr<T[]> m_data{};
    int m_capacity{};
    int m_0 = 0; // Position of [0].
    int m_size = 0;

public:
    // All {}-initialized.
    explicit ring_buffer(int capacity) : m_data(new T[capacity]{}), m_capacity(capacity) { assert(capacity > 0); }
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

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
class record_for {
    ring_buffer<T> m_data;
    int m_pos = 0; // Current position.

public:
    explicit record_for(int capacity) : m_data(capacity) { m_data.emplace_back(); }
    record_for(const record_for&) = delete;
    record_for& operator=(const record_for&) = delete;

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

// It's extremely hard to design a generalized shortcut function (e.g. whether to highlight/filter/test disabled/hover/focus)...
enum class ctrl_mode { ctrl, no_ctrl };
enum class repeat_mode { repeat, no_repeat, down };
inline bool test_key(ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat) {
    return ((ctrl == ctrl_mode::ctrl) == ImGui::GetIO().KeyCtrl) &&
           (repeat == repeat_mode::down ? ImGui::IsKeyDown(key)
                                        : ImGui::IsKeyPressed(key, repeat == repeat_mode::repeat));
}

class preview_group {
    struct blobT {
        tile_with_texture tile{};
        bool newly_restarted = false;
        bool active = false;
    };

    std::unordered_map<int /*id*/, blobT> m_blobs{};
    const tileT m_init{iso3::rand_tile({256, 196}, std::mt19937{0})};

    ImVec2 texture_size() const { return ImVec2(m_init.size().x, m_init.size().y); }

public:
    preview_group() = default;
    preview_group(const preview_group&) = delete;
    preview_group& operator=(const preview_group&) = delete;

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
    void restart_all() {
        for (auto& [_, blob] : m_blobs) {
            blob.tile.assign(m_init);
            blob.newly_restarted = true;
        }
    }

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
    void image(const ruleT& rule, const speedT& speed, const int id, shared_popup& m_popup, extra_message& m_message,
               std::optional<ruleT>* to_rule = nullptr) {
        constexpr ImVec2 border = {1, 1};
        // TODO: is it possible to distinguish items with no id from the background (e.g. IsBgHovered())?
        // ImGui::Dummy(texture_size() + border * 2);
        ImGui::PushID(id);
        ImGui::InvisibleButton(":|", texture_size() + border * 2);
        ImGui::PopID();
        if (!ImGui::IsItemVisible()) {
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
        if (tile.empty()) {
            tile.assign(m_init);
            blob.newly_restarted = true;
        }
        // TODO: whether to run before displaying?
        // TODO: whether to always skip the init state?
        if (std::exchange(blob.newly_restarted, false)) {
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
        if (m_popup.begin_popup(id)) {
            imgui_LockScroll();
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

private:
    static char test_op() {
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

// TODO: support loading list of rules.
// TODO: support setting to game-of-life.
// TODO: support configurable init state.
// TODO: support adding to temp list.
class main_data {
    using isotropic = iso3::isotropic;

    record_for<ruleT> m_rule{20};
    preview_group m_preview{};
    std::mt19937 m_rand{uint32_t(std::time(0))};
    shared_popup m_popup{};
    extra_message m_message{};

    // Too large to be stack-allocated.
    main_data(const main_data&) = delete;
    main_data& operator=(const main_data&) = delete;
    main_data() { iso3::test_all(m_rand); }

public:
    static auto make_unique() { return std::unique_ptr<main_data>(new main_data{}); }

    // TODO: should not be public.
    preview_group::speedT speed{.pause = false, .step = 1, .interval = 0};

    std::optional<ruleT> to_rule = std::nullopt; // TODO: use ruleT + bool instead?
    bool reset = false;
    bool restart = false;
    bool randomize = false;
    bool paste = false;

    bool has_prev() const { return m_rule.has_prev(); }
    bool has_next() const { return m_rule.has_next(); }
    bool to_prev = false;
    bool to_next = false;

    void display() {
        flush();
        m_popup.set_popup_id("Options");
        m_popup.begin();
        m_preview.begin();
        // TODO: slightly wasteful. (Can reuse memory by making `record_for::get()` return non-const ref, but that's risky.)
        std::unique_ptr<ruleT> temp_rule(new ruleT{m_rule.get()});
        ruleT& rule = *temp_rule;

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        int preview_index = 0;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_image_width() + item_spacing); // For alignment.
        m_preview.image(rule, speed, preview_index++, m_popup, m_message, nullptr);

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
            if (m_popup.begin_popup(-100)) {
                // imgui_LockScroll(); // No need to disable scrolling.
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
                    m_preview.image(rule, speed, preview_index++, m_popup, m_message, &to_rule);
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
        if (std::exchange(randomize, false)) {
            // !!TODO: support different modes.
            iso3::randomize(to_rule.emplace(m_rule.get()), m_rand, 1.0 / 32);
            // iso3::rand_rule(to_rule.emplace(), m_rand, {64, 4, 1});
        }
        if (std::exchange(paste, false)) {
            if (!iso3::from_string(to_rule.emplace(), ImGui::GetClipboardText())) {
                to_rule.reset();
                m_message.set("Nothing to paste.");
            } else {
                m_message.set("Pasted.");
            }
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
        if (std::exchange(restart, false)) {
            m_preview.restart_all();
        }
    }

    static void select_group(int& to_locate, std::mt19937& m_rand) {
        // TODO: working but technically should belong to object.
        static iso3::envT cells{};
        static record_for<iso3::envT> record{10}; // Only for "Locate" and "Random".
        {
            ImGui::BeginDisabled(!record.has_prev());
            if (ImGui::SmallButton("<<")) {
                record.to_prev();
                cells = record.get();
                to_locate = iso3::encode(cells);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!record.has_next());
            if (ImGui::SmallButton(">>")) {
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 24, 24, 255)); // Applies to all windows.

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    if (ImGui::Begin("Main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        // TODO: uncertain about the design (condition & op).
        const bool enable_shortcut =
            !ImGui::IsAnyItemActive() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
        const auto shortcut = [enable_shortcut](ctrl_mode ctrl, ImGuiKey key, repeat_mode repeat,
                                                ImGuiID highlight = ImGui::GetItemID()) {
            if (enable_shortcut && test_key(ctrl, key, repeat)) {
                if (highlight) {
                    imgui_HighlightItem(highlight);
                }
                return true;
            }
            return false;
        };

        data.reset |= imgui_DoubleClickButton("Reset");
        ImGui::SameLine();
        data.randomize |= imgui_DoubleClickButton("Random");
        ImGui::SameLine();
        data.paste |= imgui_DoubleClickButton("Paste") || shortcut(ctrl_mode::ctrl, ImGuiKey_V, repeat_mode::no_repeat);
        ImGui::SameLine();
        ImGui::Text("%d fps", (int)std::round(ImGui::GetIO().Framerate));
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_prev());
        data.to_prev |=
            ImGui::Button("Undo") || (data.has_prev() && shortcut(ctrl_mode::ctrl, ImGuiKey_Z, repeat_mode::no_repeat));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_next());
        data.to_next |=
            ImGui::Button("Redo") || (data.has_next() && shortcut(ctrl_mode::ctrl, ImGuiKey_Y, repeat_mode::no_repeat));
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

    ImGui::PopStyleColor();
}
