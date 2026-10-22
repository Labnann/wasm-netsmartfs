// Copyright 2021 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

// This file defines the fetch backend.

#include "netsmart_backend.h"
#include "backend.h"
#include "proxied_async_js_impl_backend.h"
#include "wasmfs.h"
#include "manifest.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

namespace wasmfs {

const uint32_t DEFAULT_CHUNK_SIZE = 16*1024*1024;
class NetSmartDirectory;


class NetSmartBackend : public wasmfs::ProxiedAsyncJSBackend {
  std::string baseUrl;
  uint32_t chunkSize;
  std::map<std::string, uint32_t> pathToFileMap;
  bool manifestPopulated = false;

 public:
  Manifest manifest;

  NetSmartBackend(
      const std::string& baseUrl,
      uint32_t chunkSize,
      std::function<void(backend_t)> setupOnThread)
      : ProxiedAsyncJSBackend(setupOnThread),
        baseUrl(baseUrl),
        chunkSize(chunkSize),
        manifest(baseUrl)
  {}

  std::shared_ptr<DataFile> createFile(mode_t mode) override;
  std::shared_ptr<Directory> createDirectory(mode_t mode) override;
  void populateManifestOnce(NetSmartDirectory* root);

  const std::string getFileURL(const std::string& filePath);
  uint32_t getChunkSize();

  void updatePathToFileMap(const std::string& url, uint32_t file);
  uint32_t getFileFromPath(const std::string& url);
};

class NetSmartFile : public ProxiedAsyncJSImplFile {
  std::string filePath;
  std::string fileUrl;


public:
  uint32_t getNetSmartFileIndex() {
    static_assert(sizeof(this) == sizeof(uint32_t));
    return uint32_t(this);
  }

  NetSmartFile(const std::string& path,
            mode_t mode,
            backend_t backend,
            emscripten::ProxyWorker& proxy)
    : ProxiedAsyncJSImplFile(mode, backend, proxy), filePath(path) {
    this->fileUrl = dynamic_cast<NetSmartBackend*>(getBackend())->getFileURL(filePath);
  }

  const std::string& getPath() const { return filePath; }
  const std::string& getURL() const { return fileUrl; }
  const uint32_t getChunkSize() const { return dynamic_cast<NetSmartBackend*>(getBackend())->getChunkSize(); }
};

class NetSmartDirectory : public MemoryDirectory {
  std::string dirPath;
  emscripten::ProxyWorker& proxy;

public:
  NetSmartDirectory(const std::string& path,
                 mode_t mode,
                 backend_t backend,
                 emscripten::ProxyWorker& proxy)
    : MemoryDirectory(mode, backend), dirPath(path), proxy(proxy) {}

  std::shared_ptr<DataFile> insertDataFile(const std::string& name,
                                           mode_t mode) override {
    auto childPath = getChildPath(name);
    auto backend = dynamic_cast<NetSmartBackend*>(getBackend());
    auto child =
      std::make_shared<NetSmartFile>(childPath, mode, backend, proxy);

    backend->updatePathToFileMap(childPath, child->getNetSmartFileIndex());

    insertChild(name, child);
    return child;
  }

  std::shared_ptr<Directory> insertDirectory(const std::string& name,
                                             mode_t mode) override {
    auto childPath = getChildPath(name);
    auto childDir =
      std::make_shared<NetSmartDirectory>(childPath, mode, getBackend(), proxy);
    insertChild(name, childDir);
   //printf("getchild: insertdir: finished: %s\n", childPath.c_str());
    return childDir;
  }

  std::string getChildPath(const std::string& name) const {
    return dirPath + '/' + name;
  }

  NetSmartDirectory* ensureDirectory(const std::string& path) {
    NetSmartDirectory* current = this;
    size_t start = 0;

    while (start < path.size()) {
      size_t end = path.find('/', start);
      if (end == std::string::npos)
        end = path.size();

      std::string name = path.substr(start, end - start);
      if (!name.empty()) {
        auto child = current->MemoryDirectory::getChild(name);
        auto dir = std::dynamic_pointer_cast<NetSmartDirectory>(child);

        if (!dir) {
          dir = std::dynamic_pointer_cast<NetSmartDirectory>(
              current->insertDirectory(name, 0777));
        }

        current = dir.get();
      }

      start = end + 1;
    }

    return current;
  }

  void populateFromManifest() {
   //printf("populateFromManifest: dirPath='%s'\n", dirPath.c_str());
    auto* backend = dynamic_cast<NetSmartBackend*>(getBackend());

    for (const auto& path : backend->manifest.getDirectories()) {
      std::string dir = path;
      while (!dir.empty() && dir.back() == '/')
        dir.pop_back();

      ensureDirectory(dir);
    }

    for (const auto& [path, url] : backend->manifest.getFiles()) {
      size_t pos = path.find_last_of('/');
      std::string dir =
          (pos == std::string::npos) ? "" : path.substr(0, pos);
      std::string filename =
          (pos == std::string::npos) ? path : path.substr(pos + 1);

      if (!filename.empty())
        ensureDirectory(dir)->insertDataFile(filename, 0777);
    }
  }


  std::shared_ptr<File> getChild(const std::string& name) override {
   //printf("getChild: dirPath='%s', name='%s'\n", dirPath.c_str(), name.c_str());
    return MemoryDirectory::getChild(name);
  }

};

void NetSmartBackend::populateManifestOnce(NetSmartDirectory* root) {
  if (manifestPopulated)
    return;

  root->populateFromManifest();
  manifestPopulated = true;
}


std::shared_ptr<DataFile> NetSmartBackend::createFile(mode_t mode) {
  return std::make_shared<NetSmartFile>("", mode, this, proxy);
}

std::shared_ptr<Directory> NetSmartBackend::createDirectory(mode_t mode) {
 //printf("creating rootdir, populated: %d\n", manifestPopulated);
  auto root =
      std::make_shared<NetSmartDirectory>("", mode, this, proxy);
  populateManifestOnce(root.get());
  return root;
}


const std::string NetSmartBackend::getFileURL(const std::string& filePath) {
  std::string path = filePath;
  if (!path.empty() && path[0] == '/') {
    path.erase(0, 1);
  }

  auto it = manifest.getFiles().find(path);
  std::string url = (it != manifest.getFiles().end()) ? it->second : "";

  return url;
}


uint32_t NetSmartBackend::getChunkSize() {
  return chunkSize;
}

void NetSmartBackend::updatePathToFileMap(const std::string& path, uint32_t file) {
  pathToFileMap[path] = file;
  //printf("netsmart_backend: update: %s, fd: %d\n", path.c_str(), file);
}


uint32_t NetSmartBackend::getFileFromPath(const std::string& path) {
  auto it = pathToFileMap.find(path);
  auto file =it != pathToFileMap.end() ? it->second : 0;
  //printf("netsmart_backend: search: %s, result: %d\n", path.c_str(), file);
  return file;
}

extern "C" {
  backend_t wasmfs_create_netsmart_backend(const char* base_url, uint32_t chunkSize) {
  // ProxyWorker cannot safely be synchronously spawned from the main browser
  // thread. See comment in thread_utils.h for more details.
  assert(!emscripten_is_main_browser_thread() &&
         "Cannot safely create fetch backend on main browser thread");
  return wasmFS.addBackend(std::make_unique<NetSmartBackend>(
    base_url ? base_url : "",
    chunkSize ? chunkSize : DEFAULT_CHUNK_SIZE,
    [](backend_t backend) { _wasmfs_create_netsmart_backend_js(backend); }));
  }

const char* _wasmfs_netsmart_get_file_url(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::NetSmartFile*>(ptr);
  return file ? file->getURL().data() : nullptr;
}

const char* _wasmfs_netsmart_get_file_path(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::NetSmartFile*>(ptr);
  return file->getPath().c_str();
}


uint32_t _wasmfs_netsmart_get_chunk_size(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::NetSmartFile*>(ptr);
  return file ? file->getChunkSize() : DEFAULT_CHUNK_SIZE;
}

uint32_t _wasmfs_netsmart_get_file_from_path(void* ptr, const char* path) {
  auto* backend = reinterpret_cast<wasmfs::NetSmartBackend*>(ptr);
  return backend ? backend->getFileFromPath(path) : 0;
}

const char* _wasmfs_netsmart_get_suggestor_url(void* ptr) {
  auto* backend = reinterpret_cast<wasmfs::NetSmartBackend*>(ptr);
  if (!backend){
    perror("FATAL: Backend not found.\n");
    return nullptr;
  }
  return backend->manifest.getPrefetcherServer().c_str();
}

}

} // namespace wasmfs
