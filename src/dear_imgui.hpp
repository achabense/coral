#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "imgui.h"
// #include "imgui_internal.h"

// At least `*least` area is visible.
extern bool imgui_IsItemVisibleEx(float least);

extern bool imgui_IsItemDisabled();

extern void imgui_HighlightItem(ImGuiID id);

// Necessary for `shared_popup` below (to be shared across windows).
// Unfortunately not exposed in "imgui.h". Related: https://github.com/ocornut/imgui/pull/8995
extern bool imgui_BeginPopupEx(ImGuiID id, ImGuiWindowFlags flags = 0);

// Will also disable scrolling in the current window.
extern void imgui_LockScroll();

// There can be at most one call in each window, and (workaround) should be called after items that may show popup / lock scroll.
// Without ctrl: -/+ dy; with ctrl: to 0/max.
extern void imgui_SetScrollWithUpDown(int dy);

// Behaves like `InVisibleButton()` if visible, otherwise `Dummy()`. Returns `IsItemVisible()`.
// (Unlike `InVisibleButton()`, this will not preserve active id if the item is scrolled outside of visible area.)
extern bool imgui_DummyEx(ImVec2 size, const char* str_id, int extra_id);

extern bool imgui_DoubleClickButton(const char* label, ImVec2 size = {});

extern void imgui_SliderIntEx(float slider_width, const char* label, int& val, int min /*[*/, int max /*]*/,
                              bool repeat, const char* format /*= "%d"*/);

// To decouple label from item-id (~ PushID(extra_id) / str_id).
// `label` is rendered as plain text, i.e. won't skip "##...", "###..." etc.
extern bool imgui_SelectableEx(const char* str_id, int extra_id, const char* label, bool selected = false,
                               ImGuiSelectableFlags flags = 0 /*, ImVec2 size = {}*/);

// Those defined in "imgui_internal.h" cannot be declared for use here (as they are inline functions).
inline ImVec2 imgui_Floor(ImVec2 a) { return {std::floor(a.x), std::floor(a.y)}; }
inline ImVec2 imgui_Ceil(ImVec2 a) { return {std::ceil(a.x), std::ceil(a.y)}; }
inline ImVec2 imgui_Round(ImVec2 a) { return {std::round(a.x), std::round(a.y)}; }
inline ImVec2 imgui_Min(ImVec2 a, ImVec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline ImVec2 imgui_Max(ImVec2 a, ImVec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline ImVec2 imgui_Clamp(ImVec2 a, ImVec2 min /*[*/, ImVec2 max /*]*/) {
    return {std::clamp(a.x, min.x, max.x), std::clamp(a.y, min.y, max.y)};
}

// Defined here just for convenience. Inheritance is generally avoided in this project; this is the only exception.
// (Also prevents implicit move ctor/assignment.)
class no_copy {
protected:
    no_copy() = default;
    ~no_copy() = default;
    no_copy(const no_copy&) = delete;
    no_copy& operator=(const no_copy&) = delete;
};

// Can be shared across windows; doesn't rely on item id.
class shared_popup : no_copy {
    std::optional<int> owner_id; // Independent of imgui's id system.
    ImGuiID popup_id = 0;
    bool activated = false;

public:
    void begin(const char* str_id) {
        if (popup_id == 0) {
            popup_id = ImGui::GetID(str_id);
        }
        activated = false;
    }
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
            // activated = true;   // Otherwise, begin_popup() cannot be called before open() (id will be reset by end()).
            // imgui_LockScroll(); // At least for this frame.
        }
    }
    bool opened() const { return owner_id.has_value(); }
    bool opened(int id) const { return owner_id == id; }

    void button_to_open(const char* label, int id) {
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(opened(id) ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
        if (ImGui::Button(label)) {
            open(id);
        }
        ImGui::PopStyleColor(2);
    }

    // Workaround to enable popup when an input field is active but not held.
    // Must work in combination with some hover test & should not apply to input field itself.
    // (Normally `!IsAnyitemActive()` is enough, but when an input field is accepting input (even if not held), it will also report active id, and it's strange to disable popup in this case. `IsItemHovered()` (for other items) and `IsWindowHovered()` will report false normally when the input field is actually held.)
    void open_on_idle_rclick(int id /*, bool hovered*/) {
        // if (!hovered) { return; }
        assert(popup_id != 0);
        assert(!imgui_IsItemDisabled());
        if (!owner_id && (!ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) /*&& !ImGui::IsMouseDown(ImGuiMouseButton_Left)*/) {
            open(id); // TODO: slightly wasteful.
        }
    }

    void open_for_text(int id, bool hovered) {
        if (hovered || opened(id)) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(ImGuiCol_Text));
            if (hovered) {
                open_on_idle_rclick(id);
            }
        }
    }

    // Can be a single function to take callbacks, but it's likely more efficient to split to two functions.
    bool begin_popup(int id, bool lock_scroll) {
        if (owner_id == id && imgui_BeginPopupEx(popup_id, ImGuiWindowFlags_NoNav)) {
            activated = true;
            if (lock_scroll) {
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
class extra_message : no_copy {
    std::string str;
    double time = 0;
    std::optional<ImVec2> pos;
    std::optional<ImVec2> str_size;

public:
    void set(const char* s, double t = 0.5 /*sec*/) {
        str = s;
        time = t;
        pos.reset();
        str_size.reset();
    }

    void display_if_present() {
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

class file_loader : no_copy {
    std::shared_ptr<void> m_impl{}; // Unique ownership; just for simpler code.

public:
    bool open = false;

    // Modal window.
    // true ~ loaded successfully (`data` may become non-empty even if failed); doesn't care about utf8-boundary.
    // (Callers should only deal with successful case.)
    bool display_if_open(std::string& data, int max_size = 1024 * 1024);
};

// TODO: should use fewer global vars.
#define inline_var inline // For tracking global inline variables.
#define static_var static // For tracking non-const static variables.

// TODO: workaround for message and tooltips.
// (The current msg handling works but is somewhat messy... Lacks convention.)
inline_var extra_message set_message_obj{};
inline void set_message(const char* s, double t = 0.5) { set_message_obj.set(s, t); }

inline_var bool item_tooltip_enabled = true;
inline void item_tooltip(const char* tooltip, bool highlight = true) {
    if (!item_tooltip_enabled) {
        return;
    }
    if (highlight) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  IM_COL32(255, 255, 0, 32));
    }
    if (ImGui::BeginItemTooltip()) {
        // TODO: ideally should be fine-tuned per tooltip.
        ImGui::PushTextWrapPos(42 * ImGui::GetFontSize());
        ImGui::TextUnformatted(tooltip, tooltip + std::strlen(tooltip));
        ImGui::PopTextWrapPos();
        // TODO: should take part in filtering.
        if (ImGui::GetIO().KeyCtrl && !ImGui::IsAnyItemActive()) {
            const ImVec2 padding = ImGui::GetStyle().WindowPadding - ImVec2(2, 2);
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos() + padding,
                                                      ImGui::GetWindowPos() + ImGui::GetWindowSize() - padding,
                                                      IM_COL32(255, 255, 255, 32));
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                ImGui::SetClipboardText(tooltip);
                set_message("Copied.");
            }
        }
        ImGui::EndTooltip();
    }
}
