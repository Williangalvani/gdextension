#include "rtspserver.h"
#include <godot_cpp/core/class_db.hpp>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

using namespace godot;

bool need_frame = true;

RtspServer::RtspServer() {
    // Initialize any variables here.
    gst_init(NULL, NULL);
    gst_debug_set_default_threshold(GST_LEVEL_WARNING);
    need_frame = true;
}

RtspServer::~RtspServer() {
    // Add your cleanup here.
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

void RtspServer::_process(double delta) {
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

void RtspServer::setup_gstreamer_pipeline() {
  pipeline = gst_pipeline_new ("mypipeline");
  appsrc = gst_element_factory_make ("appsrc", "mysrc");
  conv = gst_element_factory_make ("videoconvert", "myconvert");
  x264enc = gst_element_factory_make ("x264enc", "myencoder");
  rtph264pay = gst_element_factory_make ("rtph264pay", "myrtppay");
  GstRTSPServer *server = gst_rtsp_server_new();
  GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

  if (!pipeline || !appsrc || !conv || !x264enc || !rtph264pay || !server || !mounts) {
    g_printerr ("Failed to create one or more elements.\n");
    return;
  }

 // Set up the appsrc
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 100, 1,
      NULL);
  g_object_set (appsrc, "caps", caps, "format", GST_FORMAT_TIME,"is-live", true, NULL);
  gst_caps_unref (caps);

  g_object_set (x264enc, "tune", 0x00000004, NULL); // Set zerolatency tune
  g_object_set (udpsink, "host", "0.0.0.0", "port", 8554, NULL);


  g_signal_connect (appsrc,  "need-data", G_CALLBACK (need_data), NULL);

  gst_bin_add_many (GST_BIN (pipeline), appsrc, conv, x264enc, rtph264pay, NULL);
  gst_element_link_many (appsrc, conv, x264enc, rtph264pay, NULL);

  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  gst_rtsp_media_factory_set_launch(factory, "( mypipeline )");
  gst_rtsp_mount_points_add_factory(mounts, "/test", factory);
  gst_rtsp_server_attach(server, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

void RtspServer::push_buffer_to_gstreamer(const PackedByteArray &raw_data) {
  if (!pipeline || !appsrc || !conv || !x264enc || !rtph264pay || !udpsink) {
    g_printerr ("creating pipeline...\n");
    setup_gstreamer_pipeline();
    return;
  }
  if (need_frame){
    push_frame(appsrc, raw_data);
    need_frame = false;
  }
}

void RtspServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup_gstreamer_pipeline"), &RtspServer::setup_gstreamer_pipeline);
    ClassDB::bind_method(D_METHOD("push_buffer_to_gstreamer"), &RtspServer::push_buffer_to_gstreamer);
}

