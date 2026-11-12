#pragma once
// Lưới tile của một trang ở một tỉ lệ render. Dùng chung cho UI (chọn tile cần vẽ)
// và luồng render (vùng pixel cần render) để hai bên luôn khớp nhau tới từng pixel.

#include <algorithm>
#include <cmath>

#include "pdf/document.hpp"

namespace pdfread::pdf {

// Cạnh tile (pixel thiết bị).
inline constexpr int kTileSize = 512;
// Tỉ lệ render lượng tử hóa: scale (pixel/point) = scale_q / kScaleDenominator.
inline constexpr int kScaleDenominator = 4096;

[[nodiscard]] inline double scale_from_q(int scale_q) noexcept {
    return static_cast<double>(scale_q) / kScaleDenominator;
}

struct TileGrid {
    int width_px = 0;
    int height_px = 0;
    int cols = 0;
    int rows = 0;
};

[[nodiscard]] inline TileGrid tile_grid(double page_w_pt, double page_h_pt, int scale_q) noexcept {
    const double s = scale_from_q(scale_q);
    TileGrid g;
    g.width_px = std::max(1, static_cast<int>(std::ceil(page_w_pt * s - 1e-6)));
    g.height_px = std::max(1, static_cast<int>(std::ceil(page_h_pt * s - 1e-6)));
    g.cols = (g.width_px + kTileSize - 1) / kTileSize;
    g.rows = (g.height_px + kTileSize - 1) / kTileSize;
    return g;
}

[[nodiscard]] inline IRect tile_rect(const TileGrid& g, int col, int row) noexcept {
    return IRect{
        col * kTileSize,
        row * kTileSize,
        std::min(g.width_px, (col + 1) * kTileSize),
        std::min(g.height_px, (row + 1) * kTileSize),
    };
}

// Khoảng tile (bao gồm hai đầu) giao với vùng pixel [x0, x1) x [y0, y1) của trang.
struct TileSpan {
    int col0 = 0;
    int col1 = -1;
    int row0 = 0;
    int row1 = -1;
    [[nodiscard]] bool empty() const noexcept { return col1 < col0 || row1 < row0; }
};

[[nodiscard]] inline TileSpan tiles_in(const TileGrid& g, double x0, double y0, double x1, double y1) noexcept {
    x0 = std::max(x0, 0.0);
    y0 = std::max(y0, 0.0);
    x1 = std::min(x1, static_cast<double>(g.width_px));
    y1 = std::min(y1, static_cast<double>(g.height_px));
    if (x1 <= x0 || y1 <= y0) return {};
    return TileSpan{
        static_cast<int>(x0) / kTileSize,
        static_cast<int>(std::ceil(x1) - 1) / kTileSize,
        static_cast<int>(y0) / kTileSize,
        static_cast<int>(std::ceil(y1) - 1) / kTileSize,
    };
}

}  // namespace pdfread::pdf
