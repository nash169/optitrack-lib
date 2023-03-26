#include <iostream>
#include <optitrack_lib/Optitrack.hpp>

using namespace optitrack_lib;

int main(int argc, char const* argv[])
{
    Optitrack opt;

    bool status = opt.connect();

    while (true) {
        std::cout << "Receiving:" << std::endl;
        std::cout << opt.rigidBodies() << std::endl;
    }

    return 0;
}
