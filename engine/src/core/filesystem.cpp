#include "aether/filesystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace aether {

bool normalizePath(const std::string& in, std::string& out) {
    std::string p = in;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::vector<std::string> parts;
    std::stringstream ss(p);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
            continue;
        }
        parts.push_back(seg);
    }
    out.clear();
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += '/';
        out += parts[i];
    }
    return !out.empty();
}

bool IFileSystem::readText(const std::string& path, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    // Strip UTF-8 BOM so Lua can load files saved by Windows editors.
    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF) out.erase(0, 3);
    return true;
}

DirFileSystem::DirFileSystem(std::string root) : root_(std::move(root)) {}

bool DirFileSystem::readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::string norm;
    if (!normalizePath(path, norm)) return false;
    std::ifstream f(fs::path(root_) / norm, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return (bool)f;
}

bool DirFileSystem::exists(const std::string& path) {
    std::string norm;
    if (!normalizePath(path, norm)) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::path(root_) / norm, ec);
}

DirStorage::DirStorage(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

bool DirStorage::valid(const std::string& name) const {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return name != "." && name != ".." && name.find("..") == std::string::npos;
}

bool DirStorage::write(const std::string& name, const std::string& data) {
    if (!valid(name)) return false;
    // Write to a temp file then rename so a crash mid-save never corrupts the previous save.
    fs::path final = fs::path(dir_) / name;
    fs::path tmp = fs::path(dir_) / (name + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, final, ec);
    return !ec;
}

bool DirStorage::read(const std::string& name, std::string& out) {
    if (!valid(name)) return false;
    std::ifstream f(fs::path(dir_) / name, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool DirStorage::remove(const std::string& name) {
    if (!valid(name)) return false;
    std::error_code ec;
    return fs::remove(fs::path(dir_) / name, ec);
}

bool DirStorage::exists(const std::string& name) {
    if (!valid(name)) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir_) / name, ec);
}

std::vector<std::string> DirStorage::list() {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file()) continue;
        std::string n = e.path().filename().string();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0) continue;
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool MemoryStorage::write(const std::string& name, const std::string& data) {
    if (name.empty()) return false;
    for (auto& f : files_)
        if (f.first == name) { f.second = data; return true; }
    files_.emplace_back(name, data);
    return true;
}
bool MemoryStorage::read(const std::string& name, std::string& out) {
    for (auto& f : files_)
        if (f.first == name) { out = f.second; return true; }
    return false;
}
bool MemoryStorage::remove(const std::string& name) {
    for (size_t i = 0; i < files_.size(); i++)
        if (files_[i].first == name) { files_.erase(files_.begin() + (long)i); return true; }
    return false;
}
bool MemoryStorage::exists(const std::string& name) {
    for (auto& f : files_)
        if (f.first == name) return true;
    return false;
}
std::vector<std::string> MemoryStorage::list() {
    std::vector<std::string> n;
    for (auto& f : files_) n.push_back(f.first);
    std::sort(n.begin(), n.end());
    return n;
}

}  // namespace aether
