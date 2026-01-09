#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "imgui.h"
// #include "imgui_internal.h"

// At least `*least` area is visible.
extern bool imgui_IsItemVisibleEx(float least);

extern void imgui_HighlightItem(ImGuiID id);

// Necessary for `shared_popup` below (to be shared across windows).
// Unfortunately not exposed in "imgui.h". Related: https://github.com/ocornut/imgui/pull/8995
extern bool imgui_BeginPopupEx(ImGuiID id, ImGuiWindowFlags flags = 0);

// Will also disable scrolling in the current window.
extern void imgui_LockScroll();

extern bool imgui_DoubleClickButton(const char* label, ImVec2 size = {});

extern void imgui_SliderIntEx(float slider_width, const char* label, int& val, int min /*[*/, int max /*]*/,
                              bool repeat);

// Those defined in "imgui_internal.h" cannot be declared for use here (as they are inline functions).
inline ImVec2 imgui_Floor(ImVec2 a) { return {std::floor(a.x), std::floor(a.y)}; }
inline ImVec2 imgui_Ceil(ImVec2 a) { return {std::ceil(a.x), std::ceil(a.y)}; }
inline ImVec2 imgui_Round(ImVec2 a) { return {std::round(a.x), std::round(a.y)}; }
inline ImVec2 imgui_Min(ImVec2 a, ImVec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline ImVec2 imgui_Max(ImVec2 a, ImVec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline ImVec2 imgui_Clamp(ImVec2 a, ImVec2 min /*[*/, ImVec2 max /*]*/) {
    return {std::clamp(a.x, min.x, max.x), std::clamp(a.y, min.y, max.y)};
}

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
    bool begin_popup(int id) {
        if (owner_id == id && imgui_BeginPopupEx(popup_id, ImGuiWindowFlags_NoNav)) {
            activated = true;
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

    void set(const char* s, double t = 0.5 /*sec*/) {
        str = s;
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
                const ImVec2 p = offset + (ImGui::IsMousePosValid() ? ImGui::GetMousePos() : ImVec2{});
                const ImVec2 min = ImGui::GetStyle().WindowPadding;
                const ImVec2 max = ImGui::GetMainViewport()->Size - total - min;
                pos = min.x < max.x && min.y < max.y ? imgui_Clamp(p, min, max) : p;
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
