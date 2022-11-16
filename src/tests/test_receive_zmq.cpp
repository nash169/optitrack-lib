#include <cstdio>

#include <zmq.hpp>

#include "optitrack/optitrack_zmq_proto.h"

bool poll(zmq::socket_t& subscriber, optitrack::proto::RigidBody& rb) {
  zmq::message_t message;
  auto res = subscriber.recv(message, zmq::recv_flags::dontwait);
  if (res) {
    rb = *message.data<optitrack::proto::RigidBody>();
  }
  return res.has_value();
}

int main(int, char**) {
  zmq::context_t context(1);
  zmq::socket_t subscriber(context, ZMQ_SUB);
  subscriber.set(zmq::sockopt::conflate, 1);
  subscriber.set(zmq::sockopt::subscribe, "");
  subscriber.bind("tcp://0.0.0.0:5511");

  optitrack::proto::RigidBody rb{};
  while (subscriber.connected()) {
    if (poll(subscriber, rb)) {
      printf("RigidBody %i: %+5.3f, %+5.3f, %+5.3f\n", rb.id, rb.x, rb.y, rb.z);
    }
  }
}

