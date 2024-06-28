#include <Eigen/Core>
#include <iostream>

#include <zmq_stream/Subscriber.hpp>

using namespace zmq_stream;

int main(int, char**)
{
    Subscriber receiver;
    receiver.configure("localhost", "5511");

    while (true) {
        std::cout << receiver.receive<Eigen::VectorXd>(7).transpose() << std::endl;
    }
}