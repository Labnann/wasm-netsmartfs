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


class NetSmartBackend : public wasmfs::ProxiedAsyncJSBackend {
  std::string baseUrl;
  uint32_t chunkSize;
  std::map<std::string, uint32_t> pathToFileMap;

  std::unordered_map<
      std::string,
      std::vector<std::pair<uint32_t, std::string>>
  > pending_directory_updates;

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

  const std::string getFileURL(const std::string& filePath);
  uint32_t getChunkSize();

  void updatePathToFileMap(const std::string& url, uint32_t file);
  uint32_t getFileFromPath(const std::string& url);

  uint32_t createfile_and_add_to_pending_directory_updates(
      const std::string& dirpath,
      const std::string& filename);

  std::vector<std::pair<uint32_t, std::string>>
  consume_pending_directory_updates(const std::string& dirpath)
  {
    auto it = pending_directory_updates.find(dirpath);
    if (it == pending_directory_updates.end())
      return {};

    auto updates = std::move(it->second);
    pending_directory_updates.erase(it);
    return updates;
  }

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
    printf("created path: %s, with url: %s\n", path.c_str(), this->fileUrl.c_str());
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
    printf("crf into dir: %s  name: %s \n", dirPath.c_str(), name.c_str());

    auto url = backend->getFileURL(childPath);
    backend->updatePathToFileMap(url, child->getNetSmartFileIndex());

    insertChild(name, child);
    return child;
  }

  std::shared_ptr<Directory> insertDirectory(const std::string& name,
                                             mode_t mode) override {
    auto childPath = getChildPath(name);
    auto childDir =
      std::make_shared<NetSmartDirectory>(childPath, mode, getBackend(), proxy);
    printf("crd into dir: %s  name: %s \n", dirPath.c_str(), name.c_str());
    insertChild(name, childDir);
    return childDir;
  }

  std::string getChildPath(const std::string& name) const {
    return dirPath + '/' + name;
  }

  std::shared_ptr<File> getChild(const std::string& name) override {
      auto* backend = dynamic_cast<NetSmartBackend*>(getBackend());
      // update any pending updates from backend to the directory
      for (const auto& [file, filename] :
          backend->consume_pending_directory_updates(dirPath)) {
          insertDataFile(filename, 0777);
      }

      auto child = MemoryDirectory::getChild(name);
      if (child)
          return child;
  
      auto childPath = getChildPath(name);
  
      switch (backend->manifest.exists_file(childPath)) {
        case Manifest::DATA_FILE:
          printf("childpath: getattempt: datafile %s\n", childPath.c_str());
          insertDataFile(name, 0777);
          break;
        case Manifest::DIRECTORY_FILE:
          printf("childpath: getattempt: directory %s\n", childPath.c_str());
          insertDirectory(name, 0777);
          break;
        case Manifest::NO:
          printf("childpath: getattempt: ENOFOUND %s\n", childPath.c_str());
          break;
      }

      return MemoryDirectory::getChild(name);
  }
};

std::shared_ptr<DataFile> NetSmartBackend::createFile(mode_t mode) {
  return std::make_shared<NetSmartFile>("", mode, this, proxy);
}

std::shared_ptr<Directory> NetSmartBackend::createDirectory(mode_t mode) {
  return std::make_shared<NetSmartDirectory>("", mode, this, proxy);
}


uint32_t NetSmartBackend::createfile_and_add_to_pending_directory_updates(
    const std::string& dirpath,
    const std::string& filename)
{
  auto file = std::make_shared<NetSmartFile>(filename, 0777, this, proxy);
  uint32_t id = reinterpret_cast<uintptr_t>(file.get());
  pending_directory_updates[dirpath].emplace_back(id, filename);
  return id;
}

const std::string NetSmartBackend::getFileURL(const std::string& filePath) {
  std::string path = filePath;
  if (!path.empty() && path[0] == '/') {
    path.erase(0, 1);
  }

  auto it = manifest.getFiles().find(path);
  std::string url = (it != manifest.getFiles().end()) ? it->second : "";

  printf("path: %s, url: %s\n", filePath.c_str(), url.c_str());
  return url;
}


uint32_t NetSmartBackend::getChunkSize() {
  return chunkSize;
}

void NetSmartBackend::updatePathToFileMap(const std::string& url, uint32_t file) {
  pathToFileMap[url] = file;
}


uint32_t NetSmartBackend::getFileFromPath(const std::string& url) {
  auto it = pathToFileMap.find(url);
  return it != pathToFileMap.end() ? it->second : 0;
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

uint32_t _wasmfs_netsmart_get_chunk_size(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::NetSmartFile*>(ptr);
  return file ? file->getChunkSize() : DEFAULT_CHUNK_SIZE;
}

uint32_t _wasmfs_netsmart_get_file_from_path(void* ptr, const char* path) {
  auto* backend = reinterpret_cast<wasmfs::NetSmartBackend*>(ptr);
  uint32_t file = backend ? backend->getFileFromPath(path) : 0;

  if (!file && backend) {
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of('/');

    std::string dir = (pos == std::string::npos) ? "" : fullPath.substr(0, pos);
    std::string filename = (pos == std::string::npos) ? fullPath : fullPath.substr(pos + 1);

    if (backend->manifest.exists_file(path) == Manifest::DATA_FILE) {
      file = backend->createfile_and_add_to_pending_directory_updates(
          dir, filename);
    }
  }

  return file;
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
