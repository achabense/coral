#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "imgui.h"
// #include "imgui_internal.h"

// At least `*least` area is visible.
extern bool imgui_IsItemVisibleEx(float least);

// Necessary for `shared_popup` below (to be shared across windows).
// Unfortunately not exposed in "imgui.h". Related: https://github.com/ocornut/imgui/pull/8995
extern bool imgui_BeginPopupEx(ImGuiID id, ImGuiWindowFlags flags = 0);

// Will also disable scrolling in the current window.
extern void imgui_LockScroll();

extern bool imgui_DoubleClickButton(const char* label, ImVec2 size = {});

extern void imgui_SliderIntEx(float slider_width, const char* label, int& val, int min /*[*/, int max /*]*/);

// Can be shared across windows; doesn't rely on item id.
class shared_popup {
    std::optional<int> owner_id; // Independent of imgui's id system.
    ImGuiID popup_id = 0;
    bool activated = false;

public:
    shared_popup() = default;
    shared_popup(const shared_popup&) = delete;
    shared_popup& operator=(const shared_popup&) = delete;

    // Must not change after assigned.
    void set_popup_id(const char* str_id) { popup_id = ImGui::GetID(str_id); }

    void begin() { activated = false; }
    void end() {
        if (!activated) {
            owner_id.reset();
        }
    }

    void open(int id) {
        assert(popup_id != 0);
        if (!owner_id) {
            ImGui::OpenPopup(popup_id, ImGuiPopupFlags_NoReopen);
            owner_id = id;
        }
    }
    bool opened() const { return owner_id.has_value(); }
    bool opened(int id) const { return owner_id == id; }

    // Can be a single function to take callbacks, but it's likely more efficient to split to two functions.
    bool begin_popup(int id, bool lock_scroll = true) {
        if (owner_id == id && imgui_BeginPopupEx(popup_id, ImGuiWindowFlags_NoNav)) {
            activated = true;
            if (lock_scroll) { // && !ImGui::IsWindowHovered(...) (currently not needed)
                imgui_LockScroll();
            }
            return true;
        }
        return false;
    }
    // Must be called after successful begin_popup().
    void end_popup() { ImGui::EndPopup(); }
};

// Doesn't affect window system / regular tooltips.
class extra_message {
    std::string str;
    double time = 0;
    std::optional<ImVec2> pos;
    std::optional<ImVec2> str_size;

public:
    extra_message() = default;
    extra_message(const extra_message&) = delete;
    extra_message& operator=(const extra_message&) = delete;

    void set(std::string&& s, double t = 0.5 /*sec*/) {
        str = std::move(s);
        time = t;
        pos.reset();
        str_size.reset();
    }

    void display() {
        if (!str.empty()) {
            const char* str_begin = str.data();
            const char* str_end = str_begin + str.size();
            if (!str_size) {
                str_size = ImGui::CalcTextSize(str_begin, str_end);
            }
            constexpr ImVec2 padding = {6, 6};
            constexpr ImVec2 offset = {12, 6};
            const ImVec2 total = *str_size + padding * 2;
            if (!pos) {
                ImVec2& p = pos.emplace(offset + (ImGui::IsMousePosValid() ? ImGui::GetMousePos() : ImVec2{}));
                // Clamp into the main window.
                const ImVec2 min = ImGui::GetStyle().WindowPadding;
                const ImVec2 max = ImGui::GetMainViewport()->Size - total - min;
                if (min.x < max.x && min.y < max.y) {
                    p.x = std::clamp(p.x, min.x, max.x);
                    p.y = std::clamp(p.y, min.y, max.y);
                }
            }
            ImDrawList& draw = *ImGui::GetForegroundDrawList();
            draw.AddRectFilled(*pos, *pos + total, ImGui::GetColorU32(ImGuiCol_PopupBg));
            draw.AddRect(*pos, *pos + total, ImGui::GetColorU32(ImGuiCol_Border));
            draw.AddText(*pos + padding, ImGui::GetColorU32(ImGuiCol_Text), str_begin, str_end);

            time -= ImGui::GetIO().DeltaTime;
            if (time <= 0) {
                str.clear();
            }
        }
    }
};
