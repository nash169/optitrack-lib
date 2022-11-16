#include <iostream>
#include <optitrack_lib/OptiTrackZMQBridge.hpp>

int main(int, char**)
{
    optitrack::OptiTrackZMQBridge bridge;

    if (!bridge.connect("128.178.145.104", "0.0.0.0:5511")) {
        return -1;
    }

    bridge.checkOptiTrackConnection();

    while (true) {
        // let the callback stream any received optitrack data over ZMQ
    }

    return 0;
}