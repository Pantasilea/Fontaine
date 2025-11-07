# Fontaine

Fontaine is a simple-to-use CLI program that generates bitmap fonts (and it also supports
the generation of Signed Distance Field fonts).

For more information, please read Manual.html

Here is a sample of a generated bitmap font:

<img width="1024" height="1024" alt="showcase" src="https://github.com/user-attachments/assets/fb52c672-ab35-4d2d-9ea1-62b6bf228072" />

## Building

To build the program, you can copy-paste the source folder (which contains only .h, .hpp
and .cpp files) to your desired location and build the program with your preferred build
environment. The only extra thing you must do is to tell your preferred build environment
to include your own copy of the header files of FreeType (2.13.3) and libpng (1.6.50) and
link your own copy of their corresponding library files. This is because I used vcpkg to
obtain FreeType and libpng, and I use Visual Studio so for me including their header files
and building the project just works.
