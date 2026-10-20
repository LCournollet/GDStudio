#include "Files.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#if __has_include(<asp/fs/std.hpp>)
#include <asp/fs/std.hpp>
#endif

#include <algorithm>
#include <fstream>
#include <sstream>
#include <type_traits>

namespace {

using GeodePath = std::decay_t<decltype(geode::Mod::get()->getSaveDir())>;

template <class P>
std::filesystem::path toStd(P const& p) {
    if constexpr (std::is_same_v<P, std::filesystem::path>) return p;
    else return p.toStdPath().unwrapOr(std::filesystem::path{});
}

template <class P>
P fromStd(std::filesystem::path const& p) {
    if constexpr (std::is_same_v<P, std::filesystem::path>) return p;
    else return P::fromStdPath(p);
}

}  // namespace

namespace files {

std::filesystem::path saveDir() { return toStd(geode::Mod::get()->getSaveDir()); }

std::filesystem::path configDir() { return toStd(geode::Mod::get()->getConfigDir()); }

std::filesystem::path ensureDir(std::filesystem::path const& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::optional<std::string> readText(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return std::nullopt;
    return std::move(ss).str();
}

bool writeTextSafe(std::filesystem::path const& file, std::string const& text, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot open " + fileName(tmp));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) return fail("cannot write " + fileName(tmp));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);  // replaces the target on Windows and POSIX
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return fail("cannot replace " + fileName(file));
    }
    return true;
}

std::vector<std::filesystem::path> listFiles(std::filesystem::path const& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string fileName(std::filesystem::path const& file) {
    auto u8 = file.filename().u8string();
    return std::string(u8.begin(), u8.end());
}

void openFolder(std::filesystem::path const& dir) { geode::utils::file::openFolder(fromStd<GeodePath>(dir)); }

}  // namespace files
