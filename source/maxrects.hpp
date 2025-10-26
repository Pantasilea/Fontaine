#pragma once

#include <vector>
#include <list>

struct Rect {
    char32_t code_point = 0;
    int x = 0;
    int y = 0;
    int w = 0; // width
    int h = 0; // height
    int bin = -1;

    int area() const noexcept { return w * h; }
};

/*
This class implements the Maximal Rectangles (Best Area Fit variation) algorithm
as described in Jukka Jylänki's document: https://github.com/juj/RectangleBinPack/blob/master/RectangleBinPack.pdf
*/
class Bin {
public:
    Bin(const int width, const int height, const bool multiple_bins) noexcept;

    void layout_bulk(std::vector<Rect>& container);
    int processed_rectangles() const noexcept;
    void reset() noexcept;
private:
    bool fits(const Rect& a, const Rect& b) noexcept; // does 'b' fits in 'a'?
    bool overlaps(const Rect& a, const Rect& b) noexcept;
    bool inside(const Rect& a, const Rect& b) noexcept; // is 'b' completely inside 'a'?
    std::list<Rect>::const_iterator find_best_free_rectangle(const Rect& outsider) noexcept;
    void compute_new_free_rectangles(const Rect& free_rect, const Rect& inserted_rect) noexcept;

    std::list<Rect> m_free_rectangles;
    std::list<Rect> m_new_free_rectangles;
    int m_processed_rectangles = 0;
    const int m_width;
    const int m_height;
    const bool m_multiple_bins;
};
