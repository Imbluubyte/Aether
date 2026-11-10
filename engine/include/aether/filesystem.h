// Read-only game asset access and writable save storage. Both are interfaces so the Android
// backend can read from APK assets while desktop/headless read from a directory.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aether {

// Normalises a game-relative path: converts '\\' to '/', strips leading "./" and '/', and returns
// false for paths that escape the root ("..").
bool normalizePath(const std::string& in, std::string& out);

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool readFile(const std::string& path, std::vector<uint8_t>& out) = 0;
    virtual bool exists(const std::string& path) = 0;
    bool readText(const std::string& path, std::string& out);
};

class DirFileSystem : public IFileSystem {
public:
    explicit DirFileSystem(std::string root);
    bool readFile(const std::string& path, std::vector<uint8_t>& out) override;
    bool exists(const std::string& path) override;
    const std::string& root() const { return root_; }

private:
    std::string root_;
};

// Writable per-game storage (save files). Names are flat file names, no directories.
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool write(const std::string& name, const std::string& data) = 0;
    virtual bool read(const std::string& name, std::string& out) = 0;
    virtual bool remove(const std::string& name) = 0;
    virtual bool exists(const std::string& name) = 0;
    virtual std::vector<std::string> list() = 0;
};

class DirStorage : public IStorage {
public:
    explicit DirStorage(std::string dir);
    bool write(const std::string& name, const std::string& data) override;
    bool read(const std::string& name, std::string& out) override;
    bool remove(const std::string& name) override;
    bool exists(const std::string& name) override;
    std::vector<std::string> list() override;

private:
    bool valid(const std::string& name) const;
    std::string dir_;
};

// In-memory storage, used by tests so they never touch the disk.
class MemoryStorage : public IStorage {
public:
    bool write(const std::string& name, const std::string& data) override;
    bool read(const std::string& name, std::string& out) override;
    bool remove(const std::string& name) override;
    bool exists(const std::string& name) override;
    std::vector<std::string> list() override;

private:
    std::vector<std::pair<std::string, std::string>> files_;
};

}  // namespace aether
