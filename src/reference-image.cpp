/*===========================================================================
  reference-image.cpp
===========================================================================*/

// Instantiate stb_image here (single-TU include of the implementation)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb/stb_image.h"

#include "reference-image.hpp"
#include <obs-module.h>  // blog()

#include <cstring>
#include <cmath>
#include <algorithm>

// ────────────────────────────────────────────────────────────────────────────
// Load / unload
// ────────────────────────────────────────────────────────────────────────────

bool ReferenceStore::load(const std::string &id, const std::string &path)
{
    if (id.empty() || path.empty()) return false;

    int w = 0, h = 0, channels = 0;
    // Force 4-channel RGBA output
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        blog(LOG_WARNING,
             "[obs-framebridge] Failed to load reference image '%s': %s",
             path.c_str(), stbi_failure_reason());
        return false;
    }

    RefImage img;
    img.width  = w;
    img.height = h;
    img.rgba.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);

    {
        std::lock_guard<std::mutex> lk(store_mutex_);
        images_[id] = std::move(img);
    }

    blog(LOG_INFO,
         "[obs-framebridge] Loaded reference image '%s' from '%s' (%d×%d)",
         id.c_str(), path.c_str(), w, h);
    return true;
}

void ReferenceStore::unload(const std::string &id)
{
    std::lock_guard<std::mutex> lk(store_mutex_);
    images_.erase(id);
    blog(LOG_INFO, "[obs-framebridge] Unloaded reference image '%s'", id.c_str());
}

const RefImage *ReferenceStore::get(const std::string &id) const
{
    std::lock_guard<std::mutex> lk(store_mutex_);
    auto it = images_.find(id);
    return (it != images_.end()) ? &it->second : nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
// Scale helper — bilinear downsample/upsample to dst_w × dst_h
// ────────────────────────────────────────────────────────────────────────────

RefImage ReferenceStore::scale(const RefImage &src, int dst_w, int dst_h)
{
    RefImage dst;
    if (!src.valid() || dst_w <= 0 || dst_h <= 0) return dst;

    dst.width  = dst_w;
    dst.height = dst_h;
    dst.rgba.resize((size_t)dst_w * dst_h * 4);

    float x_ratio = (float)src.width  / (float)dst_w;
    float y_ratio = (float)src.height / (float)dst_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        float sy_f = (dy + 0.5f) * y_ratio - 0.5f;
        int   sy0  = std::max(0, (int)sy_f);
        int   sy1  = std::min(src.height - 1, sy0 + 1);
        float wy   = sy_f - (float)sy0;
        if (wy < 0.0f) wy = 0.0f;

        for (int dx = 0; dx < dst_w; ++dx) {
            float sx_f = (dx + 0.5f) * x_ratio - 0.5f;
            int   sx0  = std::max(0, (int)sx_f);
            int   sx1  = std::min(src.width - 1, sx0 + 1);
            float wx   = sx_f - (float)sx0;
            if (wx < 0.0f) wx = 0.0f;

            uint8_t *out = dst.rgba.data() + ((size_t)dy * dst_w + dx) * 4;

            const uint8_t *p00 = src.rgba.data() + ((size_t)sy0 * src.width + sx0) * 4;
            const uint8_t *p01 = src.rgba.data() + ((size_t)sy0 * src.width + sx1) * 4;
            const uint8_t *p10 = src.rgba.data() + ((size_t)sy1 * src.width + sx0) * 4;
            const uint8_t *p11 = src.rgba.data() + ((size_t)sy1 * src.width + sx1) * 4;

            for (int c = 0; c < 4; ++c) {
                float v = (float)p00[c] * (1.0f - wx) * (1.0f - wy)
                        + (float)p01[c] *         wx  * (1.0f - wy)
                        + (float)p10[c] * (1.0f - wx) *         wy
                        + (float)p11[c] *         wx  *         wy;
                out[c] = (uint8_t)std::max(0.0f, std::min(255.0f, v));
            }
        }
    }

    return dst;
}
