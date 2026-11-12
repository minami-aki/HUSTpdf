#include <catch2/catch_test_macros.hpp>

#include "pdf/tiles.hpp"

using namespace pdfread::pdf;

TEST_CASE("tile_grid rounds page size up to whole pixels", "[pdf][tiles]") {
    const TileGrid g = tile_grid(595, 842, kScaleDenominator);  // scale 1
    CHECK(g.width_px == 595);
    CHECK(g.height_px == 842);
    CHECK(g.cols == 2);
    CHECK(g.rows == 2);

    const TileGrid g15 = tile_grid(595, 842, kScaleDenominator * 3 / 2);  // scale 1.5
    CHECK(g15.width_px == 893);  // 892.5 -> 893
    CHECK(g15.height_px == 1263);
    CHECK(g15.cols == 2);
    CHECK(g15.rows == 3);

    const TileGrid exact = tile_grid(512, 1024, kScaleDenominator);
    CHECK(exact.cols == 1);
    CHECK(exact.rows == 2);
}

TEST_CASE("tile_rect clips the last row and column", "[pdf][tiles]") {
    const TileGrid g = tile_grid(595, 842, kScaleDenominator);
    const IRect a = tile_rect(g, 0, 0);
    CHECK(a.x0 == 0);
    CHECK(a.x1 == 512);
    CHECK(a.y1 == 512);
    const IRect b = tile_rect(g, 1, 1);
    CHECK(b.x0 == 512);
    CHECK(b.y0 == 512);
    CHECK(b.x1 == 595);
    CHECK(b.y1 == 842);
    CHECK(b.width() == 83);
}

TEST_CASE("tiles_in selects tiles overlapping a pixel region", "[pdf][tiles]") {
    const TileGrid g = tile_grid(595, 842, kScaleDenominator);

    TileSpan s = tiles_in(g, 0, 0, 10, 10);
    CHECK(s.col0 == 0);
    CHECK(s.col1 == 0);
    CHECK(s.row0 == 0);
    CHECK(s.row1 == 0);

    s = tiles_in(g, 500, 500, 600, 600);
    CHECK(s.col0 == 0);
    CHECK(s.col1 == 1);
    CHECK(s.row0 == 0);
    CHECK(s.row1 == 1);

    s = tiles_in(g, 0, 0, 512, 512);  // biên phải/dưới là nửa mở
    CHECK(s.col1 == 0);
    CHECK(s.row1 == 0);

    s = tiles_in(g, -1000, -1000, 5000, 5000);  // bị kẹp vào trang
    CHECK(s.col0 == 0);
    CHECK(s.col1 == 1);
    CHECK(s.row1 == 1);

    CHECK(tiles_in(g, -100, 0, 0, 100).empty());
    CHECK(tiles_in(g, 0, 900, 100, 1000).empty());
}
