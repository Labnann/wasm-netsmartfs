
#ifndef NETSMART_MANIFEST_H
#define NETSMART_MANIFEST_H

#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <emscripten/fetch.h>

class Manifest {
 public:
  enum FileType {
    NO,
    DATA_FILE,
    DIRECTORY_FILE
  };

 private:
  std::string baseUrl;
  std::string prefetcherServer;
  std::vector<std::string> directories;
  std::unordered_set<std::string> directorySet;
  std::map<std::string, std::string> files;

  static int parseEntry(
      const char* type,
      const char* key,
      const char* value,
      void* userdata);

  static void onLoaded(emscripten_fetch_t* fetch);
  static void onError(emscripten_fetch_t* fetch);

 public:
  explicit Manifest(const std::string& baseUrl);

  FileType exists_file(const std::string& path) const;

  const std::string& getPrefetcherServer() const;
  const std::vector<std::string>& getDirectories() const;
  const std::map<std::string, std::string>& getFiles() const;
};

#endif
