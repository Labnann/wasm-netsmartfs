#include "manifest.h"
#include "ini_parser/ini_parser.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <emscripten/fetch.h>

Manifest::Manifest(const std::string& baseUrl)
    : baseUrl(baseUrl)
{
    std::string url = this->baseUrl;

    if (!url.empty() && url.back() == '/')
        url.pop_back();

    url += "/MANIFEST";

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);

    std::strcpy(attr.requestMethod, "GET");
    attr.attributes =
        EMSCRIPTEN_FETCH_LOAD_TO_MEMORY |
        EMSCRIPTEN_FETCH_SYNCHRONOUS;

    emscripten_fetch_t* fetch = emscripten_fetch(&attr, url.c_str());

    if (!fetch) {
        std::fprintf(stderr, "Failed to fetch MANIFEST: %s\n",
                     url.c_str());
        return;
    }

    if (fetch->status >= 200 && fetch->status < 300) {
        std::string contents(fetch->data, fetch->numBytes);

        int result = ini_parse(
            contents.c_str(),
            Manifest::parseEntry,
            this
        );

        if (result != 0) {
            std::fprintf(stderr, "Failed to parse MANIFEST: %d\n", result);
        }
    } else {
        std::fprintf(stderr, "Failed to fetch MANIFEST: HTTP %hu\n",
                     fetch->status);
    }

    emscripten_fetch_close(fetch);
}

int Manifest::parseEntry(
    const char* type,
    const char* key,
    const char* value,
    void* userdata)
{
    auto* manifest = static_cast<Manifest*>(userdata);

    if (std::strcmp(type, "prefetcher") == 0) {
        if (std::strcmp(key, "server") == 0) {
            manifest->prefetcherServer = value;
        }
    } else if (std::strcmp(type, "directories") == 0) {
        std::string path = key;

        while (!path.empty() && path.front() == '/')
            path.erase(path.begin());

        while (!path.empty() && path.back() == '/')
            path.pop_back();

        manifest->directories.emplace_back(path);
        manifest->directorySet.emplace(std::move(path));
    } else if (std::strcmp(type, "files") == 0) {
        std::string path = key;

        while (!path.empty() && path.front() == '/')
            path.erase(path.begin());

        while (!path.empty() && path.back() == '/')
            path.pop_back();

        std::string url = value;

        if (url.empty()) {
            url = manifest->baseUrl;

            if (!url.empty() && url.back() != '/')
                url += '/';

            url += key;
        }

        manifest->files[std::move(path)] = std::move(url);
    }

    return 0;
}

Manifest::FileType Manifest::exists_file(const std::string& path) const
{
    std::size_t first = path.find_first_not_of('/');
    if (first == std::string::npos)
        return NO;

    std::size_t last = path.find_last_not_of('/');

    std::string normalized = path.substr(first, last - first + 1);

    if (files.find(normalized) != files.end())
        return DATA_FILE;

    if (directorySet.find(normalized) != directorySet.end())
        return DIRECTORY_FILE;

    return NO;
}

const std::string& Manifest::getPrefetcherServer() const
{
    return prefetcherServer;
}

const std::vector<std::string>& Manifest::getDirectories() const
{
    return directories;
}

const std::map<std::string, std::string>& Manifest::getFiles() const
{
    return files;
}
