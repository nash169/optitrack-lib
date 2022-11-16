#include "optitrack_lib/OptiTrackZMQBridge.hpp"
#include "optitrack_lib/optitrack_zmq_proto.h"
#include <cstdio>

namespace optitrack {

    OptiTrackZMQBridge::OptiTrackZMQBridge()
    {
        client_ = std::make_unique<OptiTrackClient>();
        zmqContext_ = std::make_unique<zmq::context_t>(1);
    }

    OptiTrackZMQBridge::~OptiTrackZMQBridge()
    {
        if (zmqPublisher_ != nullptr) {
            zmqPublisher_->close();
        }
        zmqContext_.reset();
        zmqPublisher_.reset();
        client_.reset();
    }

    bool OptiTrackZMQBridge::connect(const std::string& serverIP, const std::string& publishURI)
    {
        if (client_->connect(serverIP) != ErrorCode::ErrorCode_OK) {
            printf("[connect] Could not connect to OptiTrack server!\n");
            return false;
        }

        if (!configureZMQ(publishURI)) {
            printf("[connect] Could not configure ZMQ publisher!\n");
            return false;
        }

        client_->setCallback([this](auto data) { publishCallback(data); });

        is_connected = true;
        printf("CONNECTED\n");
        return true;
    }

    void OptiTrackZMQBridge::checkOptiTrackConnection()
    {
        client_->testConnection();
        client_->requestDataDescription();
    }

    bool OptiTrackZMQBridge::configureZMQ(const std::string& uri)
    {
        if (zmqPublisher_ != nullptr && zmqPublisher_->connected()) {
            printf("[configureZMQ] Publisher already connected!\n");
            return true;
        }
        if (zmqContext_->handle() != nullptr) {
            zmqPublisher_ = std::make_unique<zmq::socket_t>(*zmqContext_, ZMQ_PUB);
            zmqPublisher_->connect("tcp://" + uri);
            printf("[configureZMQ] Publisher connected.\n");
            return true;
        }

        return false;
    }

    void OptiTrackZMQBridge::publishCallback(sFrameOfMocapData* data)
    {
        for (int32_t body = 0; body < data->nRigidBodies; ++body) {
            // 0x01 : bool, rigid body was successfully tracked in this frame
            bool trackingValid = data->RigidBodies[body].params & 0x01;
            if (trackingValid) {
                optitrack::proto::RigidBody rb{
                    .id = data->RigidBodies[body].ID,
                    .meanError = data->RigidBodies[body].MeanError,
                    .x = data->RigidBodies[body].x,
                    .y = data->RigidBodies[body].y,
                    .z = data->RigidBodies[body].z,
                    .qw = data->RigidBodies[body].qw,
                    .qx = data->RigidBodies[body].qx,
                    .qy = data->RigidBodies[body].qy,
                    .qz = data->RigidBodies[body].qz};

                publish(rb);
            }
        }
    }

    void OptiTrackZMQBridge::publish(const proto::RigidBody& rb)
    {
        if (zmqPublisher_ == nullptr || !zmqPublisher_->connected()) {
            return;
        }
        zmq::message_t message(sizeof(proto::RigidBody));
        memcpy(message.data(), &rb, sizeof(proto::RigidBody));
        zmqPublisher_->send(message, zmq::send_flags::none);
    }

} // namespace optitrack