extends SubViewport

var image : Image

# Called when the node enters the scene tree for the first time.
func _ready():
	#$"../../UdpH264Streamer".setup_gstreamer_pipeline()
	RenderingServer.frame_post_draw.connect(push)
	$"../../UdpH264Streamer".input_height = self.size.y
	$"../../UdpH264Streamer".input_width = self.size.x
	print("script done with setting up gstreamer")
	
func push():
	$"../../UdpH264Streamer".push_buffer_to_gstreamer(get_texture().get_image().get_data())

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta):
	pass
	# Call the capture_to_buffer() function from the previous answer
	# to get the raw OpenGL buffer data

