#include "maxrects.hpp"

#include <limits>
#include <algorithm>
#include <stdexcept>
#include <string>
#include "mystdint.hpp"

Bin::Bin(const int width, const int height, const bool multiple_bins) noexcept
    : m_width {width}, m_height {height}, m_multiple_bins {multiple_bins}
{
    // initially, the entire bin is free
    Rect rect;
    rect.w = width;
    rect.h = height;

    m_free_rectangles.push_back(rect);
}

void Bin::layout_bulk(std::vector<Rect>& container)
{
    int bin_instance = 0;
    for(Rect& r : container) {
        /* search the best free rectangle */
        auto it = find_best_free_rectangle(r);
        if(it == m_free_rectangles.cend()) { // no more rectangles fit in the bin
            if(not m_multiple_bins) return;
            reset();
            ++bin_instance;
            it = find_best_free_rectangle(r);
            if(it == m_free_rectangles.cend()) {
                std::string error_msg {"Error: The glyph "};
                error_msg.append(std::to_string(static_cast<uint32>(r.code_point)));
                error_msg.append(" (UTF-32 code point) didn't fit in an empty bin. The -font-size is too large for the -image-size.");
                throw std::runtime_error {error_msg};
            }
        }
        r.x = it->x;
        r.y = it->y;
        r.bin = bin_instance;

        /* compute new free rectangles */
        for(auto iter = m_free_rectangles.cbegin(); iter != m_free_rectangles.cend();) {
            if(overlaps(*iter, r)) {
                compute_new_free_rectangles(*iter, r);
                iter = m_free_rectangles.erase(iter);
            }
            else { ++iter; }
        }
        /* validate the new free rectangles against themselves */
        for(auto iter1 = m_new_free_rectangles.cbegin(); iter1 != m_new_free_rectangles.cend(); ++iter1) {
            for(auto iter2 = m_new_free_rectangles.cbegin(); iter2 != m_new_free_rectangles.cend();) {
                if(iter1 == iter2) {
                    ++iter2;
                    continue;
                }

                if(inside(*iter1, *iter2)) {
                    iter2 = m_new_free_rectangles.erase(iter2);
                    continue;
                }
                ++iter2;
            }
        }
        /* validate the new free rectangles against the old free rectangles */
        for(auto iter1 = m_free_rectangles.cbegin(); iter1 != m_free_rectangles.cend(); ++iter1) {
            for(auto iter2 = m_new_free_rectangles.cbegin(); iter2 != m_new_free_rectangles.cend();) {
                if(inside(*iter1, *iter2)) {
                    iter2 = m_new_free_rectangles.erase(iter2);
                    continue;
                }
                ++iter2;
            }
        }
        /* the merging can finally be done */
        m_free_rectangles.insert(m_free_rectangles.end(), m_new_free_rectangles.cbegin(), m_new_free_rectangles.cend());
        m_new_free_rectangles.clear();
        ++m_processed_rectangles;
    }
}

int Bin::processed_rectangles() const noexcept
{
    return m_processed_rectangles;
}

void Bin::reset() noexcept
{
    m_free_rectangles.clear();
    Rect r;
    r.w = m_width;
    r.h = m_height;
    m_free_rectangles.push_back(r);
}

bool Bin::fits(const Rect& a, const Rect& b) noexcept
{
    return b.w <= a.w and b.h <= a.h;
}

bool Bin::overlaps(const Rect& a, const Rect& b) noexcept
{
    const bool x_overlap = b.x <= a.x + (a.w - 1) and b.x + (b.w - 1) >= a.x;
    const bool y_overlap = b.y <= a.y + (a.h - 1) and b.y + (b.h - 1) >= a.y;
    return x_overlap and y_overlap;
}

bool Bin::inside(const Rect& a, const Rect& b) noexcept
{
    return b.x >= a.x and b.x + b.w <= a.x + a.w and b.y >= a.y and b.y + b.h <= a.y + a.h;
}

std::list<Rect>::const_iterator Bin::find_best_free_rectangle(const Rect& outsider) noexcept
{
    // Best Area Fit score (lower is better)
    int baf_score = std::numeric_limits<int>::max();
    // Best Short Side Fit score (lower is better)
    int bssf_score = std::numeric_limits<int>::max();

    auto iter_result = m_free_rectangles.cend();
    auto iter_end = m_free_rectangles.cend();
    for(auto iter = m_free_rectangles.cbegin(); iter != iter_end; ++iter) {
        const Rect& free_rect = *iter;
        if(fits(free_rect, outsider)) {
            int unused_area = free_rect.area() - outsider.area();

            int unused_width = free_rect.w - outsider.w;
            int unused_height = free_rect.h - outsider.h;
            int most_used_dimension = std::min(unused_width, unused_height);

            if(unused_area < baf_score) {
                baf_score = unused_area;
                iter_result = iter;
            }
            else if(unused_area == baf_score and most_used_dimension < bssf_score) {
                bssf_score = most_used_dimension;
                iter_result = iter;
            }
        }
    }
    return iter_result;
}

void Bin::compute_new_free_rectangles(const Rect& free_rect, const Rect& inserted_rect) noexcept
{
    // compute potential new free rectangles located above and below
    if(inserted_rect.x < free_rect.x + free_rect.w and inserted_rect.x + inserted_rect.w > free_rect.x) {
        // overlap is on the lower side, create new potential free rectangle above
        if(inserted_rect.y + inserted_rect.h < free_rect.y + free_rect.h) {
            Rect new_free_rectangle = free_rect;
            new_free_rectangle.y = inserted_rect.y + inserted_rect.h;
            new_free_rectangle.h = free_rect.y + free_rect.h - (inserted_rect.y + inserted_rect.h);

            // do not allow degenerate rectangles
            if(new_free_rectangle.w > 0 and new_free_rectangle.h > 0) {
                m_new_free_rectangles.push_back(new_free_rectangle);
            }
        }

        // overlap is on the upper side, create new potential free rectangle below
        if(inserted_rect.y > free_rect.y and inserted_rect.y < free_rect.y + free_rect.h) {
            Rect new_free_rectangle = free_rect;
            new_free_rectangle.h = inserted_rect.y - free_rect.y;

            // do not allow degenerate rectangles
            if(new_free_rectangle.w > 0 and new_free_rectangle.h > 0) {
                m_new_free_rectangles.push_back(new_free_rectangle);
            }
        }
    }

    // compute potential new free rectangles located left and right
    if(inserted_rect.y < free_rect.y + free_rect.h and inserted_rect.y + inserted_rect.h > free_rect.y) {
        // overlap is on the right side, create new potential free rectangle at the left
        if(inserted_rect.x > free_rect.x and inserted_rect.x < free_rect.x + free_rect.w) {
            Rect new_free_rectangle = free_rect;
            new_free_rectangle.w = inserted_rect.x - free_rect.x;

            // do not allow degenerate rectangles
            if(new_free_rectangle.w > 0 and new_free_rectangle.h > 0) {
                m_new_free_rectangles.push_back(new_free_rectangle);
            }
        }

        // overlap is on the left side, create new potential free rectangle at the right
        if(inserted_rect.x + inserted_rect.w < free_rect.x + free_rect.w) {
            Rect new_free_rectangle = free_rect;
            new_free_rectangle.x = inserted_rect.x + inserted_rect.w;
            new_free_rectangle.w = free_rect.x + free_rect.w - (inserted_rect.x + inserted_rect.w);

            // do not allow degenerate rectangles
            if(new_free_rectangle.w > 0 and new_free_rectangle.h > 0) {
                m_new_free_rectangles.push_back(new_free_rectangle);
            }
        }
    }
}
