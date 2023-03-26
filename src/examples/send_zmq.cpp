
#include <Eigen/Core>
#include <iostream>

#include <optitrack_lib/Optitrack.hpp>
#include <zmq_stream/Publisher.hpp>

using namespace zmq_stream;
using namespace optitrack_lib;

int main(int argc, char const* argv[])
{
    Publisher publisher;
    publisher.configure("0.0.0.0", "5511");

    Optitrack optitrack;
    optitrack.connect();

    while (true)
        publisher.publish(optitrack.rigidBodies());

    return 0;
}