#include "ui/texture_cache.hpp"

#include <algorithm>
#include <vector>

#include <GLFW/glfw3.h>  // kéo theo GL/gl.h; chỉ dùng hàm OpenGL 1.1 nên không cần loader

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace pdfread::ui {
namespace {

std::size_t texture_bytes(int w, int h) {
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;  // driver thường lưu RGBA
}

}  // namespace

TextureCache::TextureCache(std::size_t budget_bytes) : budget_(budget_bytes) {}

TextureCache::~TextureCache() {
    clear();
    flush_deleted();
}

const TileTexture* TextureCache::use(const pdf::TileKey& key, std::uint64_t frame) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    it->second.last_used = frame;
    return &it->second;
}

void TextureCache::insert(const pdf::TileKey& key, const pdf::Pixmap* pixmap, std::uint64_t frame) {
    if (auto it = entries_.find(key); it != entries_.end()) {
        destroy(it->second);
        entries_.erase(it);
    }
    TileTexture t;
    t.last_used = frame;
    if (pixmap != nullptr && pixmap->components() == 3) {
        t.width = pixmap->width();
        t.height = pixmap->height();
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pixmap->stride() / 3));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, t.width, t.height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixmap->samples());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        t.texture = tex;
        bytes_ += texture_bytes(t.width, t.height);
    }
    entries_.emplace(key, t);
}

void TextureCache::for_each_of_page(std::uint64_t doc, int page,
                                    const std::function<void(const pdf::TileKey&, TileTexture&)>& fn) {
    for (auto& [key, tex] : entries_) {
        if (key.doc == doc && key.page == page) fn(key, tex);
    }
}

void TextureCache::evict(std::uint64_t frame) {
    if (bytes_ <= budget_) return;
    std::vector<std::pair<std::uint64_t, pdf::TileKey>> candidates;
    candidates.reserve(entries_.size());
    for (const auto& [key, tex] : entries_) {
        if (tex.last_used != frame) candidates.emplace_back(tex.last_used, key);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [_, key] : candidates) {
        if (bytes_ <= budget_) break;
        auto it = entries_.find(key);
        destroy(it->second);
        entries_.erase(it);
    }
}

void TextureCache::clear() {
    for (auto& [_, tex] : entries_) destroy(tex);
    entries_.clear();
    bytes_ = 0;
}

void TextureCache::destroy(TileTexture& t) {
    if (t.texture != 0) {
        deleted_.push_back(t.texture);
        bytes_ -= texture_bytes(t.width, t.height);
        t.texture = 0;
    }
}

void TextureCache::flush_deleted() {
    if (deleted_.empty()) return;
    std::vector<GLuint> names(deleted_.begin(), deleted_.end());
    glDeleteTextures(static_cast<GLsizei>(names.size()), names.data());
    deleted_.clear();
}

}  // namespace pdfread::ui
