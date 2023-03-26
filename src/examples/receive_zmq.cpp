#include <Eigen/Core>
#include <iostream>

#include <zmq_stream/Subscriber.hpp>

using namespace zmq_stream;

int main(int, char**)
{
    Subscriber receiver;
    receiver.configure("localhost", "5511");

    while (true) {
        std::cout << "Receiving:" << std::endl;
        std::cout << receiver.receive<Eigen::MatrixXd>(2, 7) << std::endl;
    }
}