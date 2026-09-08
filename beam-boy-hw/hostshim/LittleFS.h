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

  explicit operator bool() const { return open_; }

  size_t size() const { return writing_ ? 0 : data_.size(); }

  size_t read(uint8_t* dst, size_t len) {
    const size_t n = data_.size() - pos_ < len ? data_.size() - pos_ : len;
    memcpy(dst, data_.data() + pos_, n);
    pos_ += n;
    return n;
  }

  size_t write(const uint8_t* src, size_t len) {
    beamboy_host::files()[path_].append(reinterpret_cast<const char*>(src), len);
    return len;
  }

  void close() { open_ = false; }

 private:
  std::string path_;
  std::string data_;
  size_t pos_ = 0;
  bool writing_ = false;
  bool open_ = false;
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
    if (!writing && !exists(path)) return File();
    return File(path, writing);
  }
};

inline LittleFSClass& littleFsInstance() {
  static LittleFSClass fs;
  return fs;
}
#define LittleFS (littleFsInstance())
