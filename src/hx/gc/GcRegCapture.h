#ifndef HX_GC_HELPERS_INCLUDED
#define HX_GC_HELPERS_INCLUDED

#include <stddef.h>

// For code that reads whole stack ranges, including AddressSanitizer's redzones
#if defined(__SANITIZE_ADDRESS__) && defined(_MSC_VER) && !defined(__clang__)
   #define HX_NO_SANITIZE_ADDRESS __declspec(no_sanitize_address)
#elif defined(__SANITIZE_ADDRESS__)
   #define HX_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#elif defined(__has_feature)
   #if __has_feature(address_sanitizer)
      #define HX_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
   #endif
#endif
#ifndef HX_NO_SANITIZE_ADDRESS
   #define HX_NO_SANITIZE_ADDRESS
#endif

// By what the compiler targets: an Xcode arm64 build may define HXCPP_M64 and not HXCPP_ARM64
#if defined(HXCPP_ARM64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define HXCPP_CAPTURE_SETJMP
#endif

#ifdef HXCPP_CAPTURE_SETJMP
   #include <setjmp.h>
#else

   #if (defined(HX_WINDOWS) || defined(HX_MACOS)  || (defined(HX_LINUX) && defined(__i386__))) && !defined(HXCPP_M64)
      #define HXCPP_CAPTURE_x86
   #endif

   #if (defined(HX_MACOS) || (defined(HX_WINDOWS) && !defined(HX_WINRT)) || defined(_XBOX_ONE) || defined(HX_LINUX)) && defined(HXCPP_M64) && (defined(__x86_64__) || defined(_M_X64))
      #define HXCPP_CAPTURE_x64
   #endif

   #if (defined(HXCPP_CAPTURE_x86) || defined(HXCPP_CAPTURE_x64)) && !defined(__GNUC__)
      // MSVC: RtlCaptureContext and CONTEXT
      #include <windows.h>
      #define HXCPP_CAPTURE_CONTEXT
   #endif


#endif


namespace hx
{

// Capture Registers
//
// A pointer that the interrupted code holds only in a callee-saved register has
// to be found. The capturing function (PauseForCollect, EnterGCFreeZone,
// SetupStackAndCollect) may itself have saved such a register in its prologue and
// reused it, leaving the caller's value only in its save area - and where that
// area lies relative to the 'dummy' local that marks the bottom of the scanned
// stack depends on the compiler: MSVC puts it below the locals on x86, and below
// the scanned range on x64. EnterGCFreeZone also returns before a collection
// scans the thread, and its frame is reused by then.
//
// So along with the registers, the words from the capture point up are copied:
// the capturing frame, save area included, and the few frames above it.
enum { CAPTURE_FRAME_WORDS = 64 };

// Copies the stack from inFrom up, to at most inTopOfStack and CAPTURE_FRAME_WORDS
// words, into outWords, and returns how many it copied.
size_t CopyStackWords(void **outWords, void *inFrom, int *inTopOfStack);

// Copies the stack from just below the calling function's frame up: its frame is
// above the one this function runs in.
size_t CaptureCallerFrame(void **outWords, int *inTopOfStack);


#ifdef HXCPP_CAPTURE_SETJMP // {

struct RegisterCaptureBuffer
{
   jmp_buf registers;
   void    *frame[CAPTURE_FRAME_WORDS];
   // Zero until the first capture: a thread is marked from the time it attaches
   size_t  frameWords = 0;
};

#define CAPTURE_REGS \
   setjmp(mRegisterBuf.registers); \
   mRegisterBuf.frameWords = hx::CaptureCallerFrame(mRegisterBuf.frame, mTopOfStack);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(mRegisterBuf.frame + mRegisterBuf.frameWords)

#elif defined(HXCPP_CAPTURE_CONTEXT) // } {

// Windows x86 and x64 (MSVC). RtlCaptureContext is called by the capturing function
// itself, not by a helper: a helper that needs its argument after the call keeps it
// in a callee-saved register, and so records its own value there instead of the
// caller's. Every register is recorded, and the stack is copied from the captured
// stack pointer up.
struct RegisterCaptureBuffer
{
   CONTEXT context;
   void    *frame[CAPTURE_FRAME_WORDS];
   size_t  frameWords = 0;
};

#ifdef HXCPP_CAPTURE_x64
   #define CAPTURE_FRAME \
      hx::CopyStackWords(mRegisterBuf.frame, (void *)mRegisterBuf.context.Rsp, mTopOfStack)
#else
   // On x86, RtlCaptureContext reports the stack pointer the capturing function
   // returns with, above its frame and so above the registers it saved, so the
   // copy starts from below it instead.
   #define CAPTURE_FRAME \
      hx::CaptureCallerFrame(mRegisterBuf.frame, mTopOfStack)
#endif

#define CAPTURE_REGS \
   RtlCaptureContext(&mRegisterBuf.context); \
   mRegisterBuf.frameWords = CAPTURE_FRAME;

#define CAPTURE_REG_START (int *)(&mRegisterBuf.context)
#define CAPTURE_REG_END (int *)(mRegisterBuf.frame + mRegisterBuf.frameWords)

#elif defined(HXCPP_CAPTURE_x86) // } {

struct RegisterCaptureBuffer
{
   void *ebx;
   void *edi;
   void *esi;
   void *ebp;

   void   *frame[CAPTURE_FRAME_WORDS];
   size_t frameWords = 0;
};

void CaptureX86(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX86(mRegisterBuf); \
   mRegisterBuf.frameWords = hx::CaptureCallerFrame(mRegisterBuf.frame, mTopOfStack);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(mRegisterBuf.frame + mRegisterBuf.frameWords)

#elif defined(HXCPP_CAPTURE_x64) // }  {


struct RegisterCaptureBuffer
{
   void *rbx;
   void *rbp;
   void *rdi;
   void *r12;
   void *r13;
   void *r14;
   void *r15;

   void *xmm[16*2];

   void   *frame[CAPTURE_FRAME_WORDS];
   size_t frameWords = 0;
};

void CaptureX64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX64(mRegisterBuf); \
   mRegisterBuf.frameWords = hx::CaptureCallerFrame(mRegisterBuf.frame, mTopOfStack);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(mRegisterBuf.frame + mRegisterBuf.frameWords)


#elif defined(HXCPP_CAPTURE_ARM64) // }  {


struct RegisterCaptureBuffer
{
   void *x19;
   void *x20;
   void *x21;
   void *x22;
   void *x23;
   void *x24;
   void *x25;
   void *x26;
   void *x27;
   void *x28;
};

void CaptureArm64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureArm64(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)


#else //  }  default capture... {


class RegisterCapture
{
public:
	virtual int Capture(int *inTopOfStack,int **inBuf,int &outSize,int inMaxSize,int *inDummy);
   static RegisterCapture *Instance();
};

typedef int *RegisterCaptureBuffer[20];

#define CAPTURE_REGS \
   hx::RegisterCapture::Instance()->Capture(mTopOfStack, \
                mRegisterBuf,mRegisterBufSize,20,mBottomOfStack); \

#define CAPTURE_REG_START (int *)mRegisterBuf
#define CAPTURE_REG_END (int *)(mRegisterBuf+mRegisterBufSize)

#endif // }



}


#endif
