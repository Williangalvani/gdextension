#ifndef Udph264Streamer_H
#define Udph264Streamer_H

#include <godot_cpp/classes/node.hpp>
#include <gst/app/gstappsrc.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/node_path.hpp>

namespace godot {

class UdpH264Streamer : public Node {
    GDCLASS(UdpH264Streamer, Node)

private:
    GstElement *appsrc, *pipeline, *conv, *x264enc, *rtph264pay, *udpsink;
    int udp_port;
    void set_port(const int port) {udp_port = port;};
    int get_port() const {return udp_port;};

protected:
    static void _bind_methods();

public:
    UdpH264Streamer();
    ~UdpH264Streamer();

    void setup_gstreamer_pipeline();
    void _process(double delta) override;
    void push_buffer_to_gstreamer(const PackedByteArray &raw_data);

private:

};

}

#endif
