#pragma once

// Chunked sparse 2D grid. Levels can be very long (tens of thousands of
// blocks) and objects may sit far away from the gameplay (hidden helpers,
// trigger stacks...), so a dense bounding-box array is not an option.

#include "Geometry.hpp"

#include <array>
#include <memory>
#include <unordered_map>

namespace gdds {

struct IRect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // inclusive bounds
    bool isEmpty() const { return x1 < x0 || y1 < y0; }
    void include(int x, int y) {
        if (isEmpty()) { x0 = x1 = x; y0 = y1 = y; return; }
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    bool contains(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    int width() const { return isEmpty() ? 0 : x1 - x0 + 1; }
    int height() const { return isEmpty() ? 0 : y1 - y0 + 1; }
};

template <class T, int ChunkBits = 6>
class SparseGrid {
public:
    static constexpr int kChunk = 1 << ChunkBits;
    static constexpr int kMask = kChunk - 1;

    explicit SparseGrid(T def = T{}) : m_default(def) {}

    T get(int x, int y) const {
        Chunk const* c = findChunk(x >> ChunkBits, y >> ChunkBits);
        return c ? c->data[index(x, y)] : m_default;
    }
    T get(Cell c) const { return get(c.x, c.y); }

    T& at(int x, int y) {
        int cx = x >> ChunkBits, cy = y >> ChunkBits;
        Chunk* c = findChunk(cx, cy);
        if (!c) {
            auto chunk = std::make_unique<Chunk>();
            chunk->data.fill(m_default);
            c = chunk.get();
            m_chunks.emplace(Cell{cx, cy}, std::move(chunk));
            m_bounds.include(cx * kChunk, cy * kChunk);
            m_bounds.include(cx * kChunk + kMask, cy * kChunk + kMask);
            m_cacheKey = {cx, cy};
            m_cache = c;
        }
        return c->data[index(x, y)];
    }
    void set(int x, int y, T v) { at(x, y) = v; }
    void set(Cell c, T v) { at(c.x, c.y) = v; }

    // Bounds of the allocated chunks (cells outside are guaranteed default).
    IRect allocatedBounds() const { return m_bounds; }
    bool empty() const { return m_chunks.empty(); }
    void clear() { m_chunks.clear(); m_bounds = {}; m_cache = nullptr; }

    // Calls f(x, y, value) for every non-default cell.
    template <class F>
    void forEach(F&& f) const {
        for (auto& [key, chunk] : m_chunks) {
            for (int ly = 0; ly < kChunk; ++ly) {
                for (int lx = 0; lx < kChunk; ++lx) {
                    T const& v = chunk->data[ly * kChunk + lx];
                    if (!(v == m_default)) f(key.x * kChunk + lx, key.y * kChunk + ly, v);
                }
            }
        }
    }

private:
    struct Chunk { std::array<T, kChunk * kChunk> data; };

    static int index(int x, int y) { return (y & kMask) * kChunk + (x & kMask); }

    Chunk* findChunk(int cx, int cy) const {
        if (m_cache && m_cacheKey.x == cx && m_cacheKey.y == cy) return m_cache;
        auto it = m_chunks.find(Cell{cx, cy});
        if (it == m_chunks.end()) return nullptr;
        m_cacheKey = {cx, cy};
        m_cache = it->second.get();
        return m_cache;
    }

    T m_default;
    std::unordered_map<Cell, std::unique_ptr<Chunk>, CellHash> m_chunks;
    IRect m_bounds;
    mutable Cell m_cacheKey{INT32_MIN, INT32_MIN};
    mutable Chunk* m_cache = nullptr;
};

}  // namespace gdds
