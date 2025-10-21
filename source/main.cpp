#include "application.hpp"

int main(int argc, char* argv[])
{
    int exit_code = 0;
    {
        App app;
        exit_code = app.run(argc, argv);
    }
    return exit_code;
}
