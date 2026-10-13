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

namespace wasmfs {

const uint32_t DEFAULT_CHUNK_SIZE = 16*1024*1024;

class NetSmartBackend : public wasmfs::ProxiedAsyncJSBackend {
  std::string baseUrl;
  uint32_t chunkSize;
  std::map<std::string, uint32_t> urlToFile;

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

  void updateUrlToFile(const std::string& url, uint32_t file);
  uint32_t getFileFromUrl(const std::string& url);
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
    backend->updateUrlToFile(url, child->getNetSmartFileIndex());

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
    printf("getchild, fetch backend: %s\n", name.c_str());
    return MemoryDirectory::getChild(name);
  }
};

std::shared_ptr<DataFile> NetSmartBackend::createFile(mode_t mode) {
  return std::make_shared<NetSmartFile>("", mode, this, proxy);
}

std::shared_ptr<Directory> NetSmartBackend::createDirectory(mode_t mode) {
  return std::make_shared<NetSmartDirectory>("", mode, this, proxy);
}

const std::string NetSmartBackend::getFileURL(const std::string& filePath) {
  if (filePath == "") {
    return baseUrl;
  }
  return baseUrl + "/" + filePath;
}

uint32_t NetSmartBackend::getChunkSize() {
  return chunkSize;
}

void NetSmartBackend::updateUrlToFile(const std::string& url, uint32_t file) {
  urlToFile[url] = file;
}


uint32_t NetSmartBackend::getFileFromUrl(const std::string& url) {
  auto it = urlToFile.find(url);
  return it != urlToFile.end() ? it->second : 0;
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

uint32_t _wasmfs_netsmart_get_file_from_url(void* ptr, const char* url) {
  auto* backend = reinterpret_cast<wasmfs::NetSmartBackend*>(ptr);
  return backend ? backend->getFileFromUrl(url) : 0;
}

uint32_t _wasmfs_netsmart_get_suggestor_url(void* ptr) {
  auto* backend = reinterpret_cast<wasmfs::NetSmartBackend*>(ptr);
  return backend ? backend->manifest.getPrefetcherServer().c_str();
}

}

} // namespace wasmfs
