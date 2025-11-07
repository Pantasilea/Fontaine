#include "application.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include "mystdint.hpp"

#ifdef _WIN32
#include "mywindows.h"
#endif // _WIN32

#include "UTF8CPP/utf8.h"
#include "png.h"

#include "maxrects.hpp"
bool compare_rects(const Rect& lhs, const Rect& rhs) noexcept
{
    return lhs.area() > rhs.area();
}

std::filesystem::path get_exe_dir() noexcept
{
#ifdef __linux__
    std::error_code ec;
    std::filesystem::path p {std::filesystem::canonical("/proc/self/exe", ec)};
    if(ec) return std::filesystem::path {};
    try { p.remove_filename(); }
    catch(const std::exception& e) { return std::filesystem::path {}; }
    return p;
#endif // __linux__

#ifdef _WIN32
    wchar_t path[MAX_PATH];
    DWORD success = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if(not success) return std::filesystem::path {};
    if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) return std::filesystem::path {};
    std::filesystem::path p {path};
    try { p.remove_filename(); }
    catch(const std::exception& e) { return std::filesystem::path {}; }
    return p;
#endif // _WIN32
}

/* List of available cli arguments:
-font
-font-size
-image-size
-char-file
-verify
-output-stem
-load-vert-metrics
-as-given
-multiple-images
-sdf // signed distance fields
*/

struct Cli_args {
    std::string font_file;
    std::string char_file;
    std::string output_stem;
    int font_size = 32;
    int image_size = 256; // enough for standard ASCII
    bool load_vert_metrics = false;
    bool as_given = false;
    bool multiple_images = false;
    bool sdf = false;
    bool verify = false;
};

struct Char_info {
    char32_t code_point = 0;
    int glyph_width = 0;
    int glyph_height = 0;
    int left_bearing = 0;
    int top_bearing = 0;
    int advance_x = 0;
    int advance_y = 0;
};

bool valid_arg_index(const int index, const int max_index) noexcept
{
    return not (index > max_index);
}

std::filesystem::path create_output_filename(const std::string& output_stem, const int bin_instance, const bool image_type) noexcept
{
    std::filesystem::path p {get_exe_dir()};
    if(p.empty()) return p;
    std::string s {"output/"};
    if(image_type) { s.append(output_stem).append(1, '-').append(std::to_string(bin_instance)).append(".png"); }
    else { s.append(output_stem).append(".txt"); }
    p.append(s);
    return p;
}

void place_char_info(std::ofstream& info_file, const Rect& rect_info, const Char_info& char_info)
{
    std::string info {std::to_string(static_cast<uint32>(rect_info.code_point))};
    info.append(1, ':').append(std::to_string(rect_info.bin));
    info.append(1, ':').append(std::to_string(rect_info.x));
    info.append(1, ':').append(std::to_string(rect_info.y));
    info.append(1, ':').append(std::to_string(rect_info.w));
    info.append(1, ':').append(std::to_string(rect_info.h));
    info.append(1, ':').append(std::to_string(char_info.left_bearing));
    info.append(1, ':').append(std::to_string(char_info.top_bearing));
    info.append(1, ':').append(std::to_string(char_info.advance_x));
    info.append(1, ':').append(std::to_string(char_info.advance_y));
    info.append(1, '\n');
    info_file << info;
}

void place_pixel_data(std::vector<uint8>& atlas, const int atlas_width, const Rect& where, const uint8* glyph_image, const int glyph_pitch)
{
    uint8* atlas_ptr = atlas.data();
    atlas_ptr += atlas_width * where.y + where.x;
    for(int row = 0; row < where.h; ++row) { // for each glyph's pixel row
        std::memcpy(atlas_ptr, glyph_image, glyph_pitch);
        atlas_ptr += atlas_width;
        glyph_image += glyph_pitch;
    }
}

bool create_png_image(const std::string& output_stem, const int current_bin_instance, const int image_size, const uint8* pixel_data)
{
    png_image png_descriptor;
    std::memset(&png_descriptor, 0, sizeof(png_image));
    png_descriptor.version = PNG_IMAGE_VERSION;
    png_descriptor.width = image_size;
    png_descriptor.height = image_size;
    png_descriptor.format = PNG_FORMAT_GRAY;

    png_alloc_size_t buffer_size;
    if(not png_image_write_get_memory_size(png_descriptor, buffer_size, 0, pixel_data, 0, NULL)) {
        std::cout << "Internal error: png_image_write_get_memory_size failed.\n";
        png_image_free(&png_descriptor);
        return false;
    }
    std::vector<uint8> the_png_image; the_png_image.resize(buffer_size);
    if(not png_image_write_to_memory(&png_descriptor, the_png_image.data(), &buffer_size, 0, pixel_data, 0, NULL)) {
        std::cout << "Internal error: png_image_write_to_memory failed.\n";
        png_image_free(&png_descriptor);
        return false;
    }
    std::ofstream png_image_file {create_output_filename(output_stem, current_bin_instance, true), std::ios_base::binary};
    if(not png_image_file) {
        std::cout << "Internal error: Couldn't open a file stream to write the png image to.\n";
        png_image_free(&png_descriptor);
        return false;
    }
    png_image_file.write(reinterpret_cast<char*>(the_png_image.data()), the_png_image.size());
    if(png_image_file.fail() or png_image_file.bad()) {
        std::cout << "Internal error: Writing a png image to a file failed.\n";
        png_image_free(&png_descriptor);
        return false;
    }
    png_image_free(&png_descriptor);
    return true;
}

App::~App()
{
    if(m_font_face) FT_Done_Face(m_font_face);
    if(m_freetype_library) FT_Done_FreeType(m_freetype_library);
}

int App::run(int argc, char** argv)
{
    if(argc < 5) {
        std::cout << "Error: Not enough arguments given.\nFor help with using this program, read Manual.html\n";
        std::cout << "Number of arguments: " << argc << '\n';
        return EXIT_FAILURE;
    }

    const std::filesystem::path exe_dir {get_exe_dir()};
    if(exe_dir.empty()) {
        std::cout << "Internal error: Couldn't retrieve the program's executable path.\n";
        return EXIT_FAILURE;
    }

    /* parse the given command line arguments */

    Cli_args cli_args;
    const int last_arg_index = argc - 1;
    // code folding is a blessing
    for(int i = 0; i < argc; ++i) {
        const char* str = argv[i];
        if(str[0] != '-') continue;

        const int j = i + 1;
        if(std::strcmp(argv[i], "-font") == 0) {
            if(valid_arg_index(j, last_arg_index)) {
                cli_args.font_file = argv[j];
            }
        }
        else if(std::strcmp(argv[i], "-font-size") == 0) {
            if(valid_arg_index(j, last_arg_index)) {
                cli_args.font_size = std::atoi(argv[j]);
            }
        }
        else if(std::strcmp(argv[i], "-image-size") == 0) {
            if(valid_arg_index(j, last_arg_index)) {
                cli_args.image_size = std::atoi(argv[j]);
            }
        }
        else if(std::strcmp(argv[i], "-char-file") == 0) {
            if(valid_arg_index(j, last_arg_index)) {
                cli_args.char_file = argv[j];
            }
        }
        else if(std::strcmp(argv[i], "-verify") == 0) {
            cli_args.verify = true;
        }
        else if(std::strcmp(argv[i], "-output-stem") == 0) {
            if(valid_arg_index(j, last_arg_index)) {
                cli_args.output_stem = argv[j];
            }
        }
        else if(std::strcmp(argv[i], "-load-vert-metrics") == 0) {
            cli_args.load_vert_metrics = true;
        }
        else if(std::strcmp(argv[i], "-as-given") == 0) {
            cli_args.as_given = true;
        }
        else if(std::strcmp(argv[i], "-multiple-images") == 0) {
            cli_args.multiple_images = true;
        }
        else if(std::strcmp(argv[i], "-sdf") == 0) {
            cli_args.sdf = true;
        }
        else {
            std::cout << "Error: Invalid argument given (" << argv[i] << ").\n";
            return EXIT_FAILURE;
        }
    }

    /* validate the given command line arguments */

    if(cli_args.font_file.empty()) {
        std::cout << "Error: -font wasn't given a value.\n";
        return EXIT_FAILURE;
    }
    if(cli_args.output_stem.empty() and not cli_args.verify) {
        std::cout << "Error: -output-stem wasn't given a value.\n";
        return EXIT_FAILURE;
    }
    if(cli_args.font_size == 0 and not cli_args.verify) {
        std::cout << "Error: -font-size was given an invalid value.\n";
        return EXIT_FAILURE;
    }
    if(cli_args.image_size == 0 and not cli_args.verify) {
        std::cout << "Error: -image-size was given an invalid value.\n";
        return EXIT_FAILURE;
    }
    if(cli_args.verify and cli_args.char_file.empty()) {
        std::cout << "Error: -verify was specified but -char-file wasn't given a value.\n";
        return EXIT_FAILURE;
    }
    if(cli_args.as_given and cli_args.char_file.empty()) {
        std::cout << "Error: -as-given was specified but -char-file was not provided.\n";
        return EXIT_FAILURE;
    }

    /* validation for -load-vert-metrics is pending, FreeType needs to be initialised first */

    FT_Error error = FT_Init_FreeType(&m_freetype_library);
    if(error) {
        std::cout << "Internal error: FreeType initialisation failed.\n";
        return EXIT_FAILURE;
    }
    // load the font file into memory
    std::filesystem::path font_file_path {exe_dir};
    font_file_path.append(cli_args.font_file);

    std::ifstream ifs {font_file_path, std::ios_base::binary | std::ios_base::ate};
    if(not ifs) {
        std::cout << "Error: Failed to open the font file.\n";
        return EXIT_FAILURE;
    }
    const std::streamoff font_file_size = ifs.tellg();
    std::vector<uint8> in_memory_font_file;
    in_memory_font_file.resize(font_file_size);
    ifs.seekg(0, std::ios_base::beg);
    ifs.read(reinterpret_cast<char*>(in_memory_font_file.data()), font_file_size);
    if(ifs.fail() and not ifs.eof()) {
        std::cout << "Error: Failed to read the font file.\n";
        return EXIT_FAILURE;
    }
    ifs.close();
    error = FT_New_Memory_Face(m_freetype_library, in_memory_font_file.data(), font_file_size, 0, &m_font_face);
    if(error) {
        std::cout << "Internal error: FT_New_Memory_Face failed.\n";
        return EXIT_FAILURE;
    }
    error = FT_Select_Charmap(m_font_face, FT_ENCODING_UNICODE);
    if(error) {
        std::cout << "Error: The font file doesn't contain a Unicode character map.\n";
        return EXIT_FAILURE;
    }
    error = FT_Set_Pixel_Sizes(m_font_face, 0, cli_args.font_size);
    if(error) {
        std::cout << "Internal error: FT_Set_Pixel_Sizes failed.\n";
        return EXIT_FAILURE;
    }
    // validate -load-vert-metrics
    if(cli_args.load_vert_metrics and not FT_HAS_VERTICAL(m_font_face)) {
        std::cout << "Error: The font file doesn't contain vertical metrics.\n";
        return EXIT_FAILURE;
    }

    /* at this point, all command line arguments are validated, so let's work,
    * but first we must handle -verify
    */
    if(cli_args.verify) {
        std::filesystem::path char_file_path {exe_dir};
        char_file_path.append(cli_args.char_file);
        std::ifstream char_file {char_file_path};
        if(not char_file) {
            std::cout << "Error: Couldn't open the characters file.\n";
            return EXIT_FAILURE;
        }
        std::filesystem::path missing_characters_file_path {exe_dir};
        missing_characters_file_path.append(std::u8string {u8"output/missing-chars.txt"});
        std::ofstream missing_characters_file {missing_characters_file_path, std::ios_base::binary};
        if(not missing_characters_file) {
            std::cout << "Internal error: The missing characters file couldn't be created.\n";
            return EXIT_FAILURE;
        }
        std::string line;
        int32 line_number = 1; // just for a better error message
        while(std::getline(char_file, line)) {
            if(line.empty()) continue;

            if(not utf8::is_valid(line)) {
                std::cout << "Error: Invalid UTF-8 found in the characters file at line #" << line_number << ".\n";
                return EXIT_FAILURE;
            }

            std::u32string code_points {utf8::utf8to32(line)};
            for(const char32_t code_point : code_points) {
                if(FT_Get_Char_Index(m_font_face, code_point) == 0) {
                    std::u32string u32str;
                    u32str.append(1, code_point);
                    std::u8string u8str {utf8::utf32tou8(u32str)};
                    missing_characters_file.write(reinterpret_cast<const char*>(u8str.data()), u8str.size());
                    missing_characters_file.put(static_cast<char>(u8'\n'));
                    if(not missing_characters_file.good()) {
                        std::cout << "Internal error: Writing to the missing characters file failed.\n";
                        return EXIT_FAILURE;
                    }
                }
            }

            ++line_number;
        }
        if(not char_file.eof()) {
            std::cout << "Internal error: An error ocurred while reading the characters file.\n";
            return EXIT_FAILURE;
        }

        std::cout << "Finished the verification. Please check output/missing-chars.txt\n";
        return EXIT_SUCCESS;
    }

    /* extract the desired characters' metrics */

    std::map<char32_t, Char_info> characters;
    std::vector<Rect> glyph_rects; glyph_rects.reserve(256);
    const FT_Int32 load_flag = cli_args.load_vert_metrics ? FT_LOAD_VERTICAL_LAYOUT : FT_LOAD_DEFAULT;
    FT_Render_Mode render_mode = cli_args.sdf ? FT_RENDER_MODE_SDF : FT_RENDER_MODE_NORMAL;
    if(cli_args.char_file.empty()) {
        FT_ULong charcode = 0;
        FT_UInt glyph_index = 0;

        charcode = FT_Get_First_Char(m_font_face, &glyph_index);
        while(glyph_index != 0) {
            error = FT_Load_Glyph(m_font_face, glyph_index, load_flag);
            if(error) {
                std::cout << "Internal error: Couldn't load the glyph with character code " << charcode << ".\n";
                return EXIT_FAILURE;
            }

            error = FT_Render_Glyph(m_font_face->glyph, render_mode);
            if(error) {
                std::cout << "Internal error: Couldn't render the glyph with character code " << charcode << ".\n";
                return EXIT_FAILURE;
            }

            Char_info ci;
            ci.code_point = charcode;
            ci.glyph_width = m_font_face->glyph->bitmap.width;
            ci.glyph_height = m_font_face->glyph->bitmap.rows;
            ci.left_bearing = m_font_face->glyph->bitmap_left;
            ci.top_bearing = m_font_face->glyph->bitmap_top;
            ci.advance_x = m_font_face->glyph->advance.x >> 6;
            ci.advance_y = m_font_face->glyph->advance.y >> 6;

            characters.emplace(charcode, ci);

            Rect r;
            r.code_point = charcode;
            r.w = ci.glyph_width;
            r.h = ci.glyph_height;

            glyph_rects.push_back(r);

            charcode = FT_Get_Next_Char(m_font_face, charcode, &glyph_index);
        }
    }
    else {
        std::filesystem::path char_file_path {exe_dir};
        char_file_path.append(cli_args.char_file);
        std::ifstream char_file {char_file_path};
        if(not char_file) {
            std::cout << "Error: Couldn't open the characters file.\n";
            return EXIT_FAILURE;
        }

        std::string line;
        int32 line_number = 1; // just for a better error message
        while(std::getline(char_file, line)) {
            if(line.empty()) continue;

            if(not utf8::is_valid(line)) {
                std::cout << "Error: Invalid UTF-8 found in the characters file at line #" << line_number << ".\n";
                return EXIT_FAILURE;
            }

            int32 char_number = 1; // just for a better error message
            std::u32string code_points = utf8::utf8to32(line);
            for(const char32_t code_point : code_points) {
                if(characters.contains(code_point)) continue;

                FT_UInt glyph_index = FT_Get_Char_Index(m_font_face, code_point);
                if(glyph_index == 0u) {
                    std::cout << "Error: The font file does not contain the character #" << char_number << " in the line #" << line_number << ".\n";
                    return EXIT_FAILURE;
                }

                error = FT_Load_Glyph(m_font_face, glyph_index, load_flag);
                if(error) {
                    std::cout << "Internal error: Failed to load the character #" << char_number << " in the line #" << line_number << ".\n";
                    return EXIT_FAILURE;
                }

                error = FT_Render_Glyph(m_font_face->glyph, render_mode);
                if(error) {
                    std::cout << "Internal error: Failed to render the character #" << char_number << " in the line #" << line_number << ".\n";
                    return EXIT_FAILURE;
                }

                ++char_number;

                Char_info ci;
                ci.code_point = code_point;
                ci.glyph_width = m_font_face->glyph->bitmap.width;
                ci.glyph_height = m_font_face->glyph->bitmap.rows;
                ci.left_bearing = m_font_face->glyph->bitmap_left;
                ci.top_bearing = m_font_face->glyph->bitmap_top;
                ci.advance_x = m_font_face->glyph->advance.x >> 6;
                ci.advance_y = m_font_face->glyph->advance.y >> 6;

                characters.emplace(code_point, ci);

                Rect r;
                r.code_point = code_point;
                r.w = ci.glyph_width;
                r.h = ci.glyph_height;

                glyph_rects.push_back(r);
            }

            ++line_number;
        }
        if(not char_file.eof()) {
            std::cout << "Internal error: An error ocurred while reading the characters file.\n";
            return EXIT_FAILURE;
        }
    }

    /* find the optimal places for the glyphs to be put within the image */

    Bin bin {cli_args.image_size, cli_args.image_size, cli_args.multiple_images};
    if(not cli_args.as_given) std::sort(glyph_rects.begin(), glyph_rects.end(), compare_rects);
    try { bin.layout_bulk(glyph_rects); }
    catch(const std::runtime_error& e) {
        std::cout << e.what() << '\n';
        return EXIT_FAILURE;
    }
    if(bin.processed_rectangles() == 0) {
        std::cout << "Error: -font-size is too large for -image-size\n";
        return EXIT_FAILURE;
    }

    /* pack the glyphs' textures and information */

    int current_bin_instance = 0;
    const int processed_rectangles = bin.processed_rectangles();
    std::ofstream info_file {create_output_filename(cli_args.output_stem, current_bin_instance, false), std::ios_base::binary};
    if(not info_file) {
        std::cout << "Internal error: Couldn't create the information output file.\n";
        return EXIT_FAILURE;
    }
    info_file << "atlas-dimensions:" << std::to_string(cli_args.image_size) << '\n';
    info_file << "linespace:" << std::to_string(m_font_face->size->metrics.height >> 6) << '\n';
    std::vector<uint8> atlas; atlas.resize(cli_args.image_size * cli_args.image_size);
    for(int i = 0; i < processed_rectangles; ++i) {
        const Rect& r = glyph_rects[i];
        if(r.bin != current_bin_instance) {
            if(not create_png_image(cli_args.output_stem, current_bin_instance, cli_args.image_size, atlas.data())) {
                return EXIT_FAILURE;
            }
            std::memset(atlas.data(), 0, atlas.size());
            ++current_bin_instance;
        }
        error = FT_Load_Char(m_font_face, r.code_point, FT_LOAD_DEFAULT);
        if(error) {
            std::cout << "Internal error: Failed to load the character with code point " << static_cast<uint32>(r.code_point) << ".\n";
            return EXIT_FAILURE;
        }
        error = FT_Render_Glyph(m_font_face->glyph, render_mode);
        if(error) {
            std::cout << "Internal error: Couldn't render the glyph with character code " << static_cast<uint32>(r.code_point) << ".\n";
            return EXIT_FAILURE;
        }
        place_pixel_data(atlas, cli_args.image_size, r, m_font_face->glyph->bitmap.buffer, r.w);
        place_char_info(info_file, r, characters[r.code_point]);
    }
    if(not create_png_image(cli_args.output_stem, current_bin_instance, cli_args.image_size, atlas.data())) {
        return EXIT_FAILURE;
    }

    std::cout << "Finished generating files.\n";
    return EXIT_SUCCESS;
}
