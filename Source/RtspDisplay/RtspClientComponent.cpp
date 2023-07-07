#include "RtspClientComponent.h"

#include "Common/TcpSocketBuilder.h"
#include "Common/UdpSocketBuilder.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"

#include "MyRunnable.h"

#include "jpeg_frame.hpp"

URtspClientComponent::URtspClientComponent() {
  PrimaryComponentTick.bCanEverTick = true;
}

void URtspClientComponent::BeginPlay() {
  Super::BeginPlay();
}

void URtspClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  disconnect();
  Super::EndPlay(EndPlayReason);
}

void URtspClientComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
                                         FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  // if there's not a new frame, return
  if (!image_data_ready_) {
    return;
  }
  // if we've got a new frame, let's get it
  std::vector<uint8_t> data;
  size_t width = 0;
  size_t height = 0;
  {
    // copy the latest frame, which was set by the worker thread
    std::unique_lock<std::mutex> lock(image_mutex_);
    data.assign(image_data_.begin(), image_data_.end());
    image_data_ready_ = false;
    width = image_width_;
    height = image_height_;
  }

  UE_LOG(LogTemp, Log, TEXT("URtspClientComponent::TickComponent: Got a new frame, size = %d"), data.size());

  // now convert the jpeg frame into a texture and broadcast it
  // create a texture
  auto texture = UTexture2D::CreateTransient(width, height, PF_B8G8R8A8);
  // lock the texture
  uint8 *mip_data = static_cast<uint8 *>(texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
  // copy the jpeg data into the texture
  std::copy(data.data(), data.data() + data.size(), mip_data);
  // unlock the texture
  texture->PlatformData->Mips[0].BulkData.Unlock();
  // update the texture
  texture->UpdateResource();
  // broadcast the texture
  OnFrameReceived.Broadcast(texture);
}

std::string URtspClientComponent::send_request(const std::string& method, const std::string& path,
                                     const std::unordered_map<std::string, std::string>& extra_headers,
                                     std::error_code& ec) {
  // send the request
  std::string request = method + " " + path + " RTSP/1.0\r\n";
  request += "CSeq: " + std::to_string(cseq_) + "\r\n";
  if (session_id_.size() > 0) {
    request += "Session: " + session_id_ + "\r\n";
  }
  for (auto &[key, value] : extra_headers) {
    request += key + ": " + value + "\r\n";
  }
  request += "User-Agent: rtsp-client\r\n";
  request += "Accept: application/sdp\r\n";
  request += "\r\n";
  std::string response;

  uint8_t buffer[1024];
  int bytes_sent = 0;
  int bytes_received = 0;
  bool did_send = rtsp_socket_->Send((uint8_t *)request.c_str(), request.size(), bytes_sent);
  if (!did_send) {
    ec = std::make_error_code(std::errc::io_error);
    UE_LOG(LogTemp, Error, TEXT("Failed to send request"));
    return {};
  }
  if (bytes_sent <= 0) {
    ec = std::make_error_code(std::errc::io_error);
    UE_LOG(LogTemp, Error, TEXT("Failed to send request, bytes_sent = %d"), bytes_sent);
    return {};
  }
  rtsp_socket_->Recv(buffer, sizeof(buffer), bytes_received, ESocketReceiveFlags::None);
  if (bytes_received <= 0) {
    ec = std::make_error_code(std::errc::io_error);
    UE_LOG(LogTemp, Error, TEXT("Failed to receive response"));
    return {};
  }
  response.assign(buffer, buffer + bytes_received);

  // TODO: how to keep receiving until we get the full response?
  // if (response.find("\r\n\r\n") != std::string::npos) {
  //   break;
  // }

  // parse the response
  UE_LOG(LogTemp, Log, TEXT("Response:\n%s"), *FString(response.c_str()));
  if (!parse_response(response)) {
    ec = std::make_error_code(std::errc::io_error);
    UE_LOG(LogTemp, Error, TEXT("Failed to parse response"));
    return {};
  }
  return response;
}

bool URtspClientComponent::connect(FString address, int port, FString path) {
  if (IsConnected) {
    UE_LOG(LogTemp, Warning, TEXT("Already connected, disconnecting first"));
    disconnect();
  }

  UE_LOG(LogTemp, Log, TEXT("Connecting to RTSP server at %s:%d%s"), *address, port, *path);

  FString socket_name = FString::Printf(TEXT("RTSP Socket %s:%d"), *address, port);
  rtsp_socket_ = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, address, false);

  // save the rtsp address and port
  Address = address;
  Port = port;
  Path = path;
  path_ = TCHAR_TO_UTF8(*path);

  // make the server addr
  FIPv4Address ip;
  bool did_parse = FIPv4Address::Parse(address, ip);
  if (!did_parse) {
    UE_LOG(LogTemp, Error, TEXT("Failed to parse IP address"));
    return false;
  }

  rtsp_addr_ = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
  bool valid = false;
  rtsp_addr_->SetIp(ip.Value);
  rtsp_addr_->SetPort(port);

  // now connect
  if (!rtsp_socket_->Connect(*rtsp_addr_)) {
    UE_LOG(LogTemp, Error, TEXT("Failed to connect to RTSP server"));
    return false;
  }

  auto conn_state = rtsp_socket_->GetConnectionState();
  switch (conn_state) {
    case ESocketConnectionState::SCS_NotConnected:
      UE_LOG(LogTemp, Error, TEXT("RTSP socket not connected"));
      return false;
    case ESocketConnectionState::SCS_Connected:
      UE_LOG(LogTemp, Log, TEXT("RTSP socket connected"));
      break;
    case ESocketConnectionState::SCS_ConnectionError:
      UE_LOG(LogTemp, Error, TEXT("RTSP socket connection error"));
      return false;
  }

  // update the state
  IsConnected = true;

  // send the OPTIONS request
  std::error_code ec;
  send_request("OPTIONS", "*", {}, ec);
  return !ec;
}

void URtspClientComponent::disconnect() {
  UE_LOG(LogTemp, Log, TEXT("Disconnecting from RTSP server"));
  // try to send the teardown request, but don't care if it fails
  teardown();
  IsConnected = false;
  IsPlaying = false;
  // stop the main socket
  UE_LOG(LogTemp, Log, TEXT("Stopping RTSP socket"));
  if (rtsp_socket_) {
    rtsp_socket_->Close();
    delete rtsp_socket_;
    rtsp_socket_ = nullptr;
  }
  // stop the threads
  UE_LOG(LogTemp, Log, TEXT("Stopping RTP/RTCP threads"));
  if (rtp_thread_) {
    rtp_thread_->Stop();
    delete rtp_thread_;
  }
  if (rtcp_thread_) {
    rtcp_thread_->Stop();
    delete rtcp_thread_;
  }
  // stop the sockets
  UE_LOG(LogTemp, Log, TEXT("Stopping RTP/RTCP sockets"));
  if (rtp_socket_) {
    rtp_socket_->Close();
    delete rtp_socket_;
  }
  if (rtcp_socket_) {
    rtcp_socket_->Close();
    delete rtcp_socket_;
  }
}

bool URtspClientComponent::describe() {
  std::error_code ec;
  // send the describe request
  auto response = send_request("DESCRIBE", path_, {}, ec);
  if (ec) {
    return false;
  }
  // sdp response is of the form:
  //     std::regex sdp_regex("m=video (\\d+) RTP/AVP (\\d+)");
  // parse the sdp response and get the video port without using regex
  // this is a very simple sdp parser that only works for this specific case
  auto sdp_start = response.find("m=video");
  if (sdp_start == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Invalid sdp"));
    return false;
  }
  auto sdp_end = response.find("\r\n", sdp_start);
  if (sdp_end == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Incomplete sdp"));
    return false;
  }
  auto sdp = response.substr(sdp_start, sdp_end - sdp_start);
  auto port_start = sdp.find(" ");
  if (port_start == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Could not find port start"));
    return false;
  }
  auto port_end = sdp.find(" ", port_start + 1);
  if (port_end == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Could not find port end"));
    return false;
  }
  auto port = sdp.substr(port_start + 1, port_end - port_start - 1);
  video_port_ = std::stoi(port);
  UE_LOG(LogTemp, Log, TEXT("Video port: %d"), video_port_);
  auto payload_type_start = sdp.find(" ", port_end + 1);
  if (payload_type_start == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Could not find payload type start"));
    return false;
  }
  auto payload_type = sdp.substr(payload_type_start + 1, sdp.size() - payload_type_start - 1);
  video_payload_type_ = std::stoi(payload_type);
  UE_LOG(LogTemp, Log, TEXT("Video payload type: %d"), video_payload_type_);
  return true;
}

bool URtspClientComponent::setup(int rtp_port, int rtcp_port) {
  UE_LOG(LogTemp, Log, TEXT("Setting up RTSP session on ports %d-%d"), rtp_port, rtcp_port);
  std::error_code ec;
  // send the setup request
  std::unordered_map<std::string, std::string> extra_headers = {
      {"Transport", "RTP/AVP;unicast;client_port=" + std::to_string(rtp_port) + "-" + std::to_string(rtcp_port)}};
  auto response = send_request("SETUP", path_, extra_headers, ec);
  if (ec) {
    UE_LOG(LogTemp, Error, TEXT("Failed to setup"));
    return false;
  }

  init_rtp(rtp_port);
  init_rtcp(rtcp_port);

  return true;
}

bool URtspClientComponent::play() {
  UE_LOG(LogTemp, Log, TEXT("Playing RTSP session"));
  std::error_code ec;
  // send the play request
  auto response = send_request("PLAY", path_, {}, ec);
  IsPlaying = ec ? false : true;
  return !ec;
}

bool URtspClientComponent::pause() {
  UE_LOG(LogTemp, Log, TEXT("Pausing RTSP session"));
  std::error_code ec;
  // send the pause request
  auto response = send_request("PAUSE", path_, {}, ec);
  IsPlaying = ec ? true : false;
  return !ec;
}

bool URtspClientComponent::teardown() {
  UE_LOG(LogTemp, Log, TEXT("Tearing down RTSP session"));
  std::error_code ec;
  // send the teardown request
  auto response = send_request("TEARDOWN", path_, {}, ec);
  IsPlaying = false;
  return !ec;
}

bool URtspClientComponent::parse_response(const std::string &response) {
  if (response.empty()) {
    UE_LOG(LogTemp, Error, TEXT("Empty response"));
    return false;
  }

  auto response_start = response.find("RTSP/1.0 ");
  if (response_start == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Invalid response"));
    return false;
  }
  // parse the status code and message
  auto response_end = response.find("\r\n", response_start);
  if (response_end == std::string::npos) {
    UE_LOG(LogTemp, Error, TEXT("Incomplete response"));
    return false;
  }
  auto response_code = response.substr(response_start + 9, response_end - response_start - 9);
  if (response_code != "200 OK") {
    UE_LOG(LogTemp, Error, TEXT("Invalid response code: %s"), *FString(response_code.c_str()));
    return false;
  }
  // parse the session id if present
  auto session_start = response.find("Session: ");
  if (session_start != std::string::npos) {
    session_id_ = response.substr(session_start + 9, response.find("\r\n", session_start) - session_start - 9);
  }
  // increment the cseq
  cseq_++;
  return true;
}

void URtspClientComponent::init_rtp(size_t rtp_port) {
  if (rtp_socket_) {
    rtp_socket_->Close();
    delete rtp_socket_;
  }
  FString socket_name = FString::Printf(TEXT("RTP Socket %d"), rtp_port);
  rtp_socket_ = FUdpSocketBuilder(*socket_name)
                    .AsReusable()
                    .BoundToPort(rtp_port)
                    .WithReceiveBufferSize(6 * 1024)
                    .WithSendBufferSize(6 * 1024)
                    .Build();
  UE_LOG(LogTemp, Log, TEXT("RTP port: %d"), rtp_port);
  // make a thread to receive rtp packets using the rtp_socket
  rtp_thread_ = new FMyRunnable(std::bind(&URtspClientComponent::rtp_thread_func, this));
}

void URtspClientComponent::init_rtcp(size_t rtcp_port) {
  if (rtcp_socket_) {
    rtcp_socket_->Close();
    delete rtcp_socket_;
  }
  FString socket_name = FString::Printf(TEXT("RTCP Socket %d"), rtcp_port);
  rtcp_socket_ = FUdpSocketBuilder(*socket_name)
                     .AsReusable()
                     .BoundToPort(rtcp_port)
                     .WithReceiveBufferSize(6 * 1024)
                     .WithSendBufferSize(6 * 1024)
                     .Build();
  UE_LOG(LogTemp, Log, TEXT("RTCP port: %d"), rtcp_port);
  // make a thread to receive rtcp packets using the rtcp_socket
  rtcp_thread_ = new FMyRunnable(std::bind(&URtspClientComponent::rtcp_thread_func, this));
}

void URtspClientComponent::rtp_thread_func() {
  // receive the rtp packet
  size_t max_packet_size = 65536;
  uint8_t *data = new uint8_t[max_packet_size];
  int32 bytes_read = 0;
  rtp_socket_->Recv(data, max_packet_size, bytes_read, ESocketReceiveFlags::None);
  if (bytes_read > 0) {
    std::vector<uint8_t> packet(data, data + bytes_read);
    handle_rtp_packet(packet);
  }
  delete[] data;

  // Sleep the thread for a bit
  FPlatformProcess::Sleep(0.005f);
}

void URtspClientComponent::rtcp_thread_func() {
  // receive the rtcp packet
  size_t max_packet_size = 65536;
  uint8_t *data = new uint8_t[max_packet_size];
  int32 bytes_read = 0;
  rtcp_socket_->Recv(data, max_packet_size, bytes_read, ESocketReceiveFlags::None);
  if (bytes_read > 0) {
    std::vector<uint8_t> packet(data, data + bytes_read);
    handle_rtcp_packet(packet);
  }
  delete[] data;

  // Sleep the thread for a bit
  FPlatformProcess::Sleep(0.005f);
}

void URtspClientComponent::handle_rtp_packet(std::vector<uint8_t> &data) {
  // parse the rtp packet
  // jpeg frame that we are building
  static std::unique_ptr<espp::JpegFrame> jpeg_frame;

  UE_LOG(LogTemp, Log, TEXT("Got RTP packet of size: %d"), data.size());

  std::string_view packet(reinterpret_cast<char *>(data.data()), data.size());
  // parse the rtp packet
  espp::RtpJpegPacket rtp_jpeg_packet(packet);
  auto frag_offset = rtp_jpeg_packet.get_offset();
  if (frag_offset == 0) {
    // first fragment
    UE_LOG(LogTemp, Log, TEXT("Received first fragment, size: %d, sequence number: %d"),
           rtp_jpeg_packet.get_data().size(), rtp_jpeg_packet.get_sequence_number());
    if (jpeg_frame) {
      // we already have a frame, this is an error
      UE_LOG(LogTemp, Warning, TEXT("Received first fragment but already have a frame"));
      jpeg_frame.reset();
    }
    jpeg_frame = std::make_unique<espp::JpegFrame>(rtp_jpeg_packet);
  } else if (jpeg_frame) {
    UE_LOG(LogTemp, Log, TEXT("Received middle fragment, size: %d, sequence number: %d"),
           rtp_jpeg_packet.get_data().size(), rtp_jpeg_packet.get_sequence_number());
    // middle fragment
    jpeg_frame->append(rtp_jpeg_packet);
  } else {
    // we don't have a frame to append to but we got a middle fragment
    // this is an error
    UE_LOG(LogTemp, Warning, TEXT("Received middle fragment without a frame"));
    return;
  }

  // check if this is the last packet of the frame (the last packet will have
  // the marker bit set)
  if (jpeg_frame && jpeg_frame->is_complete()) {
    // get the jpeg data
    auto jpeg_data = jpeg_frame->get_data();
    UE_LOG(LogTemp, Log, TEXT("Received jpeg frame of size: %d B (%d x %d pixels)"),
           jpeg_data.size(), jpeg_frame->get_width(), jpeg_frame->get_height());

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    auto image_format = ImageWrapperModule.DetectImageFormat(jpeg_data.data(), jpeg_data.size());
    if (image_format == EImageFormat::Invalid) {
      UE_LOG(LogTemp, Error, TEXT("Failed to detect image format"));
      return;
    }

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(image_format);
    if (!ImageWrapper.IsValid()) {
      UE_LOG(LogTemp, Error, TEXT("Failed to create image wrapper"));
      return;
    }

    // decompress the jpeg data
    if (!ImageWrapper->SetCompressed(jpeg_data.data(), jpeg_data.size())) {
      UE_LOG(LogTemp, Error, TEXT("Failed to set compressed data"));
      return;
    }
    // Get the decompressed data
    TArray<uint8> UncompressedBGRA;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA)) {
      UE_LOG(LogTemp, Error, TEXT("Failed to get raw data"));
      return;
    }
    size_t width = ImageWrapper->GetWidth();
    size_t height = ImageWrapper->GetHeight();
    auto rgb_data = UncompressedBGRA.GetData();
    auto rgb_data_size = UncompressedBGRA.Num();

    std::unique_lock<std::mutex> lock(image_mutex_);
    image_data_.clear();
    image_data_.reserve(rgb_data_size);
    image_data_.assign(rgb_data, rgb_data + rgb_data_size);
    image_width_ = jpeg_frame->get_width();
    image_height_ = jpeg_frame->get_height();
    image_data_ready_ = true;
    // now reset the jpeg_frame
    jpeg_frame.reset();
  }
}

void URtspClientComponent::handle_rtcp_packet(std::vector<uint8_t> &data) {
  UE_LOG(LogTemp, Log, TEXT("Got RTCP packet of size: %d"), data.size());
  // parse the rtcp packet
  // send the packet to the decoder
}
