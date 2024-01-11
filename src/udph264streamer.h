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
    int rtsp_port = 5600;
    GstRTSPServer *rtsp_server = NULL;
    GstRTSPMountPoints *mounts = NULL;
    GstRTSPMediaFactory *factory = NULL;
    GMainLoop *main_loop = NULL;


protected:
    static void _bind_methods();

public:
    UdpH264Streamer();
    ~UdpH264Streamer();

    int get_port();
    void set_port(int port);
    // input_size
    int get_input_width();
    void set_input_width(int width);
    int get_input_height();
    void set_input_height(int height);

    void _process(double delta) override;
    void push_buffer_to_gstreamer(const PackedByteArray &raw_data);
    void setup_rtsp_server();

private:

};

}

#endif
