#pragma once
/*===========================================================================
  reference-image.hpp — ReferenceStore
  ---------------------------------------------------------------------------
  Loads PNG/JPEG reference images (via stb_image) and stores them keyed by
  a user-chosen string ID.  Used by the Lua API for compare_region calls.
===========================================================================*/

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

struct RefImage {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;      // width * height * 4 bytes

    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

class ReferenceStore {
public:
    // Load an image from disk, convert to RGBA, store under `id`.
    // Returns true on success.
    bool load(const std::string &id, const std::string &path);

    // Remove a previously loaded image.
    void unload(const std::string &id);

    // Retrieve a pointer to the stored image (nullptr if not found).
    // Caller must not hold the image past the next load/unload call without
    // copying — do all work under the lock if multi-threaded.
    const RefImage *get(const std::string &id) const;

    // Scale src_image into a new RefImage of (dst_w × dst_h).
    static RefImage scale(const RefImage &src, int dst_w, int dst_h);

private:
    mutable std::mutex store_mutex_;
    std::unordered_map<std::string, RefImage> images_;
};
