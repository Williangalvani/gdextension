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


std::string x264enc_factory = "( appsrc name=mysrc is-live=true ! queue leaky=upstream ! videoconvert ! x264enc tune=zerolatency bitrate=10000 ! video/x-h264,profile=high ! queue leaky=downstream ! rtph264pay name=pay0 pt=96 ! udpsink host=127.0.0.1 port=5600 )";
std::string vtenc_factory = "( appsrc name=mysrc is-live=true ! queue leaky=upstream ! videoconvert ! vtenc_h264_hw bitrate=10000 ! video/x-h264,profile=high ! queue leaky=downstream ! rtph264pay name=pay0 pt=96 ! udpsink host=127.0.0.1 port=5600 )";
std::string nvh264enc_factory = "appsrc name=godotsrc do-timestamp=true is-live=true format=time ! video/x-raw, ! queue leaky=upstream ! videoconvert ! nvh264enc bitrate=10000 ! video/x-h264,profile=high ! queue leaky=downstream ! rtph264pay config-interval=1 pt=96 ! udpsink host=127.0.0.1 port=5600";

GstElement* global_pipeline = NULL;

static void
need_data()
{
    need_frame = true;
}

GstElement *appsrc = NULL;

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


static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}


void UdpH264Streamer::setup_rtsp_server()
{
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;

    /* create a server instance */
    rtsp_server = gst_rtsp_server_new ();

    /* get the mount points for this server, every server has a default object
    * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points (rtsp_server);

    /* make a media factory for a test stream. The default media factory can use
    * gst-launch syntax to create pipelines.
    * any launch line works as long as it contains elements named pay%d. Each
    * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new ();
    gst_rtsp_media_factory_set_launch (factory, "( "
        "udpsrc port=5600 re-use=true ! application/x-rtp,media=video,clock-rate=90000,encoding-name=H264 ! rtph264depay ! h264parse config-interval=1 ! rtph264pay name=pay0 pt=96 )");

    gst_rtsp_media_factory_set_enable_rtcp(factory, FALSE);
    gst_rtsp_media_factory_set_profiles (factory, GST_RTSP_PROFILE_AVPF);

    /* store up to 0.4 seconds of retransmission data */
    //gst_rtsp_media_factory_set_retransmission_time (factory, 400 * GST_MSECOND);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

    /* don't need the ref to the mapper anymore */
    g_object_unref (mounts);

    /* attach the server to the default maincontext */
    if (gst_rtsp_server_attach (rtsp_server, NULL) == 0)
        GST_ERROR("stream failed\n");

    /* add a timeout for the session cleanup */
    g_timeout_add_seconds (2, (GSourceFunc) timeout, rtsp_server);

    /* start serving, this never stops */
    g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");
    //auto encoder = find_working_hw_encoder();
    pipeline = gst_parse_launch(nvh264enc_factory.c_str(), NULL);
    if (pipeline) {
         GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    } else {
        GST_ERROR(" BAD PIPELINE");
    }
    global_pipeline = pipeline;
    

    GstFlowReturn ret;

    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "godotsrc");

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


}

UdpH264Streamer::UdpH264Streamer()
{
    gst_init(NULL, NULL);
    main_loop = g_main_loop_new(NULL, FALSE); // Create a new GMainLoop
    need_frame = true;
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
    if (!appsrc)
    {
        appsrc = gst_bin_get_by_name(GST_BIN(global_pipeline), "godotsrc");
        if (!appsrc) {
        GST_ERROR(" APPSRC IS NULL!");
        }
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
       // GST_ERROR("NO NEED FRAME");
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
