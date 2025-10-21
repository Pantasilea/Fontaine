#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

class App {
public:
    App() noexcept {};
    ~App();

    int run(int argc, char** argv);
private:
    FT_Library m_freetype_library = nullptr;
    FT_Face m_font_face = nullptr;
};