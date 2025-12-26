#pragma once

#include <ctime>

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
inline void render_code(const codeT code, const int scale, const ImVec2 min) {
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const auto env = iso3::decode(code);
    for (int y = 0, i = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            draw.AddRectFilled(min + ImVec2(x, y) * scale, min + ImVec2(x + 1, y + 1) * scale,
                               color_for(env.data[i++]));
        }
    }
}

inline bool code_button(const codeT code, const int scale = 7) {
    constexpr ImVec2 padding = {2, 2};
    ImGui::PushID(code);
    const bool ret = ImGui::Button("##Code", ImVec2(scale * 3, scale * 3) + padding * 2);
    ImGui::PopID();
    if (ImGui::IsItemVisible()) {
        render_code(code, scale, ImGui::GetItemRectMin() + padding);
    }
    return ret;
}

inline int code_button_width(const int scale = 7) { return scale * 3 + 4; }

// TODO: support loading list of rules.
// TODO: support setting to game-of-life.
// TODO: support configurable init state.
// TODO: support undoing changes.
// TODO: support zoom window (that can be toggled off).
class main_data {
    using isotropic = iso3::isotropic;

    ruleT m_rule{};
    tile_with_texture m_tile{};
    std::vector<tile_with_texture> m_preview{};

    std::mt19937 m_rand{uint32_t(std::time(0))};

    const tileT m_init{iso3::rand_tile({256, 196}, std::mt19937{0})};

    shared_popup m_popup{};
    extra_message m_message{};

    ImVec2 image_size() const { return ImVec2(m_init.size().x, m_init.size().y); }

    // TODO: require visible area to exceed certain limit.
    void image(tile_with_texture& tile, const ruleT& rule, const int id, const bool can_select) {
        ImGui::Dummy(image_size());
        bool active = false;
        if (ImGui::IsItemVisible()) {
            ImDrawList& draw = *ImGui::GetWindowDrawList();
            const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            bool hovered = false;
            if (imgui_IsItemVisibleEx(0.15f)) {
                if (tile.empty()) {
                    tile.assign(m_init);
                }
                if (!_pause && (_interval == 0 || (ImGui::GetFrameCount() % (_interval + 1)) == 0)) {
                    tile.run(rule, _step); // TODO: whether to run before displaying?
                }

                active = true;
                hovered = ImGui::IsItemHovered();
                draw.AddImage(tile.texture(), min, max);
            }
            draw.AddRect(min, max,
                         hovered || m_popup.opened(id) ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Separator));

            bool copy = false, select = false;
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_popup.open(id);
            }
            if (m_popup.begin_popup(id)) {
                copy |= ImGui::Selectable("Copy");
                select |= can_select && ImGui::Selectable("Select");
                m_popup.end_popup();
            }
            if (hovered && ImGui::GetIO().KeyCtrl) {
                copy |= ImGui::Shortcut(ImGuiKey_C | ImGuiMod_Ctrl, ImGuiInputFlags_RouteAlways);
                if (can_select) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    select |= ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                }
            }
            if (copy) {
                ImGui::SetClipboardText(iso3::to_string(rule).c_str());
                m_message.set("Copied.");
            }
            if (select) {
                to_rule = rule; // For simpler sync logic.
                m_message.set("Selected.");
            }
        }
        if (!active && !tile.empty()) {
            tile.clear();
        }
    }

    // Too large to be stack-allocated.
    main_data(const main_data&) = delete;
    main_data& operator=(const main_data&) = delete;
    main_data() {
        iso3::test_all(m_rand);
        m_preview.resize(isotropic::k * (cellT::states - 1));
    }

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

    void display() {
        flush();

        m_popup.set_popup_id("Options");
        m_popup.begin();

        const int item_spacing = ImGui::GetStyle().ItemSpacing.x;
        // To align with the code-buttons below.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + code_button_width() + item_spacing);
        image(m_tile, m_rule, /*id*/ INT_MAX, false);

        ImGui::Separator();

        // TODO: which scrolling mode to use?
        // It's still possible to scroll to position in page mode by dragging from the scrollbar.
        // However, if users are unware of this, they will have trouble due to the huge list size.
        // And the default min grab size makes the scrollbar hard to click. (Can be controlled with `GetStyle().GrabMinSize`.)
        // However, scrolling by page is indeed the desired behavior sometimes...
        // Related: https://github.com/ocornut/imgui/issues/8002
        ImGui::GetIO().ConfigScrollbarScrollByPage = false; // Always scroll to position.

        if (ImGui::BeginChild("Groups")) {
            const int group_spacing = item_spacing * 3;
            const int group_width = code_button_width() + item_spacing + image_size().x;
            const int avail_width = ImGui::GetContentRegionAvail().x;
            const int per_line = std::max((avail_width + group_spacing) / (group_width + group_spacing), 1);

            int group_index = 0, preview_index = 0;
            for (const auto& group : isotropic::get().groups()) {
                if (group_index % per_line != 0) {
                    ImGui::SameLine(0, group_spacing);
                } else if (group_index != 0) {
                    ImGui::Separator();
                }
                ++group_index;

                // !!TODO: unfinished. Should display the current value & value-to.
                const bool hit = code_button(group[0]);
                ImGui::SameLine(0, item_spacing);
                ImGui::BeginGroup();
                for (int i = 0; i < cellT::states - 1; ++i) {
                    iso3::increase(m_rule, group);
                    image(m_preview[preview_index], m_rule, preview_index, true);
                    ++preview_index;
                }
                ImGui::EndGroup();
                iso3::increase(m_rule, group); // Restored.
            }
        }
        ImGui::EndChild();

        m_popup.end();
        m_message.display();
    }

private:
    void flush() {
        // TODO: whether to compare the rule (same -> no restart)?
        if (std::exchange(reset, false)) {
            m_rule.fill({});
            restart = true;
        }
        if (std::exchange(randomize, false)) {
            iso3::randomize(m_rule, m_rand);
            restart = true;
        }
        if (std::exchange(paste, false)) {
            if (!iso3::from_string(to_rule.emplace(), ImGui::GetClipboardText())) {
                to_rule.reset();
                m_message.set("Nothing to paste.");
            }
        }
        if (to_rule) {
            m_rule = *to_rule;
            to_rule.reset();
            restart = true;
        }
        if (std::exchange(restart, false)) {
            if (!m_tile.empty()) {
                m_tile.assign(m_init);
            }
            for (auto& preview : m_preview) {
                if (!preview.empty()) {
                    preview.assign(m_init);
                }
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
