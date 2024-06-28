#include <Eigen/Core>
#include <iostream>

#include <zmq_stream/Requester.hpp>

using namespace zmq_stream;

int main(int, char**)
{
    Requester requester;
    requester.configure("localhost", "5511");

    Eigen::Matrix<double, 3, 1> vec(1., 2., 3.);

    while (true) {
        std::cout << requester.request<Eigen::VectorXd>(vec, 7).transpose() << std::endl;
    }
}