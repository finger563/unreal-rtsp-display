# unreal-rtsp-display
Example code for receiving video streams over RTSP using Unreal Engine FSockets
and displaying them in real time.

https://github.com/finger563/unreal-rtsp-display/assets/213467/64270d95-f223-43ef-885b-599e607d48b1

When running this example, you will need to configure the address, port, and
path of your RTSP server on the actor:

![CleanShot 2023-07-18 at 13 42 36](https://github.com/finger563/unreal-rtsp-display/assets/213467/8884b601-5fa0-4b29-89db-1c271c8055cc)

Note: this example currently only supports MJPEG streams over RTSP, which are
parsed with the RtpJpegPacket class into JpegFrames. This example uses Unreal
Engine's built-in image decoding to decode the jpeg frames into uncompressed
image data for use in a UTexture2D.

This example contains a few components:

1. The `RtspClientComponent` class: This component can be added to an actor and
   exposes some functions for connecting to an RTSP server and configuring /
   controlling the stream. Inside its TickComponent function, it waits for new
   images (decompressed) to be available and if so, creates a new transient
   UTexture2D and writes the decompressed data to the new texture. It then
   broadcasts this new texture using the multicast delegate to any registered
   listeners.
2. The `RtpPacket`, `RtpJpegPacket`, `JpegHeader`, and `JpegFrame` classes which
   handle the parsing of the media data (as RTP over UDP from the server to the
   client) and reassembling of multiple networks packets into a single jpeg
   frame.
3. The `MyRunnable` class: used by the RtspClientComponent when it connects to a
   server it spawns two runnables (Unreal Engine threads) for receiving data
   from the server on the RTP/UDP socket and the RTCP/UDP socket. The RTP
   runnable runs a bound function from the RtspClientComponent class which
   receives the raw data, parses it into jpeg frames, decompresses the jpeg
   frames, and then updates some mutex-protected data to inform the game thread
   (RtspClientComponent::TickComponent) that new data is available. This class
   is also used to allow the FSocket::Connect (TCP connection from RTSP Client
   to RTSP Server) to run without blocking the main / game thread.
4. `M_Display` and `M_Display_Inst`: these assets in the Content/Materials
   directory are simple materials which render a texture parameter with optional
   configuration for the UV mapping of the texture. This material instance is
   the base for the dynamic material instance that is created at runtime in the
   RtspDisplay blueprint.
5. `RtspDisplay`: This blueprint actor contains an RtspClientComponent and a
   Plane static mesh component. On BeginPlay it creates a dynamic material
   instance of the M_Display_Inst which it stores a reference to so that it can
   dynamically update the texture parameter that the material is rendering. It
   sets this material on the plane static mesh component. It binds an event to
   the OnFrameReceived event from the RtspClientComponent and when it receives a
   message from that event, it sets the new texture to be the dynamic material
   instance's texture parameter.
6. `W_RtspDisplay`: This user widget contains the UI (2D) for interacting with a
   RtspClientComponent. It is configured by the `RtspDisplayMap`'s level
   blueprint to be added as the UI to the viewport for the first player
   controller and to control the RtspClientComponent of the RtspDisplay actor in
   the level. It provides the UI (display image, URL textbox, and buttons for
   connect, disconnect, play, and pause) for the RtspClientComponent.

Image of the running example in the editor:

Disconnected (With text box to write URI and connect button):
![CleanShot 2023-07-18 at 13 40 12](https://github.com/finger563/unreal-rtsp-display/assets/213467/88722e5d-f8fa-4852-b55b-3ba9be8da057)
Connected:
![CleanShot 2023-07-18 at 13 40 27](https://github.com/finger563/unreal-rtsp-display/assets/213467/9271463d-55eb-47bc-aedc-0aea512df317)
Playing:
![CleanShot 2023-07-18 at 13 40 42](https://github.com/finger563/unreal-rtsp-display/assets/213467/885ee177-535e-4da9-a843-aa2342e79ee0)

### Details

#### RtspDisplay Actor Blueprint

![CleanShot 2023-07-18 at 13 44 33](https://github.com/finger563/unreal-rtsp-display/assets/213467/6d7109b6-fd43-46af-b526-889ab9237294)

#### M_Display Material

<img width="1242" alt="CleanShot 2023-07-08 at 10 51 50@2x" src="https://github.com/finger563/unreal-rtsp-display/assets/213467/656a5447-39db-4fcc-bb16-92a839dc4e41">

#### RtspDisplay User Widget

![CleanShot 2023-07-18 at 13 45 26](https://github.com/finger563/unreal-rtsp-display/assets/213467/ecab159c-0201-4ee0-8fdd-90ee3e997023)
Note the image has been set with angle 180 and X scale of -1 so that it matches the camera image that I'm currently sending. May need to be changed for other streams depending on camera orientation.

Its blueprint:
![CleanShot 2023-07-18 at 13 48 23](https://github.com/finger563/unreal-rtsp-display/assets/213467/bbea4667-841b-4004-8afa-b12e4b667da2)

#### Level Blueprint

![CleanShot 2023-07-18 at 13 48 56](https://github.com/finger563/unreal-rtsp-display/assets/213467/c97d9954-a887-4773-8a3b-54104b102e31)


### Setup for Android App

Follow the setup instructions
[here](https://docs.unrealengine.com/5.2/en-US/how-to-set-up-android-sdk-and-ndk-for-your-unreal-engine-development-environment/).

Note: you will likely have to modify the `/Users/Shared/Epic\
Games/UE_5.2/Engine/Extras/Android/SetupAndroid.command` file - possibly to
point to the right `JAVA_HOME` directory. In my case I had to modify the
JAVA_HOME export in the `SetupAndroid.command` file to point to
`/Library/Java/JavaVirtualMachines/adoptopenjdk-8.jdk/Contents/Home` and had
to install jdk8 specifically.

You will need to set the environment variables (under `Android SDK`)
appropriately, e.g.:

- `Android SDK` : `/Users/bob/Library/Android/sdk`
- `Android NDK` : `/Users/bob/Library/Android/sdk/ndk/25.1.8937393`
- `Location of JAVA` : `/Library/Java/JavaVirtualMachines/adoptopenjdk-8.jdk/Contents/Home`
- `SDK API Level` : `android-32`
- `NDK API Level` : `android-32`

For that version of java (jdk 8) which is required to successfully build for
android, you can (on macos) install it via:

``` sh
brew install --cask adoptopenjdk8
```

