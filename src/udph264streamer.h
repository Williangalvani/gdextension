#ifndef Udph264Streamer_H
#define Udph264Streamer_H

#include <godot_cpp/classes/node.hpp>
#include <gst/app/gstappsrc.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <gst/rtsp-server/rtsp-server.h>

namespace godot {

class UdpH264Streamer : public Node {
    GDCLASS(UdpH264Streamer, Node)

private:
    GstElement *appsrc, *pipeline, *videoconvert, *x264enc, *rtph264pay, *udpsink, *queue1, *queue2, *capsfilter, *h264parse;
    int udp_port;
    GstRTSPServer *rtsp_server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    GMainLoop *main_loop;


protected:
    static void _bind_methods();

public:
    UdpH264Streamer();
    ~UdpH264Streamer();

    int get_port();
    void set_port(int port);
    void setup_gstreamer_pipeline();
    void _process(double delta) override;
    void push_buffer_to_gstreamer(const PackedByteArray &raw_data);
    void setup_rtsp_server();

private:

};

}

#endif
