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
    GstElement *nvh264enc_capsfilter = NULL, *appsrc = NULL, *pipeline = NULL, *videoconvert = NULL, *x264enc = NULL, *rtph264pay = NULL, *udpsink = NULL, *queue1 = NULL, *queue2 = NULL, *capsfilter = NULL, *h264parse = NULL;
    int udp_port = 5600;
    GstRTSPServer *rtsp_server = NULL;
    GstRTSPMountPoints *mounts = NULL;
    GstRTSPMediaFactory *factory = NULL;
    GMainLoop *main_loop = NULL;
    int input_width = 1152;
    int input_height = 648;

protected:
    static void _bind_methods();

public:
    UdpH264Streamer();
    ~UdpH264Streamer();

    int get_port();
    void set_port(int port);
    // input_size
    int get_input_width() { return input_width; };
    void set_input_width(int width) { input_width = width; };
    int get_input_height() { return input_height; };
    void set_input_height(int height) { input_height = height; };

    void setup_gstreamer_pipeline();
    void _process(double delta) override;
    void push_buffer_to_gstreamer(const PackedByteArray &raw_data);
    void setup_rtsp_server();

private:

};

}

#endif
