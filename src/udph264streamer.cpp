#include "udph264streamer.h"
#include <godot_cpp/core/class_db.hpp>
#include <gst/gst.h>

using namespace godot;

bool need_frame = true;

UdpH264Streamer::UdpH264Streamer() {
    // Initialize any variables here.
    gst_init(NULL, NULL);
    //gst_debug_set_default_threshold(GST_LEVEL_WARNING);
    udp_port = 5600;
    need_frame = true;
}

UdpH264Streamer::~UdpH264Streamer() {
    // Add your cleanup here.
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

void UdpH264Streamer::_process(double delta) {
}


static void
need_data () {
  need_frame = true;
}

static void push_frame(GstElement* appsrc, const PackedByteArray &raw_data) {
  static GstClockTime timestamp = 0;
  guint size;
  GstBuffer *buffer;
  GstFlowReturn ret;
  buffer = gst_buffer_new_allocate (NULL, raw_data.size(), NULL);
  gst_buffer_fill(buffer, 0, raw_data.ptr(), raw_data.size());
  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 100);
  timestamp += GST_BUFFER_DURATION (buffer);
  g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
  gst_buffer_unref (buffer);
}

void UdpH264Streamer::setup_gstreamer_pipeline() {
    g_printerr ("creating pipeline...\n");
    pipeline = gst_pipeline_new("mypipeline");
    appsrc = gst_element_factory_make("appsrc", "mysrc");
    videoconvert = gst_element_factory_make("videoconvert", "myvideoconvert");
    capsfilter = gst_element_factory_make("capsfilter", "mycapsfilter");
    udpsink = gst_element_factory_make("udpsink", "myudpsink");
    queue1 = gst_element_factory_make("queue", "myqueue1");
    queue2 = gst_element_factory_make("queue", "myqueue2");
    h264parse = gst_element_factory_make("h264parse", "myh264parser");
    rtph264pay = gst_element_factory_make("rtph264pay", "myrtppay");

    GstCaps *appsrccaps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "RGB",  // Adjust as necessary
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION, 60, 1,  // Setting a fixed framerate
        NULL);
    g_object_set(G_OBJECT(appsrc), "caps", appsrccaps,"format", GST_FORMAT_TIME, "is-live", true, NULL);
    gst_caps_unref(appsrccaps);

    // Configure caps for capsfilter if needed
GstCaps *convert_caps = gst_caps_new_simple("video/x-raw",
    "format", G_TYPE_STRING, "I420",
    "width", G_TYPE_INT, 1280,
    "height", G_TYPE_INT, 720,
    "framerate", GST_TYPE_FRACTION, 60, 1,  // Setting a fixed framerate
    NULL);
    g_object_set(G_OBJECT(capsfilter), "caps", convert_caps, NULL);
    gst_caps_unref(convert_caps);
    g_object_set (udpsink, "host", "127.0.0.1", "port", udp_port, NULL);
    // Check for macOS to decide on the encoder
    #ifdef __AAPPLE__
      x264enc = gst_element_factory_make("vtenc_h264_hw", "myencoder");
      g_object_set(x264enc, "realtime", true, NULL);
      g_object_set(x264enc, "allow-frame-reordering", false, NULL);
    #else
      x264enc = gst_element_factory_make("x264enc", "myencoder");
      g_object_set(x264enc, "tune", 4, NULL);
      g_object_set(x264enc, "bitrate", 5000, NULL);
    #endif

    g_signal_connect (appsrc,  "need-data", G_CALLBACK (need_data), NULL);

    if (!pipeline || !appsrc || !videoconvert || !capsfilter || !x264enc || !queue1 || !queue2 || !rtph264pay || !udpsink) {
        g_printerr("Failed to create one or more elements.\n");
        return;
    }

    // Configure your elements as needed, e.g., set properties on appsrc, x264enc, udpsink

    gst_bin_add_many(GST_BIN(pipeline), appsrc, videoconvert, capsfilter, queue1, x264enc, h264parse, queue2, rtph264pay, udpsink, NULL);
    gst_element_link_many(appsrc, videoconvert, capsfilter, queue1, x264enc, queue2, rtph264pay, udpsink, NULL);

    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_printerr ("set to playing\n");
    // GstState state;
    // gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    // if (state == GST_STATE_PLAYING) {
    //   g_printerr ("pipeline is playing\n");
    // }
}

void UdpH264Streamer::push_buffer_to_gstreamer(const PackedByteArray &raw_data) {
    // Check if pipeline and essential elements are created
    if (!pipeline || !appsrc || !videoconvert || !x264enc || !rtph264pay || !udpsink || !queue1 || !queue2) {
        g_printerr("Pipeline or one of its elements is not created, setting up the pipeline...\n");
        setup_gstreamer_pipeline();
        return;
    }

    // // Check the state of the pipeline asynchronously
    // GstState current_state;
    // GstState pending_state;
    // // Set a reasonable timeout for state change, e.g., 100 milliseconds
    // GstClockTime timeout = 100 * GST_MSECOND;
    // GstStateChangeReturn state_return = gst_element_get_state(pipeline, &current_state, &pending_state, timeout);

    // if (state_return == GST_STATE_CHANGE_ASYNC && pending_state == GST_STATE_PLAYING) {
    //     // Pipeline is transitioning to PLAYING, we can proceed
    // } else if (state_return != GST_STATE_CHANGE_SUCCESS || current_state != GST_STATE_PLAYING) {
    //     g_printerr("Pipeline is not in a PLAYING state or failed to change state, attempting to set it to PLAYING...\n");
    //     gst_element_set_state(pipeline, GST_STATE_PLAYING);
    //     // Optionally, handle cases where the pipeline cannot be set to PLAYING
    //     return;
    // }

    // If the pipeline is playing and a new frame is needed, push the frame
    if (need_frame) {
        push_frame(appsrc, raw_data);
        need_frame = false;
    }
}

int UdpH264Streamer::get_port() {
    return udp_port;
}

void UdpH264Streamer::set_port(int port) {
    udp_port = port;
}

void UdpH264Streamer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup_gstreamer_pipeline"), &UdpH264Streamer::setup_gstreamer_pipeline);
    ClassDB::bind_method(D_METHOD("push_buffer_to_gstreamer"), &UdpH264Streamer::push_buffer_to_gstreamer);
    ClassDB::bind_method(D_METHOD("get_port"), &UdpH264Streamer::get_port);
    ClassDB::bind_method(D_METHOD("set_port", "udp_port"), &UdpH264Streamer::set_port);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "udp_port"), "set_port", "get_port");
}
