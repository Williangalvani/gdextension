#include "udph264streamer.h"
#include <godot_cpp/core/class_db.hpp>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

using namespace godot;

bool need_frame = true;

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;


std::string find_working_hw_encoder() {
    std::vector<std::string> encoders = {"nvh264enc", "msdkh264enc","vtenc_h264_hw", "x264enc"};

    for (const auto& encoder_name : encoders) {
        std::string pipeline_str = "videotestsrc ! " + encoder_name + " ! fakesink";
        GError *error = NULL;
        GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

        if (pipeline == NULL || error != NULL) {
            g_printerr("Failed to create pipeline: %s\n", error->message);
            g_error_free(error);
            continue;
        }

        GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            gst_object_unref(pipeline);
            continue;
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return encoder_name;
    }
    return "";
}
void UdpH264Streamer::setup_rtsp_server() {
    rtsp_server = gst_rtsp_server_new();
    g_object_set (rtsp_server, "service", port, NULL);
    mounts = gst_rtsp_server_get_mount_points(rtsp_server);
    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    // Launch string for the RTSP server pipeline
    std::string launch_string =
        "udpsrc port=" + std::to_string(udp_port) + " caps=\"application/x-rtp\" ! "
        "rtph264depay ! h264parse ! video/x-h264, width=(int)" + std::to_string(this->input_width) +
        ", height=(int)" + std::to_string(this->input_height) + ", framerate=(fraction)60/1 ! "
        "rtph264pay config-interval=10 name=pay0 pt=96";


    gst_rtsp_media_factory_set_launch(factory, launch_string.c_str());
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);
    g_object_unref(mounts);

    // Start the RTSP server
    gst_rtsp_server_attach(rtsp_server, NULL);
}

UdpH264Streamer::UdpH264Streamer() {
    gst_init(NULL, NULL);
    main_loop = g_main_loop_new(NULL, FALSE);  // Create a new GMainLoop
    need_frame = true;
}

UdpH264Streamer::~UdpH264Streamer() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
    if (rtsp_server) {
        g_object_unref(rtsp_server);
        rtsp_server = NULL;
    }
    if (factory) {
        g_object_unref(factory);
        factory = NULL;
    }
    if (main_loop) {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }
}

void UdpH264Streamer::_process(double delta) {
      if (main_loop) {
        // Iterate the GStreamer main loop
        g_info("Running main loop");
        while (g_main_context_pending(g_main_loop_get_context(main_loop))) {
          g_main_context_iteration(g_main_loop_get_context(main_loop), FALSE);
        }
    }
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
        "width", G_TYPE_INT, input_width,
        "height", G_TYPE_INT, input_height,
        "framerate", GST_TYPE_FRACTION, 60, 1,  // Setting a fixed framerate
        NULL);
    g_object_set(G_OBJECT(appsrc), "caps", appsrccaps,"format", GST_FORMAT_TIME, "is-live", true, NULL);
    gst_caps_unref(appsrccaps);

    // Create the caps for the h264parse element
    GstCaps *caps = gst_caps_from_string("video/x-h264,stream-format=avc,alignment=au,profile=baseline");
    if (!caps) {
        g_printerr("Failed to create caps.\n");
        return;
    }

    // Get the source pad of the h264parse element
    GstPad *pad = gst_element_get_static_pad(h264parse, "src");
    if (!pad) {
        g_printerr("Failed to get source pad of h264parse.\n");
        gst_caps_unref(caps);
        return;
    }

    // Set the caps on the pad
    if (!gst_pad_set_caps(pad, caps)) {
        g_printerr("Failed to set caps on h264parse.\n");
    }

    // Unref the pad and the caps
    gst_object_unref(pad);
    gst_caps_unref(caps);


    // Configure caps for capsfilter if needed
GstCaps *convert_caps = gst_caps_new_simple("video/x-raw",
    "format", G_TYPE_STRING, "I420",
    "width", G_TYPE_INT, input_width,
    "height", G_TYPE_INT, input_height,
    "framerate", GST_TYPE_FRACTION, 60, 1,  // Setting a fixed framerate
    NULL);
    g_object_set(G_OBJECT(capsfilter), "caps", convert_caps, NULL);
    gst_caps_unref(convert_caps);
    g_object_set (udpsink, "host", "127.0.0.1", "port", udp_port, NULL);
    // Check for macOS to decide on the encoder

    // TODO: use this to pick the encoder
    auto encoder = find_working_hw_encoder();
    GST_ERROR("Encoder: %s\n", encoder.c_str());

    if (encoder == "vtenc_h264_hw") {
      x264enc = gst_element_factory_make("x264enc", "myencoder");
      g_object_set(x264enc, "pass", 4, NULL);  // Set pass to 'quantizer'
      g_object_set(x264enc, "quantizer", 20, NULL);  // Set quantizer to a reasonable value
      g_object_set(x264enc, "tune", 4, NULL);  // Set tune to 'zerolatency'
      g_object_set(x264enc, "bitrate", 5000, NULL);
      g_object_set(x264enc, "key-int-max", 120, NULL);  // Set keyframe interval to 2 seconds
    } else if (encoder == "x264enc") {
      x264enc = gst_element_factory_make("x264enc", "myencoder");
      g_object_set(x264enc, "pass", 4, NULL);  // Set pass to 'quantizer'
      g_object_set(x264enc, "quantizer", 20, NULL);  // Set quantizer to a reasonable value
      g_object_set(x264enc, "tune", 4, NULL);  // Set tune to 'zerolatency'
      g_object_set(x264enc, "bitrate", 5000, NULL);
      g_object_set(x264enc, "key-int-max", 120, NULL);  // Set keyframe interval to 2 seconds
    } else if (encoder == "nvh264enc") {
      x264enc = gst_element_factory_make("nvh264enc", "myencoder");
      GstElement *cudaupload = gst_element_factory_make("cudaupload", NULL);
      GstElement *cudaconvert = gst_element_factory_make("cudaconvert", NULL);

      GstCaps *cudaconvert_caps = gst_caps_from_string("video/x-raw(memory:CUDAMemory),format=I420");
      cudaconvert_capsfilter = gst_element_factory_make("capsfilter", NULL);
      g_object_set(cudaconvert_capsfilter, "caps", cudaconvert_caps, NULL);
      gst_caps_unref(cudaconvert_caps);

      g_object_set(x264enc, "bitrate", 5000, NULL);
      g_object_set(x264enc, "rc-mode", "cbr", NULL);
      g_object_set(x264enc, "gop-size", -1, NULL);
      g_object_set(x264enc, "qos", TRUE, NULL);
      g_object_set(x264enc, "preset", "low-latency-hq", NULL);

      GstCaps *nvh264enc_caps = gst_caps_from_string("video/x-h264,profile=high");
      nvh264enc_capsfilter = gst_element_factory_make("capsfilter", NULL);
      g_object_set(nvh264enc_capsfilter, "caps", nvh264enc_caps, NULL);
      gst_caps_unref(nvh264enc_caps);

    } else if (encoder == "vaapih264enc") {
      x264enc = gst_element_factory_make("vaapih264enc", "myencoder");

    } else {
      g_printerr("Invalid encoder: %s\n", encoder.c_str());
      return;
    }
    g_signal_connect (appsrc,  "need-data", G_CALLBACK (need_data), NULL);

    if (!pipeline || !appsrc || !videoconvert || !capsfilter || !x264enc || !h264parse || !queue1 || !queue2 || !rtph264pay || !udpsink) {
        g_printerr("Failed to create one or more elements.\n");
        return;
    }

    // Configure your elements as needed, e.g., set properties on appsrc, x264enc, udpsink

    if (encoder == "nvenc_h264") {
      gst_bin_add_many(GST_BIN(pipeline), appsrc, cudaupload, cudaconvert, cudaconvert_capsfilter, x264enc, nvh264enc_capsfilter, queue1, rtph264pay, udpsink, NULL);
      gst_element_link_many(appsrc, cudaupload, cudaconvert, cudaconvert_capsfilter, x264enc, nvh264enc_capsfilter, queue1, rtph264pay, udpsink, NULL);
    } else {
      gst_bin_add_many(GST_BIN(pipeline), appsrc, videoconvert, capsfilter, queue1, x264enc, h264parse, queue2, rtph264pay, udpsink, NULL);
      gst_element_link_many(appsrc, videoconvert, capsfilter, queue1, x264enc, h264parse, queue2, rtph264pay, udpsink, NULL);
    }
    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_printerr ("set to playing\n");

    setup_rtsp_server();

}

void UdpH264Streamer::push_buffer_to_gstreamer(const PackedByteArray &raw_data) {
    // Check if pipeline and essential elements are created
    if (!pipeline || !appsrc || !videoconvert || !x264enc || !rtph264pay || !udpsink || !queue1 || !queue2) {
        g_printerr("Pipeline or one of its elements is not created, setting up the pipeline...\n");
        setup_gstreamer_pipeline();
        return;
    }

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

    ClassDB::bind_method(D_METHOD("get_input_width"), &UdpH264Streamer::get_input_width);
    ClassDB::bind_method(D_METHOD("set_input_width", "width"), &UdpH264Streamer::set_input_width);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "input_width"), "set_input_width", "get_input_width");

    ClassDB::bind_method(D_METHOD("get_input_height"), &UdpH264Streamer::get_input_height);
    ClassDB::bind_method(D_METHOD("set_input_height", "height"), &UdpH264Streamer::set_input_height);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "input_height"), "set_input_height", "get_input_height");

    ClassDB::bind_method(D_METHOD("get_port"), &UdpH264Streamer::get_port);
    ClassDB::bind_method(D_METHOD("set_port", "udp_port"), &UdpH264Streamer::set_port);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "udp_port"), "set_port", "get_port");
}
