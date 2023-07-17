#include "MyRunnable.h"

#pragma region Main Thread Code
// This code will be run on the thread that invoked this thread (i.e. game thread)

FMyRunnable::FMyRunnable(FMyRunnable::callback_t callback)
  : bRunThread(true)
  , Callback(callback)
{
  // Link to the thread that created this object
  Thread = FRunnableThread::Create(this, TEXT("FMyRunnable"));
}

FMyRunnable::~FMyRunnable()
{
  if (Thread != NULL)
  {
    // blocking call until thread has completed
    Thread->Kill();
    delete Thread;
  }
}

void FMyRunnable::Stop()
{
  bRunThread = false;
}

#pragma endregion
// the code below will run on the new thread

bool FMyRunnable::Init()
{
  // This code will not run until the thread has been created
  // This code will run on the thread that created this object

  // This is where you can do any thread specific initialization that needs to be done.
  // This is not the place to start your thread - that should be done in the constructor or Start() method.
  // Return true if initialization was successful, false otherwise
  return true;
}

uint32 FMyRunnable::Run()
{
  // This code will not run until the thread has been created
  // This code will run on the thread that created this object

  // This is the loop that will run on the new thread
  while (bRunThread)
  {
    // do some work
    if (Callback == nullptr)
      break;

    bool ShouldStop = Callback();
    if (ShouldStop)
      break;
  }

  return 0;
}

void FMyRunnable::Exit()
{
  // This code will not run until the thread has been created
  // This code will run on the thread that created this object

  // This is where you can do any thread specific cleanup that needs to be done.
  // This is not the place to kill your thread - that should be done in the destructor or Stop() method.
}
