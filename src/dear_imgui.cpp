#include "imgui_internal.h"

#include "dear_imgui.hpp"

// Im[US]XX types are typedefs of raw int types, and it's unclear whether they should mean intXX_t or int_leastXX_t...
// So the following fails, the program is unlikely to work as expected...
// Related: https://github.com/ocornut/imgui/issues/8546 and 8740
static_assert(sizeof(ImU32) == sizeof(uint32_t) && sizeof(ImS32) == sizeof(int32_t));

bool imgui_IsItemVisibleEx(float least) {
    ImRect rect = GImGui->LastItemData.Rect;
    const float least_area = rect.GetArea() * least;
    rect.ClipWithFull(GImGui->CurrentWindow->ClipRect);
    return rect.GetArea() >= least_area;
}

bool imgui_IsItemDisabled() {
    return (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) ||
           (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);
}

void imgui_HighlightItem(ImGuiID id) {
    if (id) {
        ImGui::NavHighlightActivated(id);
    }
}

bool imgui_BeginPopupEx(ImGuiID id, ImGuiWindowFlags flags) {
    if (ImGui::BeginPopupEx(id, flags | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoSavedSettings)) {
        // TODO: risky workaround to prevent left-click from focusing the background -> affecting visual (as hover is blocked).
        // Related: `UpdateMouseMovingWindowEndFrame()`.
        if (ImGui::IsWindowFocused() /*topmost popup*/ && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() &&
            !ImGui::IsAnyItemHovered()) {
            ImGui::GetIO().MouseClicked[1] |= std::exchange(ImGui::GetIO().MouseClicked[0], false);
        }
        return true;
    }
    return false;
}

void imgui_LockScroll() { ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame); }

// TODO: not reusing `shortcut_group` in "gui.hpp", and not taking part in filtering.
// (See the comments below; may be able to replace `shortcut_group` with a function in the future.)
void imgui_SetScrollWithUpDown(int dy) {
    // To respect `imgui_LockScroll()`.
    if (!ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner)) {
        return;
    }
    // <-> `no_active_and_window_focused()`.
    if (!(!ImGui::IsAnyItemActive() &&
          ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy))) {
        return;
    }

    std::optional<float> scroll_y = std::nullopt;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, !ctrl /*-> repeat*/)) {
        scroll_y = !ctrl ? ImGui::GetScrollY() - dy : 0;
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, !ctrl)) {
        scroll_y = !ctrl ? ImGui::GetScrollY() + dy : ImGui::GetScrollMaxY();
    }
    if (scroll_y) {
        ImGui::SetScrollY(*scroll_y);
        imgui_LockScroll();
        const ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(GImGui->CurrentWindow, ImGuiAxis_Y);
        ImGui::NavHighlightActivated(scrollbar_id);
        // TODO: this may be able to fix both filtering and interaction with popup/locking, but will block IsItemHovered() for one frame.
        // Related: https://github.com/ocornut/imgui/issues/9138 (fixed but need to wait for release).
        // ImGui::SetActiveID(scrollbar_id, GImGui->CurrentWindow); // Will last for only one frame.
        // GImGui->ActiveIdFromShortcut = scrollbar_id;
    }
}

bool imgui_DummyEx(ImVec2 size, const char* str_id, int extra_id) {
    const bool visible = ImGui::IsRectVisible(size);
    if (!visible) {
        ImGui::Dummy(size);
    } else {
        ImGui::PushID(extra_id);
        ImGui::InvisibleButton(str_id, size);
        ImGui::PopID();
    }
    assert(visible == ImGui::IsItemVisible());
    return visible;
}

bool imgui_DoubleClickButton(const char* label, ImVec2 size) {
    for (const auto col : {ImGuiCol_Button, ImGuiCol_ButtonActive, ImGuiCol_ButtonHovered}) {
        ImGui::PushStyleColor(col, ImLerp(ImGui::GetStyleColorVec4(col), ImVec4(1, 0, 0, 1), 0.2f));
    }
    const bool ret = ImGui::ButtonEx(label, size, ImGuiButtonFlags_PressedOnDoubleClick);
    ImGui::PopStyleColor(3);
    return ret;
}

void imgui_SliderIntEx(float slider_width, const char* label, int& val, int min /*[*/, int max /*]*/, bool repeat,
                       const char* format) {
    assert(min <= max);
    val = std::clamp(val, min, max);
    const float inner_spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float frame_height = ImGui::GetFrameHeight();
    const char* label_end = label + std::strlen(label);
    ImGui::PushID(label, label_end);
    ImGui::SetNextItemWidth(slider_width);
    ImGui::SliderInt("##Slider", &val, min, max, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, repeat);
    ImGui::SameLine(0, inner_spacing);
    if (ImGui::Button("-", {frame_height, frame_height})) {
        --val;
    }
    ImGui::SameLine(0, inner_spacing);
    if (ImGui::Button("+", {frame_height, frame_height})) {
        ++val;
    }
    ImGui::PopItemFlag();
    ImGui::PopID();
    const char* display_end = ImGui::FindRenderedTextEnd(label, label_end);
    if (label != display_end) {
        ImGui::SameLine(0, inner_spacing);
        ImGui::TextUnformatted(label, display_end);
    }
    val = std::clamp(val, min, max);
}

bool imgui_SelectableEx(const char* str_id, int extra_id, const char* label, bool selected,
                        ImGuiSelectableFlags flags) {
    assert(str_id[0] == '#' && str_id[1] == '#');
    const ImVec2 pos =
        GImGui->CurrentWindow->DC.CursorPos + ImVec2(0, GImGui->CurrentWindow->DC.CurrLineTextBaseOffset);

    ImGui::PushID(extra_id);
    const bool ret = ImGui::Selectable(str_id, selected, flags /*, {0, 0}*/);
    ImGui::PopID();
    if (ImGui::IsItemVisible()) {
        // TODO: risky workaround to emulate normal label rendering.
        const char* label_end = label + std::strlen(label); // Won't skip ##(#)...
        const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, false);
        const ImRect bb = GImGui->LastItemData.Rect;
        // const ImVec2 pos = bb.Min; // Wrong pos, see above.
        const ImVec2 size = bb.GetSize();
        ImGui::RenderTextClippedEx(GImGui->CurrentWindow->DrawList, pos,
                                   ImVec2(ImMin(pos.x + size.x, GImGui->CurrentWindow->WorkRect.Max.x), pos.y + size.y),
                                   label, label_end, &label_size, GImGui->Style.SelectableTextAlign, &bb);
    }
    return ret;
}
