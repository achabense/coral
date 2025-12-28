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
inline void render_code(const codeT code, const int scale, const ImVec2 min, ImDrawList& draw) {
    const auto env = iso3::decode(code);
    for (int y = 0, i = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            draw.AddRectFilled(min + ImVec2(x, y) * scale, min + ImVec2(x + 1, y + 1) * scale,
                               color_for(env.data[i++]));
        }
    }
}

inline void code_image(const codeT code, const int scale = 7) {
    constexpr ImVec2 border = {1, 1};
    ImGui::Dummy(ImVec2(scale * 3, scale * 3) + border * 2);
    if (ImGui::IsItemVisible()) {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        render_code(code, scale, min + border, draw);
        draw.AddRect(min, max, IM_COL32(180, 180, 180, 255));
    }
}

inline int code_image_width(const int scale = 7) { return scale * 3 + 2 /*border*/; }

// Ring buffer.
class rule_with_record {
    // There seems no way to enforce std::vector to allocate memory for exactly n objects...
    static constexpr int m_capacity = 16;
    std::unique_ptr<ruleT[]> m_rules{new ruleT[m_capacity]{}};
    int m_size = 1; // Record size. (Initially with a single ruleT{}.)
    int m_0 = 0;    // Position of [0].
    int m_pos = 0;  // Position of the current rule (relative to m_0).

    ruleT& at(int pos) { return m_rules[(m_0 + pos) % m_capacity]; }

public:
    rule_with_record() = default;
    rule_with_record(const rule_with_record&) = delete;
    rule_with_record& operator=(const rule_with_record&) = delete;

    bool has_next() const { return m_pos < m_size - 1; }
    bool has_prev() const { return m_pos > 0; }
    void to_next() { m_pos = std::min(m_pos + 1, m_size - 1); }
    void to_prev() { m_pos = std::max(m_pos - 1, 0); }

    ruleT& get() { return at(m_pos); }
    void set(const ruleT& rule) {
        assert(1 <= m_size && m_size <= m_capacity);
        assert(0 <= m_0 && m_0 < m_capacity);
        assert(0 <= m_pos && m_pos < m_size);
        if (at(m_pos) != rule) {
            m_size = m_pos + 1; // Discard rules after the current rule.
            if (m_size < m_capacity) {
                ++m_size;
            } else if (++m_0 == m_capacity) { // Discard the oldest rule.
                m_0 = 0;
            }
            m_pos = m_size - 1;
            at(m_pos) = rule;
        }
    }
};

// TODO: support loading list of rules.
// TODO: support setting to game-of-life.
// TODO: support configurable init state.
// TODO: support adding to temp list.
// TODO: support editable window.
class main_data {
    using isotropic = iso3::isotropic;

    rule_with_record m_rule{};

    struct blobT {
        tile_with_texture tile{};
        bool active = false;
    };
    std::unordered_map<int /*id*/, blobT> m_preview{};

    std::mt19937 m_rand{uint32_t(std::time(0))};

    const tileT m_init{iso3::rand_tile({256, 196}, std::mt19937{0})};

    shared_popup m_popup{};
    extra_message m_message{};

    ImVec2 texture_size() const { return ImVec2(m_init.size().x, m_init.size().y); }
    ImVec2 image_size() const { return texture_size() + ImVec2{2, 2} /*border*/; }

    void image(const ruleT& rule, const int id, const bool can_select) {
        constexpr ImVec2 border = {1, 1};
        ImGui::Dummy(texture_size() + border * 2);
        if (ImGui::IsItemVisible()) {
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            ImDrawList& draw = *ImGui::GetWindowDrawList();
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            bool hovered = false;
            if (imgui_IsItemVisibleEx(0.15f)) {
                hovered = ImGui::IsItemHovered();

                blobT& blob = m_preview[id];
                assert(!blob.active);
                blob.active = true;
                tile_with_texture& tile = blob.tile;
                if (tile.empty()) {
                    tile.assign(m_init);
                }
                // TODO: whether to run before displaying?
                // TODO: always run if newly restarted?
                const bool pause = _pause || (!ctrl && hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left));
                if (!pause && (_interval == 0 || (ImGui::GetFrameCount() % (_interval + 1)) == 0)) {
                    tile.run(rule, _step);
                }

                // TODO: the current ownership works, but is still risky...
                // `tile` must not be cleared / resized after this (in this frame).
                const ImTextureID texture = tile.texture();
                draw.AddImage(texture, min + border, max - border);
                // TODO: should be able to configure size & scale / toggle off the zoom window.
                if (hovered && ImGui::IsMousePosValid() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
                    if (ImGui::BeginTooltip()) {
                        constexpr float scale = 3;
                        const ImVec2 total = texture_size();
                        const ImVec2 show = imgui_Min({4 * 18, 3 * 18}, total);
                        const ImVec2 pos = imgui_Max({0, 0}, imgui_Floor(ImGui::GetMousePos() - min - show / 2));
                        // ImGui::Image(texture, show * scale, pos / total, (pos + show) / total);
                        ImGui::Dummy(show * scale);
                        if (ImGui::IsItemVisible()) {
                            ImGui::GetWindowDrawList()->AddImage(texture, ImGui::GetItemRectMin(),
                                                                 ImGui::GetItemRectMax(), pos / total,
                                                                 (pos + show) / total);
                            // No need for border (will be rendered by the window).
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopStyleVar();
                }
            } else {
                draw.AddRectFilled(min, max, IM_COL32(32, 32, 32, 255));
            }
            draw.AddRect(min, max,
                         hovered || m_popup.opened(id) ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Border));

            bool select = false, copy = false;
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_popup.open(id);
            }
            if (m_popup.begin_popup(id)) {
                select |= can_select && ImGui::Selectable("Select");
                copy |= ImGui::Selectable("Copy");
                m_popup.end_popup();
            }
            if (hovered && ctrl) {
                if (can_select) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    select |= ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                }
                copy |= ImGui::Shortcut(ImGuiKey_C | ImGuiMod_Ctrl, ImGuiInputFlags_RouteAlways);
            }
            if (select) {
                to_rule = rule; // For simpler sync logic.
                m_message.set("Selected.");
            }
            if (copy) {
                ImGui::SetClipboardText(iso3::to_string(rule).c_str());
                m_message.set("Copied.");
            }
        }
    }

    // Too large to be stack-allocated.
    main_data(const main_data&) = delete;
    main_data& operator=(const main_data&) = delete;
    main_data() { iso3::test_all(m_rand); }

public:
    static auto make_unique() { return std::unique_ptr<main_data>(new main_data{}); }

    // TODO: should not be public.
    bool _pause = false;
    int _step = 1;
    int _interval = 0; // Frame based. TODO: -> time based?

    // TODO: support find-group & to-random-group & to-top.
    // std::optional<codeT> to_group;

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
        ruleT& rule = m_rule.get(); // TODO: working but risky... (Will be restored finally.)

        m_popup.set_popup_id("Options");
        m_popup.begin();

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        // To align with the code-buttons below.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_image_width() + item_spacing);
        int preview_index = 0;
        image(rule, preview_index++, false);

        ImGui::Separator();

        // TODO: document scrolling behavior & support ctrl+click to scroll to position.
        // It's possible to scroll to position in page mode by dragging from the scrollbar.
        // However, if users are unware of this, they will have trouble due to the huge list size.
        // And the default min grab size makes the scrollbar hard to click. (Controlled by `GetStyle().GrabMinSize`.)
        // Related: https://github.com/ocornut/imgui/issues/8002
        // ImGui::GetIO().ConfigScrollbarScrollByPage = false; // Always scroll to position.

        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.0f); // 12.0f by default.
        const bool child = ImGui::BeginChild("Groups");
        ImGui::PopStyleVar();
        if (child) {
            const int group_spacing = item_spacing * 3;
            const int group_width = code_image_width() + item_spacing + image_size().x;
            const int avail_width = ImGui::GetContentRegionAvail().x;
            const int per_line = std::max((avail_width + group_spacing) / (group_width + group_spacing), 1);

            int group_index = 0;
            for (const auto& group : isotropic::get().groups()) {
                if (group_index % per_line != 0) {
                    ImGui::SameLine(0, group_spacing);
                } else if (group_index != 0) {
                    ImGui::Separator();
                }
                ++group_index;

                // !!TODO: unfinished. Should display the current value & value-to.
                code_image(group[0]);
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
                    image(rule, preview_index++, true);
                }
                ImGui::EndGroup();
                iso3::increase(rule, group); // Restored.
            }
        }
        ImGui::EndChild();

        m_popup.end();
        m_message.display();

        // I've no clue whether this is truly allowed...
        // When used by algorithms, a "Predicate" is not allowed to modify input.
        // https://eel.is/c++draft/algorithms.requirements
        // std::erase_if(map) uses the same term, but it's defined by equivalent behavior (which allows modification).
        // https://eel.is/c++draft/unord.map.erasure
        std::erase_if(m_preview, [](auto& blob) { return !std::exchange(blob.second.active, false); });
    }

private:
    void flush() {
        // TODO: whether to compare the rule (same -> no restart)?
        if (std::exchange(reset, false)) {
            to_rule.emplace();
        }
        if (std::exchange(randomize, false)) {
            iso3::randomize(to_rule.emplace(m_rule.get()), m_rand);
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
            for (auto& [_, blob] : m_preview) {
                blob.tile.assign(m_init);
            }
        }
    }
};

inline void frame_main(main_data& data) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    constexpr ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Main", nullptr, main_flags)) {
        data.reset = imgui_DoubleClickButton("Reset");
        ImGui::SameLine();
        data.randomize = imgui_DoubleClickButton("Random");
        ImGui::SameLine();
        data.paste = imgui_DoubleClickButton("Paste");
        ImGui::SameLine();
        ImGui::Text("%d fps", (int)std::round(ImGui::GetIO().Framerate));
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_prev());
        data.to_prev = ImGui::Button("Undo");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.has_next());
        data.to_next = ImGui::Button("Redo");
        ImGui::EndDisabled();
        ImGui::SameLine();
        data.restart = ImGui::Button("Restart");
        ImGui::SameLine();
        ImGui::Checkbox("Pause", &data._pause);
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "Step", data._step, 1, 10);
        ImGui::SameLine();
        imgui_SliderIntEx(ImGui::GetFontSize() * 10, "Interval", data._interval, 0, 10);

        ImGui::Separator();
        data.display();
    };
    ImGui::End();
}
