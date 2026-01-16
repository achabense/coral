#include <filesystem>
#include <fstream>
#include <vector>

#include "dear_imgui.hpp"

// std::filesystem::path involves a lot of nasty designs... See the "// n." comments below.
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

// 1. interop with char-strings containing utf8 becomes broken in C++20 (have to make extra copies to get rid of deprecation & UB).
static pathT cpp17_u8path_maythrow(const std::string_view u8path) {
    const std::u8string yuck(u8path.data(), u8path.data() + u8path.size());
    return yuck;
}
static std::string cpp17_u8string_maythrow(const pathT& path) {
    const std::u8string yuck = path.u8string();
    return {yuck.data(), yuck.data() + yuck.size()};
}
static std::string cpp17_u8string_nothrow(const pathT& path) noexcept {
    try {
        return cpp17_u8string_maythrow(path);
    } catch (...) {
        return "???"; // TODO: improve.
    }
}

// 2.1. there is no way to extract filename efficiently for folder paths ending with separator.
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

// TODO: add limit.
static std::vector<entryT> collect_entries_maythrow(const pathT& path) {
    std::vector<entryT> entries{};
    for (const auto& entry :
         std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied)) {
        try {
            const bool is_file = entry.is_regular_file();
            if (is_file || entry.is_directory()) {
                // 2.2. and the standard doesn't say whether `directory_entry::path()` can end with separator, so have to consider the general case.
                pathT filename = get_filename_maythrow(entry.path(), is_file);
                std::string str = cpp17_u8string_maythrow(filename);
                entries.push_back({.filename = std::move(filename), .str = std::move(str), .is_file = is_file});
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
    std::vector<entryT> m_entries{};

public:
    bool valid() const noexcept { return !m_path.empty(); }
    void clear() noexcept {
        m_path.clear();
        m_entries.clear();
    }
    const pathT& path() const noexcept { return m_path; }
    const std::vector<entryT>& entries() const noexcept { return m_entries; }
    pathT operator/(const entryT& entry) const noexcept /*terminates (supposed to be impossible)*/ {
        return m_path / entry.filename;
    }

    // Relative to `m_path`.
    bool set_path(const pathT& path) noexcept {
        try {
            pathT canonical = std::filesystem::canonical(m_path / path);
            m_entries = collect_entries_maythrow(canonical);
            m_path.swap(canonical);
            return true;
        } catch (...) {
            return false;
        }
    }
    // 3. horridly, path defines implicit conversions from/to char-strings (and assumes mysterious encoding).
    bool set_path(const char* str) noexcept {
        try {
            return set_path(cpp17_u8path_maythrow(str));
        } catch (...) {
            return false;
        }
    }
    bool set_path(const entryT& entry) noexcept {
        try {
            return set_path(m_path / entry.filename);
        } catch (...) {
            return false;
        }
    }
    bool set_current_path() noexcept {
        try {
            return set_path(std::filesystem::current_path());
        } catch (...) {
            return false;
        }
    }
    bool refresh() noexcept {
        if (!valid()) {
            return false;
        }
        try {
            m_entries = collect_entries_maythrow(m_path);
            return true;
        } catch (...) {
            return false;
        }
    }
};

class file_loader_impl : no_copy {
    pathT m_home{};
    folderT m_current{};

    // (Can be shared from callers, but that's unnecessarily complex.)
    extra_message m_message{};
    // shared_popup m_popup{}; // TODO: support popup (for copying path etc.).

    char input_filter[40]{};
    char input_path[220]{};
    bool reset_scroll = false;

public:
    file_loader_impl() {
        if (m_current.set_current_path()) {
            m_home = m_current.path();
        }
        // !!TODO: if failed, only input mode makes sense.
    }

    bool display(std::string& data, const int max_size) {
        bool file_loaded = false;
        const auto test_loaded = [&](const bool loaded, bool& set_true) {
            if (loaded) {
                set_true = true;
            } else {
                m_message.set("Cannot open.");
            }
            return loaded;
        };

        // Workaround for extra x-clip (for e.g. TextUnformatted and Selectable).
        // TODO: use ImDrawList::Push/PopPushClipRect instead?
        ImGui::BeginChild("Limit");

        if (imgui_DoubleClickButton("Refresh")) {
            if (test_loaded(m_current.refresh(), reset_scroll)) {
                m_message.set("Refreshed.");
            }
        }
        ImGui::SameLine();
        // TODO: support record (<</>>).
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("Filter", ".txt", input_filter, std::size(input_filter));

        ImGui::Separator();

        // TODO: cache result.
        ImGui::TextUnformatted(cpp17_u8string_nothrow(m_current.path()).c_str());

        ImGui::Separator();

        constexpr const char* str_id = "##Entry";
        int extra_id = 0;
        if (imgui_SelectableEx(str_id, extra_id++, "Home", false, ImGuiSelectableFlags_NoAutoClosePopups)) {
            test_loaded(m_current.set_path(m_home), reset_scroll);
        }
        // TODO: whether to disable when at root path?
        if (imgui_SelectableEx(str_id, extra_id++, "..", false, ImGuiSelectableFlags_NoAutoClosePopups)) {
            test_loaded(m_current.set_path(".."), reset_scroll);
        }

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
                    ImGui::TextUnformatted("|-");
                    ImGui::SameLine();
                }
                if (imgui_SelectableEx(str_id, extra_id++, entry.str.c_str(), false,
                                       ImGuiSelectableFlags_NoAutoClosePopups)) {
                    sel = &entry;
                }
            }
            if (sel) {
                if (sel->is_file) {
                    test_loaded(load_file(m_current / *sel, data, max_size), file_loaded);
                } else {
                    test_loaded(m_current.set_path(sel->filename), reset_scroll);
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        // TODO: should also accept file path.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x -
                                ImGui::CalcTextSize("Input").x);
        if (ImGui::InputText("Input", input_path, std::size(input_path), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (input_path[0] != '\0' && test_loaded(m_current.set_path(input_path), reset_scroll)) {
                input_path[0] = '\0';
            }
        }

        ImGui::EndChild();
        m_message.display();
        return file_loaded;
    }
};

bool file_loader::display_if_open(std::string& data, const int max_size) {
    if (!open) {
        return false;
    }
    if (!m_impl) {
        m_impl.reset(new file_loader_impl{});
    }
    // Note: str-id is sensitive to the id stack, but ok for the current use.
    constexpr const char* window_name = "Select file";
    if (!ImGui::IsPopupOpen(window_name)) {
        ImGui::OpenPopup(window_name);
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 24));
    if (ImGui::BeginPopupModal(window_name, &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize)) {
        // Modal window also disables scrolling.
        const bool loaded = reinterpret_cast<file_loader_impl*>(m_impl.get())->display(data, max_size);
        ImGui::EndPopup();
        return loaded;
    }
    return false;
}
