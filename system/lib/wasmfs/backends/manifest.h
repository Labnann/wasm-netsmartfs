
#ifndef NETSMART_MANIFEST_H
#define NETSMART_MANIFEST_H

#include <map>
#include <string>
#include <vector>
#include <emscripten/fetch.h>

class Manifest {
  std::string baseUrl;
  std::string prefetcherServer;
  std::vector<std::string> directories;
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

  const std::string& getPrefetcherServer() const;
  const std::vector<std::string>& getDirectories() const;
  const std::map<std::string, std::string>& getFiles() const;
};

#endif

