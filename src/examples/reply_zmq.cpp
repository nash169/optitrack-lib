
#include <Eigen/Core>
#include <iostream>

#include <optitrack_lib/Optitrack.hpp>
#include <zmq_stream/Replier.hpp>

using namespace zmq_stream;
using namespace optitrack_lib;

int main(int, char**)
{
    Replier replier;
    replier.configure("0.0.0.0", "5511");

    Optitrack optitrack;
    optitrack.connect();

    auto task = [&](const Eigen::Vector3d& data) {
        // std::cout << optitrack.rigidBody("Obstacle_stick") << std::endl;
        return optitrack.rigidBody("Obstacle_stick");
    };

    while (true){
        optitrack.updateDataDescriptions();
        optitrack.updateData();
        replier.reply<Eigen::VectorXd>(task, 3);
    }

    return 0;
}