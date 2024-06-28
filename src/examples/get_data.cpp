#include <iostream>
#include <optitrack_lib/Optitrack.hpp>

using namespace optitrack_lib;

int main(int argc, char const* argv[])
{
    Optitrack opt;

    bool status = opt.connect();

    // Franka_17 Obstacle_stick

    while (true) {
        // std::cout << "Receiving:" << std::endl;
        // std::cout << opt.rigidBodies() << std::endl;
        opt.updateDataDescriptions();
        opt.updateData();
        std::cout<< opt.rigidBody("Obstacle_stick").transpose() << std::endl;
    }

    return 0;
}
