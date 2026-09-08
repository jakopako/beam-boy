#pragma once

// Beam Boy — LittleFS shim for host builds.
//
// An in-memory filesystem, just enough for Storage and the WiFi credential
// file. Being in memory is a feature for tests: each one starts from a blank
// device, and a test can corrupt a file deliberately to check that the
// checksum path really does fall back to defaults.

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace beamboy_host {

// path -> contents. Tests can reach in to seed or corrupt a file. Defined
// inline to keep the shim header-only.
inline std::map<std::string, std::string>& files() {
  static std::map<std::string, std::string> f;
  return f;
}

}  // namespace beamboy_host

class File {
 public:
  File() = default;
  File(const std::string& path, bool writing)
      : path_(path), writing_(writing), open_(true) {
    if (writing) {
      beamboy_host::files()[path] = std::string();
    } else {
      data_ = beamboy_host::files()[path];
    }
  }

  // Directory handle. The host shim has no real directories, so a directory is
  // synthesised from the set of file paths sharing a prefix -- enough to test
  // CartridgeStore's scan without a filesystem image.
  static File directory(const std::string& path) {
    File f;
    f.open_ = true;
    f.path_ = path;
    f.is_dir_ = true;

    const std::string prefix = path == "/" ? path : path + "/";
    std::vector<std::string> seen;
    for (const auto& entry : beamboy_host::files()) {
      if (entry.first.compare(0, prefix.size(), prefix) != 0) continue;

      const std::string rest = entry.first.substr(prefix.size());
      const size_t slash = rest.find('/');
      if (slash == std::string::npos) {
        f.children_.push_back({prefix + rest, false});
      } else {
        // A nested path implies a subdirectory; report it once.
        const std::string child = prefix + rest.substr(0, slash);
        bool already = false;
        for (const auto& s : seen) {
          if (s == child) already = true;
        }
        if (!already) {
          seen.push_back(child);
          f.children_.push_back({child, true});
        }
      }
    }
    return f;
  }

  explicit operator bool() const { return open_; }

  bool isDirectory() const { return is_dir_; }

  const char* name() const { return path_.c_str(); }

  File openNextFile() {
    if (!is_dir_ || next_child_ >= children_.size()) return File();
    const auto& child = children_[next_child_++];
    if (child.second) return File::directory(child.first);
    return File(child.first, false);
  }

  size_t size() const { return writing_ ? 0 : data_.size(); }

  size_t read(uint8_t* dst, size_t len) {
    const size_t n = data_.size() - pos_ < len ? data_.size() - pos_ : len;
    memcpy(dst, data_.data() + pos_, n);
    pos_ += n;
    return n;
  }

  size_t write(const uint8_t* src, size_t len) {
    beamboy_host::files()[path_].append(reinterpret_cast<const char*>(src),
                                        len);
    return len;
  }

  void close() { open_ = false; }

 private:
  std::string path_;
  std::string data_;
  size_t pos_ = 0;
  bool writing_ = false;
  bool open_ = false;
  bool is_dir_ = false;
  // path, is_directory
  std::vector<std::pair<std::string, bool>> children_;
  size_t next_child_ = 0;
};

class LittleFSClass {
 public:
  bool begin(bool = false) { return true; }
  void format() { beamboy_host::files().clear(); }
  bool exists(const char* path) {
    return beamboy_host::files().count(path) > 0;
  }
  bool remove(const char* path) {
    return beamboy_host::files().erase(path) > 0;
  }
  File open(const char* path, const char* mode) {
    const bool writing = mode && mode[0] == 'w';
    if (!writing && !exists(path)) {
      // Not a file -- it may still be a directory prefix. Synthesising it here
      // is what lets CartridgeStore::scan() run unchanged on the host.
      const std::string prefix = std::string(path) + "/";
      for (const auto& entry : beamboy_host::files()) {
        if (entry.first.compare(0, prefix.size(), prefix) == 0) {
          return File::directory(path);
        }
      }
      return File();
    }
    return File(path, writing);
  }
};

inline LittleFSClass& littleFsInstance() {
  static LittleFSClass fs;
  return fs;
}
#define LittleFS (littleFsInstance())
