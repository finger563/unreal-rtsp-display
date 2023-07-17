#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FMyRunnable : public FRunnable
{
public:
  typedef std::function<bool(void)> callback_t;

  FMyRunnable(callback_t callback);

  virtual ~FMyRunnable() override;

  virtual bool Init() override;
  virtual uint32 Run() override;
  virtual void Exit() override;
  virtual void Stop() override;

private:
  bool bRunThread = true;
  callback_t Callback = nullptr;
  FRunnableThread *Thread = nullptr;
};
