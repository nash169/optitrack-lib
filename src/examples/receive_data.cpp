#include <iostream>
#include <optitrack_lib/Optitrack.hpp>

using namespace optitrack;

int main(int argc, char const* argv[])
{
    Optitrack opt;

    bool status = opt.connect();

    opt.request();

    opt.run();

    return 0;
}
