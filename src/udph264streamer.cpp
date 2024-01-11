#include "udph264streamer.h"
#include <godot_cpp/core/class_db.hpp>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

using namespace godot;

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *)DEFAULT_RTSP_PORT;
bool need_frame = true;
int input_width = 1152;
int input_height = 648;


std::string x264enc_factory = "( appsrc name=mysrc is-live=true ! videoconvert ! x264enc tune=zerolatency bitrate=10000 ! video/x-h264,profile=baseline ! rtph264pay name=pay0 pt=96 )";
std::string vtenc_factory = "( appsrc name=mysrc is-live=true ! videoconvert ! vtenc_h264_hw bitrate=10000 ! video/x-h264,profile=baseline ! rtph264pay name=pay0 pt=96 )";
std::string nvh264enc_factory = "( appsrc name=mysrc is-live=true ! videoconvert ! nvh264enc bitrate=10000 ! video/x-h264,profile=baseline ! rtph264pay name=pay0 pt=96 )";

static void
need_data()
{
    need_frame = true;
}

GstElement *appsrc = NULL;

std::string find_working_hw_encoder()
{
    std::vector<std::string> encoders = {"nvh264enc", "msdkh264enc", "vtenc_h264_hw", "x264enc"};

    for (const auto &encoder_name : encoders)
    {
        std::string pipeline_str = "videotestsrc ! " + encoder_name + " ! fakesink";
        GError *error = NULL;
        GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

        if (pipeline == NULL || error != NULL)
        {
            g_printerr("Failed to create pipeline: %s\n", error->message);
            g_error_free(error);
            continue;
        }

        GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE)
        {
            gst_object_unref(pipeline);
            continue;
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return encoder_name;
    }
    return "";
}

/* called when a new media pipeline is constructed. We can query the
 * pipeline and configure our appsrc */
static void
media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media,
                gpointer user_data)
{
    GstElement *element;
    GstFlowReturn ret;

    /* get the element used for providing the streams of the media */
    element = gst_rtsp_media_get_element(media);

    /* get our appsrc, we named it 'mysrc' with the name property */
    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");

    /* this instructs appsrc that we will be dealing with timed buffer */
    gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");
    /* configure the caps of the video */
    g_object_set(G_OBJECT(appsrc), "caps",
                 gst_caps_new_simple("video/x-raw",
                                     "format", G_TYPE_STRING, "RGB",
                                     "width", G_TYPE_INT, input_width,
                                     "height", G_TYPE_INT, input_height,
                                     "framerate", GST_TYPE_FRACTION, 60, 1, NULL),
                 NULL);

    g_signal_connect(appsrc, "need-data", (GCallback)need_data, &ret);
    // gst_object_unref (appsrc);
    gst_object_unref(element);
}

void UdpH264Streamer::setup_rtsp_server()
{
    GError *error = NULL;

    // Create the RTSP server
    rtsp_server = gst_rtsp_server_new();
    if (!rtsp_server)
    {
        g_printerr("Failed to create RTSP server\n");
        return;
    }

    // Set the server's service port
    g_object_set(rtsp_server, "service", port, NULL);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(rtsp_server);

    factory = gst_rtsp_media_factory_new();


      auto encoder = find_working_hw_encoder();
    if (encoder == "vtenc_h264_hw") {
        gst_rtsp_media_factory_set_launch(factory,
                                      vtenc_factory.c_str());
    } else if (encoder == "x264enc") {
        gst_rtsp_media_factory_set_launch(factory,
                                      x264enc_factory.c_str());
    } else if (encoder == "nvh264enc") {
                gst_rtsp_media_factory_set_launch(factory,
                                      nvh264enc_factory.c_str());
    } else if (encoder == "msdkh264enc") {
    } else {
        g_printerr("No working encoder found\n");
        return;
    }



    /* notify when our media is ready, This is called whenever someone asks for
     * the media and a new pipeline with our appsrc is created */
    g_signal_connect(factory, "media-configure", (GCallback)media_configure,
                     NULL);
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);

    /* don't need the ref to the mounts anymore */
    g_object_unref(mounts);
    /* attach the server to the default maincontext */
    gst_rtsp_server_attach(rtsp_server, NULL);

    // Server is running now
    g_print("RTSP server is running\n");
}

UdpH264Streamer::UdpH264Streamer()
{
    gst_init(NULL, NULL);
    main_loop = g_main_loop_new(NULL, FALSE); // Create a new GMainLoop
    need_frame = false;
}

UdpH264Streamer::~UdpH264Streamer()
{

    if (rtsp_server)
    {
        g_object_unref(rtsp_server);
        rtsp_server = NULL;
    }
    if (factory)
    {
        g_object_unref(factory);
        factory = NULL;
    }
    if (main_loop)
    {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }
}

void UdpH264Streamer::_process(double delta)
{
    if (main_loop)
    {
        // Iterate the GStreamer main loop
        g_info("Running main loop");
        while (g_main_context_pending(g_main_loop_get_context(main_loop)))
        {
            g_main_context_iteration(g_main_loop_get_context(main_loop), FALSE);
        }
    }
}

static void push_frame(const PackedByteArray &raw_data)
{
    GST_ERROR("PUSHIN FRAME");
    if (!appsrc)
    {
        GST_ERROR(" APPSRC IS NULL!");
    }
    static GstClockTime timestamp = 0;
    guint size;
    GstBuffer *buffer;
    GstFlowReturn ret;
    buffer = gst_buffer_new_allocate(NULL, raw_data.size(), NULL);
    gst_buffer_fill(buffer, 0, raw_data.ptr(), raw_data.size());
    GST_BUFFER_PTS(buffer) = timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 60);
    timestamp += GST_BUFFER_DURATION(buffer);
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
}

void UdpH264Streamer::push_buffer_to_gstreamer(const PackedByteArray &raw_data)
{
    // Check if pipeline and essential elements are created
    if (!rtsp_server)
    {
        g_printerr("Pipeline or one of its elements is not created, setting up the pipeline...\n");
        setup_rtsp_server();
        return;
    }

    // If the pipeline is playing and a new frame is needed, push the frame
    if (need_frame)
    {
        push_frame(raw_data);
        // need_frame = false;
    }
    else
    {
        GST_ERROR("NO NEED FRAME");
    }
}

int UdpH264Streamer::get_port()
{
    return rtsp_port;
}

void UdpH264Streamer::set_port(int port)
{
    rtsp_port = port;
}


int UdpH264Streamer::get_input_width() {
    return input_width;
}

void UdpH264Streamer::set_input_width(int width) {
    input_width = width;
}

int UdpH264Streamer::get_input_height() {
    return input_height;
}

void UdpH264Streamer::set_input_height(int height) {
    input_height = height; 
}


void UdpH264Streamer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("push_buffer_to_gstreamer"), &UdpH264Streamer::push_buffer_to_gstreamer);

    ClassDB::bind_method(D_METHOD("get_input_width"), &UdpH264Streamer::get_input_width);
    ClassDB::bind_method(D_METHOD("set_input_width", "width"), &UdpH264Streamer::set_input_width);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "input_width"), "set_input_width", "get_input_width");

    ClassDB::bind_method(D_METHOD("get_input_height"), &UdpH264Streamer::get_input_height);
    ClassDB::bind_method(D_METHOD("set_input_height", "height"), &UdpH264Streamer::set_input_height);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "input_height"), "set_input_height", "get_input_height");

    ClassDB::bind_method(D_METHOD("get_port"), &UdpH264Streamer::get_port);
    ClassDB::bind_method(D_METHOD("set_port", "rtsp_port"), &UdpH264Streamer::set_port);
    ClassDB::add_property("UdpH264Streamer", PropertyInfo(Variant::INT, "rtsp_port"), "set_port", "get_port");
}
