#include "imgui_internal.h"

#include "dear_imgui.hpp"

bool imgui_IsItemVisibleEx(float least) {
    ImRect rect = GImGui->LastItemData.Rect;
    const float least_area = rect.GetArea() * least;
    rect.ClipWithFull(GImGui->CurrentWindow->ClipRect);
    return rect.GetArea() >= least_area;
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

bool imgui_DoubleClickButton(const char* label, ImVec2 size) {
    for (const auto col : {ImGuiCol_Button, ImGuiCol_ButtonActive, ImGuiCol_ButtonHovered}) {
        ImGui::PushStyleColor(col, ImLerp(ImGui::GetStyleColorVec4(col), ImVec4(1, 0, 0, 1), 0.2f));
    }
    const bool ret = ImGui::ButtonEx(label, size, ImGuiButtonFlags_PressedOnDoubleClick);
    ImGui::PopStyleColor(3);
    return ret;
}

void imgui_SliderIntEx(float slider_width, const char* label, int& val, int min /*[*/, int max /*]*/) {
    assert(min <= max);
    val = std::clamp(val, min, max);
    const float inner_spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float frame_height = ImGui::GetFrameHeight();
    const char* label_end = label + std::strlen(label);
    ImGui::PushID(label, label_end);
    ImGui::SetNextItemWidth(slider_width);
    ImGui::SliderInt("##Slider", &val, min, max, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    ImGui::SameLine(0, inner_spacing);
    if (ImGui::Button("-", {frame_height, frame_height})) {
        --val;
    }
    ImGui::SameLine(0, inner_spacing);
    if (ImGui::Button("+", {frame_height, frame_height})) {
        ++val;
    }
    ImGui::SameLine(0, inner_spacing);
    ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label, label_end));
    val = std::clamp(val, min, max);
    ImGui::PopID();
}
