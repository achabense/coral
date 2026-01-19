#include <filesystem>
#include <fstream>
#include <vector>

#include "dear_imgui.hpp"

// std::filesystem::path involves a lot of nasty designs...
// Most horridly, it defines implicit conversions from(/to) char-strings (and assumes mysterious encoding).
// So the related functions can easily introduce encoding bugs, and it's hard to track the calls.
// (It's even unclear whether path("string-literal") is guaranteed to have correct encoding...)
using pathT = std::filesystem::path;

// (Jokingly, it's impossible to portably open utf8-encoded path using only standard C functions - `fopen` assumes mysterious encoding.)
// true ~ loaded successfully (`data` may become non-empty even if failed); doesn't care about utf8-boundary.
static bool load_file(const pathT& path, std::string& data, const int max_size) noexcept {
    try {
        if (!std::filesystem::is_regular_file(path)) {
            return false;
        }
        std::ifstream file(path, std::ifstream::binary | std::ifstream::ate);
        if (file) {
            const std::streamsize file_size = file.tellg();
            if (file_size != -1) {
                data.resize(std::min(file_size, std::streamsize(max_size)));
                if (file.seekg(0).read(data.data(), data.size()) && file.gcount() == data.size()) {
                    return true;
                }
            }
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Interop with utf8-encoded char-strings becomes broken in C++20 (have to make extra copies to get rid of deprecation & UB).
static pathT cpp17_u8path_maythrow(const std::string_view str) {
    const std::u8string yuck(str.data(), str.data() + str.size());
    return yuck;
}
static std::string cpp17_u8string_maythrow(const pathT& path) {
    const std::u8string yuck = path.u8string();
    return {yuck.data(), yuck.data() + yuck.size()};
}

// There is no way to extract filename efficiently for folder paths ending with separator.
static pathT get_filename_maythrow(const pathT& path, const bool is_file) {
    assert(!path.empty()); // & not root path.
    const auto back = path.native().back();
    if (is_file || (back != '/' && back != '\\')) {
        return path.filename();
    } else {
        return path.parent_path().filename();
    }
}

struct entryT {
    pathT filename{};
    std::string str{};
    bool is_file{}; // false ~ folder.
};

static std::vector<entryT> collect_entries_maythrow(const pathT& path, const int reserve = 40, int max = 1000) {
    std::vector<entryT> entries{};
    entries.reserve(reserve);
    for (const auto& entry :
         std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied)) {
        try {
            const bool is_file = entry.is_regular_file();
            if (is_file || entry.is_directory()) {
                // The standard doesn't say whether `directory_entry::path()` can end with separator, so have to consider the general case.
                pathT filename = get_filename_maythrow(entry.path(), is_file);
                std::string str = cpp17_u8string_maythrow(filename);
                entries.push_back({.filename = std::move(filename), .str = std::move(str), .is_file = is_file});
            }
            if (--max <= 0) { // TODO: set message?
                break;
            }
        } catch (...) {
            continue;
        }
    }
    // Folders first.
    std::ranges::stable_partition(entries, [](const entryT& entry) { return !entry.is_file; });
    return entries;
}

class folderT : no_copy {
    pathT m_path{}; // Canonical.
    std::string m_str{};
    std::vector<entryT> m_entries{};

public:
    bool valid() const noexcept { return !m_path.empty(); }
    void clear() noexcept {
        m_path.clear();
        m_entries.clear();
    }
    const pathT& path() const noexcept { return m_path; }
    const std::string& str() const noexcept { return m_str; }
    const std::vector<entryT>& entries() const noexcept { return m_entries; }
    pathT operator/(const pathT& path) const noexcept /*terminates (supposed to be impossible)*/ {
        assert(valid());
        return m_path / path;
    }

    enum class rel_mode { local /*m_path*/, fs /*filesystem::current_path()*/ };

    bool set_dir(const pathT& path, const rel_mode rel) noexcept {
        assert(rel == rel_mode::fs || valid());
        try {
            pathT canonical = std::filesystem::canonical(rel == rel_mode::local ? m_path / path : path);
            std::string str = cpp17_u8string_maythrow(canonical);
            m_entries = collect_entries_maythrow(canonical);
            m_str.swap(str);
            m_path.swap(canonical);
            return true;
        } catch (...) {
            return false;
        }
    }
    bool set_dir(const char* str, const rel_mode rel) noexcept {
        try {
            return set_dir(cpp17_u8path_maythrow(str), rel);
        } catch (...) {
            return false;
        }
    }
    bool refresh() noexcept {
        // return set_dir(".", rel_mode::local);
        assert(valid());
        try {
            m_entries = collect_entries_maythrow(m_path);
            return true;
        } catch (...) {
            return false;
        }
    }
};

using rel_mode = folderT::rel_mode;

// TODO: refine for different cases & some error messages should appear longer.
static constexpr const char* msg_failed = "Failed.";

class file_selector : no_copy {
    pathT m_home{}; // Canonical.
    folderT m_current{};

    // (Can be shared from callers, but that's unnecessarily complex.)
    shared_popup m_popup{};

    // TODO: whether to define locally?
    // (Will disappear earlier if the window is closed immediately, but that's extremely rare.)
    // extra_message m_message{};

    char input_filter[40]{};
    char input_path[220]{};
    bool reset_scroll = false;

public:
    file_selector() {
        if (m_current.set_dir(".", rel_mode::fs)) {
            m_home = m_current.path();
        } else {
            assert(!valid()); // TODO: input mode may still make sense in this case.
        }
    }

    bool valid() const { return m_current.valid(); }

    void display(extra_message& m_message, pathT& select_file) {
        assert(valid() && !m_home.empty());
        const auto on_set_dir = [&](const bool s) {
            if (s) {
                reset_scroll = true;
            } else {
                m_message.set(msg_failed);
            }
            return s;
        };
        const auto copy_path = [&](const pathT& path, const char* str = nullptr) {
            try {
                ImGui::SetClipboardText(str ? str : cpp17_u8string_maythrow(path).c_str());
                m_message.set("Copied.");
            } catch (...) {
                m_message.set(msg_failed);
            }
        };

        m_popup.set_popup_id("Popup");
        m_popup.begin();
        // Workaround for extra x-clip (for e.g. TextUnformatted and Selectable).
        // TODO: use ImDrawList::Push/PopPushClipRect instead?
        ImGui::BeginChild("Clip");

        if (imgui_DoubleClickButton("Refresh")) {
            if (on_set_dir(m_current.refresh())) {
                m_message.set("Refreshed.");
            }
        }
        ImGui::SameLine();
        // TODO: support record (<</>>).
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("Filter", ".txt", input_filter, std::size(input_filter));

        ImGui::Separator();

        {
            // ImGui::TextUnformatted(m_current.str().c_str());
            const auto& str = m_current.str(); // Micro optimization.
            ImGui::TextUnformatted(str.data(), str.data() + str.size());
            const bool hovered = ImGui::IsItemHovered();
            if (hovered || m_popup.opened(-50)) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, IM_COL32_WHITE);
                if (hovered) {
                    m_popup.open_on_idle_rclick(-50);
                }
            }
            if (m_popup.begin_popup(-50, true)) {
                if (ImGui::Selectable("Copy path")) {
                    copy_path(m_current.path(), str.c_str());
                }
                m_popup.end_popup();
            }
        }

        ImGui::Separator();

        constexpr const char* str_id = "##Entry";
        int extra_id = 0;
        // Are canonical paths guaranteed to be unique?
        ImGui::BeginDisabled(m_current.path().native() == m_home.native());
        if (imgui_SelectableEx(str_id, extra_id++, "Home", false, ImGuiSelectableFlags_NoAutoClosePopups)) {
            on_set_dir(m_current.set_dir(m_home, rel_mode::fs));
        }
        ImGui::EndDisabled();
        // `has_parent_path()` will always return true here. See: https://stackoverflow.com/questions/58201083
        ImGui::BeginDisabled(!m_current.path().has_relative_path() /*~ root path*/);
        if (imgui_SelectableEx(str_id, extra_id++, "..", false, ImGuiSelectableFlags_NoAutoClosePopups)) {
            on_set_dir(m_current.set_dir("..", rel_mode::local));
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        if (std::exchange(reset_scroll, false)) {
            ImGui::SetNextWindowScroll({0, 0});
        }
        if (ImGui::BeginChild("Entries", ImVec2(0, -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2)))) {
            const entryT* sel = nullptr;
            const std::string_view filter = input_filter;
            for (const auto& entry : m_current.entries()) {
                if (!filter.empty() && entry.str.find(filter) == std::string::npos) {
                    continue;
                }
                if (!entry.is_file) {
                    // ImGui::TextUnformatted("|-");
                    constexpr const char* str = "|-"; // Micro optimization.
                    ImGui::TextUnformatted(str, str + 2);
                    ImGui::SameLine();
                }
                const int id = extra_id++;
                const int extra_flag = m_popup.opened(id) ? ImGuiSelectableFlags_Highlight : ImGuiSelectableFlags_None;
                if (imgui_SelectableEx(str_id, id, entry.str.c_str(), false,
                                       ImGuiSelectableFlags_NoAutoClosePopups | extra_flag)) {
                    sel = &entry;
                }
                if (ImGui::IsItemHovered()) {
                    m_popup.open_on_idle_rclick(id);
                }
                if (m_popup.begin_popup(id, true)) {
                    if (ImGui::Selectable("Copy path")) {
                        copy_path(m_current / entry.filename);
                    }
                    m_popup.end_popup();
                }
            }
            if (sel) {
                if (sel->is_file) {
                    select_file = m_current / sel->filename;
                } else {
                    on_set_dir(m_current.set_dir(sel->filename, rel_mode::local));
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x -
                                ImGui::CalcTextSize("Input").x);
        if (ImGui::InputTextWithHint("Input", "Folder or file path", input_path, std::size(input_path),
                                     ImGuiInputTextFlags_EnterReturnsTrue) &&
            input_path[0] != '\0') {
            try {
                const pathT path = m_current / cpp17_u8path_maythrow(input_path);
                const auto status = std::filesystem::status(path);
                const bool is_file = std::filesystem::is_regular_file(status);
                if (is_file || std::filesystem::is_directory(status)) {
                    const pathT canonical = std::filesystem::canonical(path);
                    if (is_file) {
                        select_file = canonical;
                    }
                    if (on_set_dir(m_current.set_dir(is_file ? canonical.parent_path() : /*directory*/ canonical,
                                                     rel_mode::fs))) {
                        input_path[0] = '\0';
                    }
                } else {
                    m_message.set(!std::filesystem::exists(status) ? "Path doesn't exist." : msg_failed);
                }
            } catch (...) {
                m_message.set(msg_failed);
            }
        }

        ImGui::EndChild();
        m_popup.end();
    }
};

bool file_loader::display_if_open(extra_message& m_message, std::string& data, const int max_size) {
    if (!open) {
        return false;
    }
    if (!m_impl) {
        m_impl.reset(new file_selector{});
    }
    file_selector& selector = *reinterpret_cast<file_selector*>(m_impl.get());
    if (!selector.valid()) {
        // TODO: improve behavior (disable the open button afterwards, or support input-only mode.)
        open = false;
        m_message.set(msg_failed);
        return false;
    }
    bool loaded = false;

    // Note: str-id is sensitive to the id stack, but ok for the current use.
    constexpr const char* window_name = "Select file";
    if (!ImGui::IsPopupOpen(window_name)) {
        ImGui::OpenPopup(window_name);
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 24));
    if (ImGui::BeginPopupModal(window_name, &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize)) {
        // Modal window also disables scrolling.
        pathT select{};
        selector.display(m_message, select);
        if (!select.empty()) {
            if (load_file(select, data, max_size)) {
                loaded = true;
            } else {
                m_message.set(msg_failed);
            }
        }
        ImGui::EndPopup();
    }
    return loaded;
}
