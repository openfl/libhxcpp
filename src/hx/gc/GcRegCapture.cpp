#include "GcRegCapture.h"

#include <string.h>

#if defined(_MSC_VER)
   #define HX_CAPTURE_NOINLINE __declspec(noinline)
#else
   #define HX_CAPTURE_NOINLINE __attribute__((noinline))
#endif

namespace hx {

#if defined(HXCPP_CAPTURE_SETJMP) || defined(HXCPP_CAPTURE_CONTEXT) || defined(HXCPP_CAPTURE_x86) || defined(HXCPP_CAPTURE_x64) // {

HX_NO_SANITIZE_ADDRESS
size_t CopyStackWords(void **outWords, void *inFrom, int *inTopOfStack)
{
   // Start on a word boundary, rounding down into the frame being copied, so the
   // copied words hold the pointers stored there rather than halves of two
   void **from = (void **)((size_t)inFrom & ~(sizeof(void *) - 1));
   void **to = (void **)inTopOfStack;
   if (!to || to <= from)
      return 0;

   size_t words = (size_t)(to - from);
   if (words > CAPTURE_FRAME_WORDS)
      words = CAPTURE_FRAME_WORDS;
   // Not memcpy, which AddressSanitizer checks even here
   for(size_t i=0;i<words;i++)
      outWords[i] = ((void * volatile *)from)[i];
   return words;
}

// Not inlined, so this frame is below the capturing function's and copying up
// from one of its locals takes in all of the capturing frame, save area included.
HX_CAPTURE_NOINLINE size_t CaptureCallerFrame(void **outWords, int *inTopOfStack)
{
   volatile int marker = 0;
   return CopyStackWords(outWords, (void *)&marker, inTopOfStack);
}

#endif // }

} // end namespace hx


#ifdef HXCPP_CAPTURE_SETJMP // {

// Nothing

#elif defined(HXCPP_CAPTURE_CONTEXT) // } {

// RtlCaptureContext and CopyStackWords

#elif defined(HXCPP_CAPTURE_x86) // }  {

namespace hx {

void CaptureX86(RegisterCaptureBuffer &outBuffer)
{
   void *regEsi;
   void *regEdi;
   void *regEbx;
   // EBP is another callee-saved general register when frame pointers are
   // omitted. A live object may be held only there across a collection.
   void *regEbp;
   asm ("mov %%esi, %0\n\t" : "=r" (regEsi) );
   asm ("mov %%edi, %0\n\t" : "=r" (regEdi) );
   asm ("mov %%ebx, %0\n\t" : "=r" (regEbx) );
   asm ("mov %%ebp, %0\n\t" : "=r" (regEbp) );
   outBuffer.esi = regEsi;
   outBuffer.edi = regEdi;
   outBuffer.ebx = regEbx;
   outBuffer.ebp = regEbp;
}

} // end namespace hx

#elif defined(HXCPP_CAPTURE_x64) // } {

namespace hx {

void CaptureX64(RegisterCaptureBuffer &outBuffer)
{
      void *regBx;
      void *regBp;
      void *regDi;
      void *reg12;
      void *reg13;
      void *reg14;
      void *reg15;
      asm ("movq %%rbx, %0\n\t" : "=r" (regBx) );
      asm ("movq %%rbp, %0\n\t" : "=r" (regBp) );
      asm ("movq %%rdi, %0\n\t" : "=r" (regDi) );
      asm ("movq %%r12, %0\n\t" : "=r" (reg12) );
      asm ("movq %%r13, %0\n\t" : "=r" (reg13) );
      asm ("movq %%r14, %0\n\t" : "=r" (reg14) );
      asm ("movq %%r15, %0\n\t" : "=r" (reg15) );
      outBuffer.rbx = regBx;
      outBuffer.rbp = regBp;
      outBuffer.rdi = regDi;
      outBuffer.r12 = reg12;
      outBuffer.r13 = reg13;
      outBuffer.r14 = reg14;
      outBuffer.r15 = reg15;
}

} // end namespace hx


#elif defined(HXCPP_CAPTURE_ARM64) // } {

namespace hx {

void CaptureArm64(RegisterCaptureBuffer &outBuffer)
{
   void *regX19;
   void *regX20;
   void *regX21;
   void *regX22;
   void *regX23;
   void *regX24;
   void *regX25;
   void *regX26;
   void *regX27;
   void *regX28;
   asm ("mov %0, x19\n\t" : "=r" (regX19) );
   asm ("mov %0, x20\n\t" : "=r" (regX20) );
   asm ("mov %0, x21\n\t" : "=r" (regX21) );
   asm ("mov %0, x22\n\t" : "=r" (regX22) );
   asm ("mov %0, x23\n\t" : "=r" (regX23) );
   asm ("mov %0, x24\n\t" : "=r" (regX24) );
   asm ("mov %0, x25\n\t" : "=r" (regX25) );
   asm ("mov %0, x26\n\t" : "=r" (regX26) );
   asm ("mov %0, x27\n\t" : "=r" (regX27) );
   asm ("mov %0, x28\n\t" : "=r" (regX28) );
   outBuffer.x19 = regX19;
   outBuffer.x20 = regX20;
   outBuffer.x21 = regX21;
   outBuffer.x22 = regX22;
   outBuffer.x23 = regX23;
   outBuffer.x24 = regX24;
   outBuffer.x25 = regX25;
   outBuffer.x26 = regX26;
   outBuffer.x27 = regX27;
   outBuffer.x28 = regX28;
}

} // end namespace

#else // }  {

namespace hx {

// Put this function here so we can be reasonablly sure that "this" register and
// the 4 registers that may be used to pass args are on the stack.
int RegisterCapture::Capture(int *inTopOfStack,int **inBuf,int &outSize,int inMaxSize, int *inBottom)
{
	int size = ( (char *)inTopOfStack - (char *)inBottom )/sizeof(void *);
	if (size>inMaxSize)
		size = inMaxSize;
	outSize = size;
	if (size>0)
	   memcpy(inBuf,inBottom,size*sizeof(void*));
	return 1;
}


RegisterCapture *gRegisterCaptureInstance = 0;
RegisterCapture *RegisterCapture::Instance()
{
	if (!gRegisterCaptureInstance)
		gRegisterCaptureInstance = new RegisterCapture();
	return gRegisterCaptureInstance;
}

} // end namespace hx

#endif // }
