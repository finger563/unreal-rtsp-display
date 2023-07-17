#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IPAddress.h"
#include "RtspClientComponent.generated.h"

class FMyRunnable;
class FSocket;
class UTexture2D;

// Blueprints can bind to this to update the UI
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisconnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlay);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPause);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFrameReceived, UTexture2D*, Texture);

/**
 * @brief This class is used to connect to a RTSP server and receive the video
 *        stream.
 *
 * @details This class is used to connect to a RTSP server and receive the video
 *          stream. It currently supports only MJPEG streams (which are simply a
 *          sequence of JPEG images). It uses a TCP socket (FSocket with
 *          ESocketType::SOCKTYPE_Streaming) to connect to the RTSP server send
 *          RTSP requests and receive RTSP responses. It uses a UDP socket
 *          (FSocket with ESocketType::SOCKTYPE_Datagram) to receive the RTP and
 *          RTCP packets, from which it extracts the JPEG images. It will
 *          convert the JPEG images to UTexture2D and broadcast them to the
 *          any registered listeners.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
  class RTSPDISPLAY_API URtspClientComponent : public UActorComponent
{
  GENERATED_BODY()
public:

  URtspClientComponent();

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool connect(FString rtsp_address, int rtsp_port = 8554, FString path = TEXT("/mjpeg/1"));

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  void disconnect();

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool describe();

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool setup(int rtp_port = 5000, int rtcp_port = 5001);

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool play();

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool pause();

  UFUNCTION(BlueprintCallable, Category = "RTSP")
  bool teardown();

  UPROPERTY(BlueprintAssignable, Category = "RTSP")
  FOnConnected OnConnected;

  UPROPERTY(BlueprintAssignable, Category = "RTSP")
  FOnDisconnected OnDisconnected;

  UPROPERTY(BlueprintAssignable, Category = "RTSP")
  FOnPause OnPause;

  UPROPERTY(BlueprintAssignable, Category = "RTSP")
  FOnPlay OnPlay;

  UPROPERTY(BlueprintAssignable, Category = "RTSP")
  FOnFrameReceived OnFrameReceived;

  UPROPERTY(BlueprintReadOnly, Category = "RTSP")
  FString Address;

  UPROPERTY(BlueprintReadOnly, Category = "RTSP")
  int Port = 8554;

  UPROPERTY(BlueprintReadOnly, Category = "RTSP")
  FString Path = TEXT("/mjpeg/1");

  UPROPERTY(BlueprintReadOnly, Category = "RTSP")
  bool IsConnected = false;

  UPROPERTY(BlueprintReadOnly, Category = "RTSP")
  bool IsPlaying = false;

 protected:

  std::string send_request(const std::string& method, const std::string& path,
                           const std::unordered_map<std::string, std::string> &extra_headers, std::error_code &ec);

  bool parse_response(const std::string &response_data);

  void init_rtp(size_t rtp_port);

  void init_rtcp(size_t rtcp_port);

  bool connect_thread_func();

  bool rtp_thread_func();

  bool rtcp_thread_func();

  void handle_rtp_packet(std::vector<uint8_t> &data);

  void handle_rtcp_packet(std::vector<uint8_t> &data);

  FSocket *rtsp_socket_ = nullptr;
  FSocket *rtp_socket_ = nullptr;
  FSocket *rtcp_socket_ = nullptr;

  FMyRunnable *connect_thread_ = nullptr;
  FMyRunnable *rtp_thread_ = nullptr;
  FMyRunnable *rtcp_thread_ = nullptr;

  std::string path_;
  int cseq_ = 0;
  int video_port_ = 0;
  int video_payload_type_ = 0;
  std::string session_id_;

  std::mutex image_mutex_;
  std::vector<uint8_t> image_data_;
  std::atomic<bool> image_data_ready_ = false;
  std::atomic<int> image_width_ = 0;
  std::atomic<int> image_height_ = 0;

  TSharedPtr<FInternetAddr> rtsp_addr_;

 public:

  void BeginPlay() override;
  void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
};
