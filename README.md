# unreal-rtsp-display
Example code for receiving video streams over RTSP using Unreal Engine FSockets
and displaying them in real time.

https://github.com/finger563/unreal-rtsp-display/assets/213467/05942919-d097-4142-9010-33f8de1883c2

Note: this example currently only supports MJPEG streams over RTSP, which are
parsed with the RtpJpegPacket class into JpegFrames. This example uses Unreal
Engine's built-in image decoding to decode the jpeg frames into uncompressed
image data for use in a UTexture2D.

This example contains a few components:

1. The RtspClientComponent class: This component can be added to an actor and
   exposes some functions for connecting to an RTSP server and configuring /
   controlling the stream. Inside its TickComponent function, it waits for new
   images (decompressed) to be available and if so, creates a new transient
   UTexture2D and writes the decompressed data to the new texture. It then
   broadcasts this new texture using the multicast delegate to any registered
   listeners.
2. The RtpPacket, RtpJpegPacket, JpegHeader, and JpegFrame classes which handle
   the parsing of the media data (as RTP over UDP from the server to the client)
   and reassembling of multiple networks packets into a single jpeg frame.
3. The MyRunnable class: used by the RtspClientComponent when it connects to a
   server it spawns two runnables (Unreal Engine threads) for receiving data
   from the server on the RTP/UDP socket and the RTCP/UDP socket. The RTP
   runnable runs a bound function from the RtspClientComponent class which
   receives the raw data, parses it into jpeg frames, decompresses the jpeg
   frames, and then updates some mutex-protected data to inform the game thread
   (RtspClientComponent::TickComponent) that new data is available.
4. M_Display and M_Display_Inst: these assets in the Content/Materials directory
   are simple materials which render a texture parameter with optional
   configuration for the UV mapping of the texture. This material instance is
   the base for the dynamic material instance that is created at runtime in the
   RtspDisplay blueprint.
5. RtspDisplay: This blueprint actor contains an RtspClientComponent and a Plane
   static mesh component. On BeginPlay it creates a dynamic material instance of
   the M_Display_Inst which it stores a reference to so that it can dynamically
   update the texture parameter that the material is rendering. It sets this
   material on the plane static mesh component. It also uses the
   RtspClientComponent to connect to, setup, and start playing the RTSP stream
   from the RTSP server. It binds an event to the OnFrameReceived event from the
   RtspClientComponent and when it receives a message from that event, it sets
   the new texture to be the dynamic material instance's texture parameter.

Image of the running example in the editor:
<img width="1725" alt="CleanShot 2023-07-08 at 10 37 49@2x" src="https://github.com/finger563/unreal-rtsp-display/assets/213467/74a2898c-718e-422e-87f3-79f995188df8">

Image showing the RTSP configuration parameters on the RtspDisplay blueprint actor:
<img width="1723" alt="CleanShot 2023-07-08 at 10 38 20@2x" src="https://github.com/finger563/unreal-rtsp-display/assets/213467/7315797f-4fb3-4af4-9d50-0ffbdee99de2">
