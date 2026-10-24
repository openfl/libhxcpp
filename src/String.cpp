#include <hxcpp.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <set>
#include <string>
#include "hx/Hash.h"
#include <hx/Thread.h>
#include <locale>

using namespace hx;

#if defined(BLACKBERRY)
using namespace std;
#endif

// -------- String ----------------------------------------


#ifdef HX_WINDOWS

#ifndef _MSC_VER
#define _MSC_VER 1
#endif

// vc 7...
#if _MSC_VER < 1400

#define SPRINTF _snprintf

#else // vc8+

#define SPRINTF _snprintf_s

#endif

#else // not windows ..

#define SPRINTF snprintf

#endif

#ifdef HX_SMART_STRINGS
#include "hx/Unicase.h"
#endif

// ---- Locale-independent numeric conversion --------------------------------
// The process locale is global mutable state that host frameworks (GUI
// toolkits, audio libraries, JNI containers) change after boot.  A
// comma-decimal locale flips strtod/printf's separator, breaking float
// parsing and printing.  Parse and print through an explicit "C" numeric
// locale where the platform provides one.
#ifndef HX_WINDOWS
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

#if defined(HX_WINDOWS) && !defined(HX_WINRT)

static _locale_t hxCNumericLocale()
{
   static _locale_t cLoc = _create_locale(LC_NUMERIC, "C");
   return cLoc;
}
static double hxStrtodC(const char *inStr, char **outEnd)
{
   return _strtod_l(inStr, outEnd, hxCNumericLocale());
}
struct HxCNumericScope { };
#define HX_SNPRINTF_C(buf,len,fmt,...) _snprintf_l(buf,len,fmt,hxCNumericLocale(),__VA_ARGS__)

#elif defined(__APPLE__) || defined(__GLIBC__) || (defined(__ANDROID_API__) && __ANDROID_API__>=21)

static locale_t hxCNumericLocale()
{
   static locale_t cLoc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
   return cLoc;
}
static double hxStrtodC(const char *inStr, char **outEnd)
{
   locale_t old = uselocale(hxCNumericLocale());
   double result = strtod(inStr, outEnd);
   uselocale(old);
   return result;
}
// Switches this thread to the "C" numeric locale for the printf calls in
// its scope
struct HxCNumericScope
{
   locale_t old;
   HxCNumericScope() { old = uselocale(hxCNumericLocale()); }
   ~HxCNumericScope() { uselocale(old); }
};
#define HX_SNPRINTF_C SPRINTF

#else

static double hxStrtodC(const char *inStr, char **outEnd)
{
   return strtod(inStr, outEnd);
}
struct HxCNumericScope { };
#define HX_SNPRINTF_C SPRINTF

#endif

// Locale-independent double parse for other runtime modules (cppia
// constant loading etc)
double _hx_strtod_c_locale(const char *inStr)
{
   return hxStrtodC(inStr, 0);
}

namespace hx
{
char HX_DOUBLE_PATTERN[20] = "%.15g";
bool HX_DOUBLE_PATTERN_CUSTOM = false;
#define HX_INT_PATTERN "%d"
#define HX_UINT_PATTERN "%ud"
}

// The pattern is given to snprintf with one double, so it may hold exactly one
// floating-point conversion, %[flags][width][.precision] then e, f, g or a in
// either case, and no other conversion that would read an argument
static bool isFloatFormat(const char *inFormat)
{
   int conversions = 0;
   for(const char *p = inFormat; *p; p++)
   {
      if (*p!='%')
         continue;
      p++;
      if (*p=='%')
         continue;
      while(*p && strchr("-+ #0",*p))
         p++;
      while(*p>='0' && *p<='9')
         p++;
      if (*p=='.')
      {
         p++;
         while(*p>='0' && *p<='9')
            p++;
      }
      if (!*p || !strchr("eEfFgGaA",*p))
         return false;
      conversions++;
   }
   return conversions==1;
}

void __hxcpp_set_float_format(String inFormat)
{
  char pattern[20];
  int last = inFormat.length < 19 ? inFormat.length : 19;
  memcpy(pattern, inFormat.utf8_str(), last*sizeof(char) );
  pattern[last] = '\0';
  if (!isFloatFormat(pattern))
     hx::Throw( HX_CSTRING("set_float_format: the format must hold one floating-point conversion, such as %.6g, and no other") );
  memcpy(HX_DOUBLE_PATTERN, pattern, sizeof(pattern));
  hx::HX_DOUBLE_PATTERN_CUSTOM = true;
}

// --- GC helper

hx::Class __StringClass;

String  String::emptyString = HX_("",00,00,00,00);
static Dynamic sConstEmptyString;
static String  sConstStrings[256];
static Dynamic sConstDynamicStrings[256];
static String *sCharToString[1088] = { 0 };


typedef Hash<TNonGcStringSet> StringSet;
static StringSet *sPermanentStringSet = 0;
static volatile int sPermanentStringSetMutex = 0;
static volatile int sCharToStringMutex = 0;

#ifdef HXCPP_COMBINE_STRINGS
static bool sIsIdent[256];
static bool InitIdent()
{
   for(int i=0;i<256;i++)
      sIsIdent[i]= (i>='a' && i<='z') || (i>='A' && i<='Z') || (i>='0' && i<='9') || (i=='_');
   return true;
}
#endif

inline static bool IsUtf16Surrogate(int ch)
{
   return ch>=0xd800 && ch<0xe000;
}
inline static bool IsUtf16LowSurrogate(int ch)
{
   return ch>=0xdc00 && ch<0xe000;
}
inline static bool IsUtf16HighSurrogate(int ch)
{
   return ch>=0xd800 && ch<0xdc00;
}

static int UTF8Bytes(int c)
{
      if( c <= 0x7F )
         return 1;
      else if( c <= 0x7FF )
         return 2;
      else if( c <= 0xFFFF )
         return 3;
      else
         return 4;
}

inline static int UTF16BytesCheck(int ch)
{
   if (ch>=0x10000)
   {
      if (ch<0x110000)
          return 2;
   }
   return 1;
}

static void UTF8EncodeAdvance(char * &ioPtr,int c)
{
      if( c <= 0x7F )
         *ioPtr++ = (c);
      else if( c <= 0x7FF )
      {
         *ioPtr++ = ( 0xC0 | (c >> 6) );
         *ioPtr++ = ( 0x80 | (c & 63) );
      }
      else if( c <= 0xFFFF )
      {
         *ioPtr++  = ( 0xE0 | (c >> 12) );
         *ioPtr++  = ( 0x80 | ((c >> 6) & 63) );
         *ioPtr++  = ( 0x80 | (c & 63) );
      }
      else
      {
         *ioPtr++  = ( 0xF0 | (c >> 18) );
         *ioPtr++  = ( 0x80 | ((c >> 12) & 63) );
         *ioPtr++  = ( 0x80 | ((c >> 6) & 63) );
         *ioPtr++  = ( 0x80 | (c & 63) );
      }
}

#ifndef HX_SMART_STRINGS
static unsigned char *sUtf8LenArray = 0;

static const unsigned char *getUtf8LenArray()
{
   if (!sUtf8LenArray)
   {
      sUtf8LenArray = (unsigned char *)malloc(256);
      for(int i=0;i<256;i++)
         sUtf8LenArray[i] =  i< 0x80 ? 1 : i<0xe0 ? 2 : i<0xf0 ? 3 : 4;
   }
   return sUtf8LenArray;
}
#endif

// Each maximal invalid subsequence (stray or missing continuation bytes,
// overlong forms, surrogates, values above U+10FFFF) becomes one U+FFFD.
// Without an end, decoding stops at the NUL terminator, which is never a
// continuation byte.
static inline int DecodeAdvanceUTF8(const unsigned char * &ioPtr,const unsigned char *end)
{
   int lead = *ioPtr++;
   if( lead < 0x80 )
      return lead;

   int code;
   int more;
   int lo = 0x80;
   int hi = 0xBF;
   if( lead >= 0xC2 && lead <= 0xDF )
   {
      code = lead & 0x1F;
      more = 1;
   }
   else if( lead >= 0xE0 && lead <= 0xEF )
   {
      code = lead & 0x0F;
      more = 2;
      if (lead==0xE0) lo = 0xA0;
      else if (lead==0xED) hi = 0x9F;
   }
   else if( lead >= 0xF0 && lead <= 0xF4 )
   {
      code = lead & 0x07;
      more = 3;
      if (lead==0xF0) lo = 0x90;
      else if (lead==0xF4) hi = 0x8F;
   }
   else
      return 0xFFFD;

   while(more--)
   {
      if (end && ioPtr>=end)
         return 0xFFFD;
      int next = *ioPtr;
      if (next<lo || next>hi)
         return 0xFFFD;
      ioPtr++;
      code = (code<<6) | (next & 0x3F);
      lo = 0x80;
      hi = 0xBF;
   }
   return code;
}

static inline int DecodeAdvanceUTF8(const unsigned char * &ioPtr)
{
   return DecodeAdvanceUTF8(ioPtr, nullptr);
}


int _hx_utf8_decode_advance(char *&ioPtr)
{
   return DecodeAdvanceUTF8( (const unsigned char * &) ioPtr );
}


inline int Char16Advance(const char16_t *&ioStr,bool throwOnErr=true)
{
   int ch = *ioStr++;
   if (IsUtf16Surrogate(ch))
   {
      if (IsUtf16LowSurrogate(ch))
      {
         if (throwOnErr)
            hx::Throw(HX_CSTRING("Invalid UTF16"));
         return 0xFFFD;
      }

      // Only a low surrogate may complete the pair.  Peek before consuming -
      // swallowing an ordinary char here would drop it, and swallowing the
      // NUL terminator would send the callers' scan loops off the end of
      // the buffer.
      int peek = *ioStr;
      if (!IsUtf16LowSurrogate(peek))
      {
         if (throwOnErr)
            hx::Throw(HX_CSTRING("Invalid UTF16"));
         return 0xFFFD;
      }
      ioStr++;

      ch = 0x10000 + ( ((ch-0xd800) << 10) | (peek-0xdc00) );
   }
   return ch;
}


void Char16AdvanceSet(char16_t *&ioStr,int inChar)
{
   if (inChar>=0x10000)
   {
      int over = (inChar-0x10000);
      if (over>=0x100000)
      {
         *ioStr++ = 0xfffd;
      }
      else
      {
         *ioStr++ = (over>>10) + 0xd800;
         *ioStr++ = (over&0x3ff) + 0xdc00;
      }
   }
   else if (IsUtf16Surrogate(inChar))
   {
      *ioStr++ = 0xfffd;
   }
   else
      *ioStr++ = inChar;
}


template<typename T>
char *TConvertToUTF8(const T *inStr, int *ioLen, hx::IStringAlloc *inBuffer,bool)
{
   int len = 0;
   int chars = 0;
   if (ioLen==0 || *ioLen==0)
   {
      while(inStr[len])
         chars += UTF8Bytes(inStr[len++]);
   }
   else
   {
      len = *ioLen;
      for(int i=0;i<len;i++)
         chars += UTF8Bytes(inStr[i]);
   }

   char *buf = inBuffer ? (char *)inBuffer->allocBytes(chars+1) :
                          (char *)NewGCPrivate(0,chars+1);
   char *ptr = buf;
   for(int i=0;i<len;i++)
       UTF8EncodeAdvance(ptr,inStr[i]);
   *ptr = 0;
   if (ioLen)
      *ioLen = chars;

   return buf;
}


template<>
char *TConvertToUTF8(const char16_t *inStr, int *ioLen, hx::IStringAlloc *inBuffer,bool throwInvalid)
{
   int len = 0;
   if (ioLen==0 || *ioLen==0)
   {
      const char16_t *s = inStr;
      while( Char16Advance(s,throwInvalid) ) { }
      len = s - inStr - 1;
   }
   else
   {
      len = *ioLen;
   }
   const char16_t *s = inStr;
   const char16_t *end = s + len;
   int chars = 0;
   while(s<end)
      chars += UTF8Bytes( Char16Advance( s,throwInvalid ) );

   char *buf = inBuffer ? (char *)inBuffer->allocBytes(chars+1) :
                          (char *)NewGCPrivate(0,chars+1);
   char *ptr = buf;
   s = inStr;
   while(s<end)
      UTF8EncodeAdvance(ptr,Char16Advance(s,throwInvalid));

   *ptr = 0;
   if (ioLen)
      *ioLen = chars;

   return buf;
}

char16_t *String::allocChar16Ptr(int len)
{
   // See NewString - reserve a memoized-hash slot for longer strings
   bool hashSlot = len >= HX_GC_STRING_HASH_SLOT_MIN_LEN;
   char16_t *result = (char16_t *)hx::InternalNew( (len+1)*2 + (hashSlot ? 4 : 0), false );
   ((unsigned int *)result)[-1] |= HX_GC_STRING_CHAR16_T | (hashSlot ? HX_GC_STRING_HASH_SLOT : 0);
   result[len] = 0;
   return result;
}


template<typename T>
static const char *GCStringDup(const T *inStr,int inLen, int *outLen=0)
{
   if (inStr==0 && inLen<=0)
   {
      if (outLen)
         outLen = 0;
      return String::emptyString.raw_ptr();
   }

   int len = inLen;
   bool allAscii = true;
   if (len==-1)
   {
      len=0;
      while(inStr[len])
      {
         if (sizeof(T)>1 && allAscii)
            allAscii = inStr[len] <= 127;
         len++;
      }
   }
   else if (sizeof(T)>1)
   {
      for(int i=0;i<inLen;i++)
         if (inStr[i]>127)
         {
            allAscii = false;
            break;
         }
   }

   if (outLen)
      *outLen = len;

   if (len==1)
      return String::fromCharCode(inStr[0]).raw_ptr();

   #ifdef HXCPP_COMBINE_STRINGS
   bool ident = len<20 && sIsIdent[inStr[0]] && (inStr[0]<'0' || inStr[0]>'9');
   if (ident && len>3)
      for(int p=1; p<len;p++)
         if (!sIsIdent[ inStr[p] ])
         {
            ident = false;
            break;
         }

   if (ident)
   {
      hx::StackContext *ctx = hx::StackContext::getCurrent();
      if (!ctx->stringSet)
         ctx->stringSet = new WeakStringSet();

      unsigned int hash = 0;
      for(int i=0;i<len;i++)
         hash = hash*223 + ((unsigned char *)inStr)[i];

      struct Finder
      {
         int len;
         const char *ptr;
         Finder(int len, const char *inPtr) : len(len), ptr(inPtr) { }
         bool operator==(const String &inStr) const
         {
            return len == inStr.length && !memcmp(ptr, inStr.__s, len*sizeof(char));
         }
      };
      String found;
      if (ctx->stringSet->findEquivalentKey(found, hash, Finder(len, inStr)))
         return found.__s;

      char *result = hx::NewString(len + 4);
      memcpy(result,inStr,sizeof(char)*(len));
      result[len] = '\0';
      *((unsigned int *)(result + len + 1)) = hash;
      ((unsigned int *)(result))[-1] |= HX_GC_STRING_HASH;

      String asString(result, len);
      ctx->stringSet->TSet( asString, true );

      return result;
   }
   #endif

   if (sizeof(T)==1 || allAscii)
   {
      char *result = hx::NewString( len );
      if (sizeof(T)==1)
         memcpy(result,inStr,sizeof(char)*(len));
      else
         for(int i=0;i<len;i++)
            result[i] = inStr[i];
      return result;
   }


   char16_t *result = String::allocChar16Ptr(len);
   memcpy(result,inStr,2*len);
   return (const char *)result;
}




template<typename T>
inline String TCopyString(const T *inString,int inLength)
{
   if (!inString)
      return String();

   #ifndef HX_SMART_STRINGS
      if (sizeof(T)==1)
      {
         int len = 0;
         const char *res = GCStringDup((const char *)inString,inLength,&len);
         return String(res,len);
      }
      else
      {
         if (inLength == 0) {
            return String::emptyString;
         }
         int length = inLength > 0 ? inLength : 0;
         const char *ptr = TConvertToUTF8(inString, &length, 0, true );
         return String(ptr,length);
      }
   #else
      int c16len=0;
      bool hasWChar = false;
      const T *end = inLength>=0 ? inString + inLength : 0;
      if (end)
      {
         for(const T *s = inString; s<end; s++)
         {
            unsigned int c = *s;
            if (c>127)
               hasWChar = true;
            c16len += UTF16BytesCheck(c);
         }
      }
      else
         for(const T *s = inString; *s; s++)
         {
            unsigned int c = *s;
            if (c>127)
               hasWChar = true;
            c16len += UTF16BytesCheck(c);
         }

      if (hasWChar)
      {
         char16_t *result = String::allocChar16Ptr(c16len);
         c16len = 0;
         for(const T *s = inString; ; s++)
         {
            if (end)
            {
               if (s>=end) break;
            }
            else
            {
               if (!*s) break;
            }
            unsigned int c = *s;
            if (c<0x10000)
               result[c16len++] = c;
            else if (c>=0x110000)
               result[c16len++] = 0xFFFD;
            else
            {
               int over = (c-0x10000);
               result[c16len++] = (over>>10) + 0xd800;
               result[c16len++] = (over&0x3ff) + 0xdc00;
            }
         }
         return String(result,c16len);
      }
      else
      {
         char *s = hx::NewString(c16len);
         for(int i=0;i<c16len;i++)
            s[i] = (char)inString[i];
         return String(s,c16len);
      }
   #endif
}


static inline int hxValidCodePoint(int c)
{
   return c<0 || c>0x10FFFF || IsUtf16Surrogate(c) ? 0xFFFD : c;
}

String __hxcpp_char_array_to_utf8_string(Array<int> &inChars,int inFirst, int inLen)
{
   int len = inChars->length;
   if (inFirst<0)
     inFirst = 0;
   if (inLen<0) inLen = len;
   if (inFirst+inLen>len)
      inLen = len-inFirst;
   if (inLen<=0)
      return String::emptyString;

   int *base = &inChars[0];
   #ifdef HX_SMART_STRINGS
   bool hasBig = false;
   for(int i=0;i<inLen;i++)
      if (base[i+inFirst]>127 || base[i+inFirst]<0)
      {
         hasBig = true;
         break;
      }

   if (hasBig)
   {
      int units = 0;
      for(int i=0;i<inLen;i++)
         units += UTF16BytesCheck(base[i+inFirst]);
      char16_t *ptr = String::allocChar16Ptr(units);
      char16_t *p = ptr;
      for(int i=0;i<inLen;i++)
         Char16AdvanceSet(p, hxValidCodePoint(base[i+inFirst]));
      return String(ptr, units);
   }
   #endif
   int bytes = 0;
   for(int i=0;i<inLen;i++)
      bytes += UTF8Bytes(hxValidCodePoint(base[i+inFirst]));
   char *result = hx::NewString(bytes);
   char *p = result;
   for(int i=0;i<inLen;i++)
      UTF8EncodeAdvance(p, hxValidCodePoint(base[i+inFirst]));
   return String(result,bytes);
}

Array<int> __hxcpp_utf8_string_to_char_array(String &inString)
{
   #ifdef HX_SMART_STRINGS
   Array<int> result = Array_obj<int>::__new(inString.length);
   if (inString.isUTF16Encoded())
   {
      const char16_t *ptr = inString.wc_str();
      for(int i=0;i<inString.length;i++)
         result[i] = ptr[i];
   }
   else
   {
      const char *ptr = inString.raw_ptr();
      for(int i=0;i<inString.length;i++)
         result[i] = ptr[i];
   }
   #else
   Array<int> result = Array_obj<int>::__new(0,inString.length);

   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;
   while(src<end)
       result->push(DecodeAdvanceUTF8(src));

   if (src!=end)
      hx::Throw(HX_CSTRING("Invalid UTF8"));
   #endif

   return result;
}


String __hxcpp_char_bytes_to_utf8_string(String &inBytes)
{
   #ifdef HX_SMART_STRINGS
   // This does not really make much sense
   return inBytes;
   #else
   int len = inBytes.length;
   char *result = TConvertToUTF8((unsigned char *)inBytes.__s,&len,0,true);
   return String(result,len);
   #endif
}


String __hxcpp_utf8_string_to_char_bytes(String &inUTF8)
{
   #ifdef HX_SMART_STRINGS
   // This does not really make much sense
   return inUTF8;
   #else
    const unsigned char *src = (unsigned char *)inUTF8.__s;
    const unsigned char *end = src + inUTF8.length;
    int char_count = 0;
    while(src<end)
    {
        int c = DecodeAdvanceUTF8(src,end);
        char_count++;
        if( c == 8364 ) // euro symbol
           c = 164;
        else if ( c == 0xFEFF ) // BOM
        {
           char_count--;
        }
        else if( c > 255 )
            hx::Throw(HX_CSTRING("Utf8::decode invalid character"));
    }

    if (src!=end)
       hx::Throw(HX_CSTRING("Invalid UTF8"));

    char *result = hx::NewString(char_count);

    src = (unsigned char *)inUTF8.__s;
    char_count = 0;
    while(src<end)
    {
        int c = DecodeAdvanceUTF8(src);
        if( c == 8364 ) // euro symbol
           c = 164;
        if ( c != 0xFEFF ) // BOM
           result[char_count++] = c;
    }

   result[char_count] = '\0';
   return String(result,char_count);
   #endif
}


void _hx_utf8_iter(String inString, Dynamic inIter)
{
   #ifdef HX_SMART_STRINGS
   if (inString.isUTF16Encoded())
      for(int i=0;i<inString.length;i++)
         inIter( (int)inString.raw_wptr()[i] );
   else
      for(int i=0;i<inString.length;i++)
         inIter( (int)inString.raw_ptr()[i] );
   #else
   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;

   while(src<end)
      inIter(DecodeAdvanceUTF8(src,end+1));

   if (src>end)
      hx::Throw(HX_CSTRING("Invalid UTF8"));
   #endif
}

int _hx_utf8_char_code_at(String inString, int inIndex)
{
   if (inIndex<0)
      return 0;
   #ifdef HX_SMART_STRINGS
   if (!inString.raw_ptr() || inIndex>=inString.length)
      return 0;
   if (inString.isUTF16Encoded())
      return inString.raw_wptr()[inIndex];
   else
      return inString.raw_ptr()[inIndex];
   #else
   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;
   const unsigned char *sLen = getUtf8LenArray();

   for(int i=0;i<inIndex;i++)
   {
      src += sLen[*src];
      if (src==end)
         return 0;
      if (src>end)
         hx::Throw(HX_CSTRING("Invalid UTF8"));
   }
   return DecodeAdvanceUTF8(src,end);
   #endif
}

int _hx_utf8_length(String inString)
{
   #ifdef HX_SMART_STRINGS
   return inString.length;
   #else

   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;

   int len = 0;
   const unsigned char *sLen = getUtf8LenArray();
   while(src<end)
   {
      src += sLen[*src];
      len++;
   }
   if (src>end)
      hx::Throw(HX_CSTRING("Invalid UTF8"));
   return len;
   #endif
}

bool _hx_utf8_is_valid(String inString)
{
   #ifdef HX_SMART_STRINGS
   return true;
   #else
   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;
   const unsigned char *sLen = getUtf8LenArray();
   while(src<end)
      src += sLen[*src];

   return src==end;
   #endif
}

String _hx_utf8_sub(String inString, int inStart, int inLen)
{
   #ifdef HX_SMART_STRINGS
   return inString.substr(inStart,inLen);
   #else
   const unsigned char *src = (const unsigned char *)inString.__s;
   const unsigned char *end = src + inString.length;

   const unsigned char *sLen = getUtf8LenArray();
   for(int i=0;i<inStart;i++)
   {
      src += sLen[*src];

      if (src==end)
         return String::emptyString;
      if (src>end)
         hx::Throw(HX_CSTRING("Invalid UTF8"));
   }
   const unsigned char *start = src;
   for(int i=0;i<inLen;i++)
   {
      src += sLen[*src];
      if (src==end)
         break;
      if (src>end)
         hx::Throw(HX_CSTRING("Invalid UTF8"));
   }
   return String::create((const char *)start, src-start);
   #endif
}





String String::create(const wchar_t *inString,int inLength) { return TCopyString(inString,inLength); }
String String::create(const char16_t *inString,int inLength) { return TCopyString(inString,inLength); }
String String::create(const char *inString,int inLength)
{
   if (!inString)
      return String();

   #ifdef HX_SMART_STRINGS
   if (inLength<0)
      for(inLength=0; inString[inLength]; inLength++) { }

   const unsigned char *c = (const unsigned char *)inString;
   for(int i=0;i<inLength;i++)
   {
      if (c[i]>127)
         return _hx_utf8_to_utf16(c, inLength,false);
   }

   #endif

   int len = 0;
   const char *s = GCStringDup(inString,inLength,&len);
   return String(s,len);
}

#if (HXCPP_API_LEVEL>=500)

String String::create(const::cpp::marshal::View<char>& buffer)
{
    auto start = buffer.ptr.ptr;
    auto end   = start + buffer.length;

    while (start < end) {
        if (*start == false)
        {
            break;
        }

        start++;
    }

    return String::create(buffer.ptr.ptr, buffer.length - (end - start));
}

String String::create(const cpp::marshal::View<char16_t>& buffer)
{
    auto start = reinterpret_cast<const char16_t*>(buffer.ptr.ptr);
    auto end   = start + buffer.length;
    auto extra = 0;

    while (start < end) {
        if (Char16Advance(start) == false)
        {
            // set extra to 1 so we don't include the null terminating character in the calculated length.
            extra = 1;

            break;
        }
    }

    return String::create(buffer.ptr.ptr, buffer.length - (end - start) - extra);
}

#endif

String::String(const Dynamic &inRHS)
{
   if (inRHS.GetPtr())
      (*this)=const_cast<Dynamic &>(inRHS)->toString();
   else
   {
      __s = 0;
      length = 0;
   }
}

void String::fromInt(int inIdx)
{
   // Write digits directly instead of going through snprintf("%d"), which pays
   // format-string parsing on every call. Int->String is extremely common
   // (logging, interpolation), so this is a worthwhile hot-path win.
   char buf[12]; // "-2147483648" is 11 chars
   char *p = buf + sizeof(buf);
   unsigned int u = (unsigned int)inIdx;
   if (inIdx < 0)
      u = (unsigned int)0 - u; // magnitude, well-defined even for INT_MIN
   do {
      *--p = (char)('0' + (u % 10));
      u /= 10;
   } while (u);
   if (inIdx < 0)
      *--p = '-';
   __s = GCStringDup(p, (int)(buf + sizeof(buf) - p), &length);
}

String::String(const int &inRHS)
{
   fromInt(inRHS);
}


String::String(const unsigned int &inRHS)
{
   // Direct digit writing; also fixes the old HX_UINT_PATTERN "%ud", which
   // appended a literal 'd' (e.g. "42d") for any raw unsigned int formatted here.
   char buf[12]; // 10-digit max for unsigned 32-bit
   char *p = buf + sizeof(buf);
   unsigned int u = inRHS;
   do {
      *--p = (char)('0' + (u % 10));
      u /= 10;
   } while (u);
   __s = GCStringDup(p, (int)(buf + sizeof(buf) - p), &length);
}


String::String(const cpp::CppInt32__ &inRHS)
{
   fromInt(inRHS.mValue);
}



// Non-finite doubles must stringify the same as other Haxe targets (JS, hl,
// jvm...): "NaN", "Infinity", "-Infinity". printf is platform-specific here
// (MSVC emits "-nan(ind)"/"inf", glibc "nan"/"inf"), so handle them explicitly.
static const char *_hx_non_finite_str(double v)
{
   if (v != v) return "NaN";
   if (v == HUGE_VAL) return "Infinity";
   if (v == -HUGE_VAL) return "-Infinity";
   return 0;
}

// Rounds inValue to inCount significant digits and returns what they read
// back as. outExp is the decimal exponent of the first digit
static double hxRoundDigits(double inValue, int inCount, char *outDigits, int &outExp)
{
   char buf[40];
   HX_SNPRINTF_C(buf,40,"%.*e",inCount-1,inValue);
   const char *p = buf;
   for(int n=0; *p!='e'; p++)
      if (*p>='0' && *p<='9')
         outDigits[n++] = *p;
   outExp = atoi(p+1);
   return hxStrtodC(buf,0);
}

static char *hxWriteExponent(char *out, int inExp)
{
   *out++ = 'e';
   *out++ = inExp<0 ? '-' : '+';
   int mag = inExp<0 ? -inExp : inExp;
   if (mag>=100)
      *out++ = '0' + mag/100;
   if (mag>=10)
      *out++ = '0' + mag/10%10;
   *out++ = '0' + mag%10;
   return out;
}

static double hxReadDigits(const char *inDigits, int inCount, int inExp)
{
   char buf[40];
   memcpy(buf,inDigits,inCount);
   *hxWriteExponent(buf+inCount,inExp-inCount+1) = '\0';
   return hxStrtodC(buf,0);
}

static int hxTrimZeros(const char *inDigits, int inCount)
{
   while(inCount>1 && inDigits[inCount-1]=='0')
      inCount--;
   return inCount;
}

// The fewest significant digits that read back as inValue, which is finite
// and positive, choosing the nearest such digits, like std::to_chars
static int hxShortestDigits(double inValue, char *outDigits, int &outExp)
{
   int binExp;
   bool powerOfTwo = inValue>DBL_MIN && frexp(inValue,&binExp)==0.5;
   if (inValue>=DBL_MIN && !powerOfTwo)
   {
      // 15 digits tell normal doubles apart, so if the nearest 15 read back,
      // they are the shortest digits padded with zeros
      for(int count=15; count<17; count++)
         if (hxRoundDigits(inValue,count,outDigits,outExp)==inValue)
            return hxTrimZeros(outDigits,count);
   }
   else
   {
      // Denormals can need fewer than 15 digits, and the doubles either side
      // of a power of two are not equally far from it, so the nearest digits
      // can miss when the next ones up still read back
      for(int count=1; count<17; count++)
      {
         double back = hxRoundDigits(inValue,count,outDigits,outExp);
         if (back==inValue)
            return hxTrimZeros(outDigits,count);
         if (powerOfTwo && back<inValue)
         {
            int i = count-1;
            while(i>=0 && outDigits[i]=='9')
               outDigits[i--] = '0';
            if (i<0)
            {
               outDigits[0] = '1';
               outExp++;
            }
            else
               outDigits[i]++;
            if (hxReadDigits(outDigits,count,outExp)==inValue)
               return hxTrimZeros(outDigits,count);
         }
      }
   }
   hxRoundDigits(inValue,17,outDigits,outExp);
   return hxTrimZeros(outDigits,17);
}

String::String(const double &inRHS)
{
   const char *nonFinite = _hx_non_finite_str(inRHS);
   if (nonFinite)
   {
      __s = GCStringDup(nonFinite,-1,&length);
      return;
   }
   char buf[100];
   HxCNumericScope cNumeric;
   if (HX_DOUBLE_PATTERN_CUSTOM)
   {
      HX_SNPRINTF_C(buf,100,HX_DOUBLE_PATTERN,inRHS);
   }
   else
   {
      // The shortest digits that read back, laid out like "%.15g", or "%.16g"
      // and "%.17g" when more digits are needed, with the exponent trimmed to
      // its significant digits ("1e-7") as on other Haxe targets
      char *out = buf;
      if (signbit(inRHS))
         *out++ = '-';
      double value = fabs(inRHS);
      char digits[17];
      int exp = 0;
      int count = 1;
      if (value==0)
         digits[0] = '0';
      else
         count = hxShortestDigits(value,digits,exp);
      int precision = count>15 ? count : 15;
      if (exp<-4 || exp>=precision)
      {
         *out++ = digits[0];
         if (count>1)
         {
            *out++ = '.';
            memcpy(out,digits+1,count-1);
            out += count-1;
         }
         out = hxWriteExponent(out,exp);
      }
      else if (exp>=0)
      {
         for(int i=0; i<=exp; i++)
            *out++ = i<count ? digits[i] : '0';
         if (count>exp+1)
         {
            *out++ = '.';
            memcpy(out,digits+exp+1,count-exp-1);
            out += count-exp-1;
         }
      }
      else
      {
         *out++ = '0';
         *out++ = '.';
         for(int i=-1; i>exp; i--)
            *out++ = '0';
         memcpy(out,digits,count);
         out += count;
      }
      *out = '\0';
   }
   buf[99]='\0';
   __s = GCStringDup(buf,-1,&length);
}


String::String(const cpp::Int64 &inRHS)
{
   // Direct digit writing instead of snprintf("%lld"); see String::fromInt.
   char buf[24]; // "-9223372036854775808" is 20 chars
   char *p = buf + sizeof(buf);
   unsigned long long u = (unsigned long long)(long long)inRHS;
   bool neg = (long long)inRHS < 0;
   if (neg)
      u = (unsigned long long)0 - u; // magnitude, well-defined even for INT64_MIN
   do {
      *--p = (char)('0' + (u % 10));
      u /= 10;
   } while (u);
   if (neg)
      *--p = '-';
   __s = GCStringDup(p, (int)(buf + sizeof(buf) - p), &length);
}


String::String(const cpp::UInt64 &inRHS)
{
   char buf[24]; // 20-digit max for unsigned 64-bit
   char *p = buf + sizeof(buf);
   unsigned long long u = (unsigned long long)inRHS;
   do {
      *--p = (char)('0' + (u % 10));
      u /= 10;
   } while (u);
   __s = GCStringDup(p, (int)(buf + sizeof(buf) - p), &length);
}

String::String(const float &inRHS)
{
   const char *nonFinite = _hx_non_finite_str(inRHS);
   if (nonFinite)
   {
      __s = GCStringDup(nonFinite,-1,&length);
      return;
   }
   char buf[100];
   HxCNumericScope cNumeric;
   HX_SNPRINTF_C(buf,100,HX_DOUBLE_PATTERN,inRHS);
   buf[99]='\0';
   __s = GCStringDup(buf,-1,&length);
}

String::String(const bool &inRHS)
{
   if (inRHS)
   {
      *this = HX_CSTRING("true");
   }
   else
   {
      *this = HX_CSTRING("false");
   }
}

void String::fromPointer(const void *p)
{
   char buf[128];
   SPRINTF(buf,128,"Native(%p)",p);
   __s = GCStringDup(buf,-1,&length);
}

#ifdef HX_SMART_STRINGS
#define ADD_HASH(X) \
    result = result*223 + (int)(X)
#endif

#ifdef HX_SMART_STRINGS
// Hash the utf8 encoding of the DECODED code points.  Hashing raw utf16
// units encodes a surrogate pair as six virtual bytes where the real utf8
// (and therefore the compile-time literal hashes and the narrow-string byte
// hashes) has four - making equal strings hash differently.  Lone
// surrogates hash as U+FFFD, matching the conversion routines.
static unsigned int HashUtf16Range(const char16_t *ptr, const char16_t *end)
{
   unsigned int result = 0;
   while(ptr<end)
   {
      int c = *ptr++;
      if (IsUtf16HighSurrogate(c) && ptr<end && IsUtf16LowSurrogate(*ptr))
         c = 0x10000 + ( ((c-0xd800)<<10) | ((*ptr++) - 0xdc00) );
      else if (IsUtf16Surrogate(c))
         c = 0xFFFD;

      if( c <= 0x7F )
      {
         ADD_HASH(c);
      }
      else if( c <= 0x7FF )
      {
         ADD_HASH(0xC0 | (c >> 6));
         ADD_HASH(0x80 | (c & 63));
      }
      else if( c <= 0xFFFF )
      {
         ADD_HASH(0xE0 | (c >> 12));
         ADD_HASH(0x80 | ((c >> 6) & 63));
         ADD_HASH(0x80 | (c & 63));
      }
      else
      {
         ADD_HASH(0xF0 | (c >> 18));
         ADD_HASH(0x80 | ((c >> 12) & 63));
         ADD_HASH(0x80 | ((c >> 6) & 63) );
         ADD_HASH(0x80 | (c & 63) );
      }
   }
   return result;
}
#endif

unsigned int String::calcSubHash(int start, int inLen) const
{
   unsigned int result = 0;
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      return HashUtf16Range(__w + start, __w + start + inLen);
   }
   else
   #endif
   {
      const unsigned char *s = (const unsigned char *)__s + start;
      for(int i=0;i<inLen;i++)
         result = result*223 + s[i];
   }

   return result;

}

unsigned int String::calcHash() const
{
   unsigned int result = 0;
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      return HashUtf16Range(__w, __w + length);
   }
   else
   #endif
   for(int i=0;i<length;i++)
      result = result*223 + ((unsigned char *)__s)[i];

   return result;
}



// InternalCreateConstBuffer is not uft16 aware whenit come to hashes
static void fixHashPerm16(const String &str)
{
   unsigned int hash = str.calcHash();
   ((unsigned int *)str.raw_ptr())[-2] = hash;
}


static unsigned char safeChars[256];

String String::__URLEncode() const
{
   Array<unsigned char> bytes(0,length);
   // utf8-encode
   __hxcpp_bytes_of_string(bytes,*this);

   int extra = 0;
   int utf8_chars = bytes->__length();
   for(int i=0;i<utf8_chars;i++)
      if (!safeChars[bytes[i]])
         extra++;
   if (extra==0)
      return *this;

   int l = utf8_chars + extra*2;
   char *result = hx::NewString(l);
   char *ptr = result;

   for(int i=0;i<utf8_chars;i++)
   {
      if (!safeChars[bytes[i]])
      {
         static char hex[] = "0123456789ABCDEF";
         unsigned char b = bytes[i];
         *ptr++ = '%';
         *ptr++ = hex[ b>>4 ];
         *ptr++ = hex[ b & 0x0f ];
      }
      else
         *ptr++ = bytes[i];
   }
   return String(result,l);
}

#ifdef HX_SMART_STRINGS
// An ASCII result must be narrow to equal the same text
static String hxNarrowIfAscii(char16_t *inWide, int inLength)
{
   for(int i=0;i<inLength;i++)
      if (inWide[i]>127)
         return String(inWide,inLength);
   char *narrow = hx::NewString(inLength);
   for(int i=0;i<inLength;i++)
      narrow[i] = (char)inWide[i];
   return String(narrow,inLength);
}
#endif

// Case conversion returns the string itself when nothing changes, and
// otherwise copies the unchanged start and converts the rest
String String::toUpperCase() const
{
   if (!__s)
      return String::emptyString;
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      int first = 0;
      while(first<length && unicase_toupper(__w[first])==__w[first])
         first++;
      if (first==length)
         return *this;
      char16_t *result = String::allocChar16Ptr(length);
      memcpy(result, __w, first*sizeof(char16_t));
      for(int i=first;i<length;i++)
      {
         // Surrogates should already may to themselves - no need to check
         result[i] = unicase_toupper( __w[i] );
      }
      return hxNarrowIfAscii(result,length);
   }
   // Byte strings are ASCII in smart-strings mode: a branchless transform is
   // correct and vectorizable, avoiding the per-char locale-dependent toupper.
   int first = 0;
   while(first<length && !(__s[first]>='a' && __s[first]<='z'))
      first++;
   if (first==length)
      return *this;
   char *result = hx::NewString(length);
   memcpy(result, __s, first);
   for(int i=first;i<length;i++)
   {
      char c = __s[i];
      result[i] = (c>='a' && c<='z') ? (char)(c - ('a'-'A')) : c;
   }
   return String(result,length);
   #else
   int first = 0;
   while(first<length && toupper(__s[first])==__s[first])
      first++;
   if (first==length)
      return *this;
   char *result = hx::NewString(length);
   memcpy(result, __s, first);
   for(int i=first;i<length;i++)
      result[i] = toupper( __s[i] );
   return String(result,length);
   #endif
}

String String::toLowerCase() const
{
   if (!__s)
      return String::emptyString;
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      int first = 0;
      while(first<length && unicase_tolower(__w[first])==__w[first])
         first++;
      if (first==length)
         return *this;
      char16_t *result = String::allocChar16Ptr(length);
      memcpy(result, __w, first*sizeof(char16_t));
      for(int i=first;i<length;i++)
         result[i] = unicase_tolower( __w[i] );
      return hxNarrowIfAscii(result,length);
   }
   // Byte strings are ASCII in smart-strings mode: branchless, vectorizable.
   int first = 0;
   while(first<length && !(__s[first]>='A' && __s[first]<='Z'))
      first++;
   if (first==length)
      return *this;
   char *result = hx::NewString(length);
   memcpy(result, __s, first);
   for(int i=first;i<length;i++)
   {
      char c = __s[i];
      result[i] = (c>='A' && c<='Z') ? (char)(c + ('a'-'A')) : c;
   }
   return String(result,length);
   #else
   int first = 0;
   while(first<length && tolower(__s[first])==__s[first])
      first++;
   if (first==length)
      return *this;
   char *result = hx::NewString(length);
   memcpy(result, __s, first);
   for(int i=first;i<length;i++)
      result[i] = tolower( __s[i] );
   return String(result,length);
   #endif
}


static int hex(int inChar)
{
   if (inChar>='0' && inChar<='9')
      return inChar-'0';
   if (inChar>='a' && inChar<='f')
      return inChar-'a' + 10;
   if (inChar>='A' && inChar<='F')
      return inChar-'A' + 10;
   return 0;
}


String String::__URLDecode() const
{
   // Create the decoded string; the decoded form might have fewer than
   // [length] characters, but it won't have more.  If it has fewer than
   // [length], some memory will be wasted here, but on the assumption that
   // most URLs have only a few '%NN' encodings in them, don't bother
   // counting the number of characters in the resulting string first.
   bool hasBig = false;

   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      // Characters above 127 are kept, as UTF-8 of up to 3 bytes per unit
      if (length > 0x7fffffff/3)
         hx::Throw(HX_CSTRING("String too long"));
      char *decoded = NewString(length*3), *d = decoded;
      for (int i = 0; i < length; i++)
      {
         int c = __w[i];
         if (c > 127)
         {
            if (IsUtf16HighSurrogate(c) && i+1<length && IsUtf16LowSurrogate(__w[i+1]))
            {
               c = 0x10000 + ( ((c-0xd800) << 10) | (__w[i+1]-0xdc00) );
               i++;
            }
            else if (IsUtf16Surrogate(c))
               c = 0xFFFD;
            UTF8EncodeAdvance(d, c);
         }
         else if (c == '+')
            *d++ = ' ';
         else if ((c == '%') && (i < (length - 2)))
         {
            *d++ = ((hex(__w[i + 1]) << 4) | (hex(__w[i + 2])));
            i += 2;
         }
         else
             *d++ = c;
      }
      return _hx_utf8_to_utf16((const unsigned char *)decoded, d-decoded,false);
   }
   #endif

   char *decoded = NewString(length), *d = decoded;
   for (int i = 0; i < length; i++)
   {
      int c = (unsigned char)__s[i];
      if (c == '+')
         *d++ = ' ';
      else if ((c == '%') && (i < (length - 2)))
      {
         int ch = ((hex(__s[i + 1]) << 4) | (hex(__s[i + 2])));
         #ifdef HX_SMART_STRINGS
         if (ch>127)
            hasBig = true;
         #endif
         *d++ = ch;
         i += 2;
      }
      else
          *d++ = c;
   }

   #ifdef HX_SMART_STRINGS
   if (hasBig)
      return _hx_utf8_to_utf16((const unsigned char *)decoded, d-decoded,false);
   #endif

   return String( decoded, (d - decoded) );
}

::String &::String::dup()
{
   const char *s = __s;
   __s = 0;
   *this = create(s,length);
   return *this;
}

const ::String &::String::makePermanent() const
{
   if (!__s || (__s[HX_GC_CONST_ALLOC_MARK_OFFSET] & HX_GC_CONST_ALLOC_MARK_BIT) )
   {
      // Already permanent
   }
   else if (!length)
   {
      const_cast<String *>(this)->__s = emptyString.__s;
   }
   else if (length==1)
   {
      const_cast<String *>(this)->__s = String::fromCharCode(cca(0)).__s;
   }
   else
   {
      unsigned int myHash = hash();
      {
         while(_hx_atomic_compare_exchange(&sPermanentStringSetMutex, 0, 1) != 0)
            __hxcpp_gc_safe_point();
         TNonGcStringSet *element = sPermanentStringSet->find(myHash ,  *this);
         _hx_atomic_store(&sPermanentStringSetMutex, 0);
         if (element)
         {
            const_cast<String *>(this)->__s = element->key.__s;
            return *this;
         }
      }

      #ifdef HX_SMART_STRINGS
      if (isUTF16Encoded())
      {
         char16_t *w = (char16_t *)InternalCreateConstBuffer(__s,(length+1)*2,true);
         ((unsigned int *)w)[-1] |= HX_GC_STRING_CHAR16_T;
         const_cast<String *>(this)->__w = w;
         fixHashPerm16(*this);
      }
      else
      #endif
      {
         char *s  = (char *)InternalCreateConstBuffer(__s,length+1,true);
         const_cast<String *>(this)->__s = s;
      }

      while(_hx_atomic_compare_exchange(&sPermanentStringSetMutex, 0, 1) != 0)
         __hxcpp_gc_safe_point();
      sPermanentStringSet->set(*this,null());
      _hx_atomic_store(&sPermanentStringSetMutex, 0);
   }

   return *this;
}


String String::createPermanent(const char *inUtf8, int length)
{
   if (!inUtf8)
      return String();

   if (length<0)
      length = strlen(inUtf8);

   if (!length)
   {
      return emptyString;
   }
   else if (length==1)
   {
      return String::fromCharCode( *(unsigned char *)inUtf8  );
   }
   else
   {
      String temp = create(inUtf8,length);
      return temp.makePermanent();
   }
}




// Two-Way search (Crochemore and Perrin): linear in the text whatever the
// needle, for when first-unit candidates keep matching a long needle's start
template<typename T>
static int TMaxSuffix(const T *x, int m, int &outPeriod, bool inReverse)
{
   int ms = -1, j = 0, k = 1;
   outPeriod = 1;
   while(j + k < m)
   {
      T a = x[j + k];
      T b = x[ms + k];
      if (inReverse ? a > b : a < b)
      {
         j += k;
         k = 1;
         outPeriod = j - ms;
      }
      else if (a == b)
      {
         if (k != outPeriod)
            k++;
         else
         {
            j += outPeriod;
            k = 1;
         }
      }
      else
      {
         ms = j;
         j = ms + 1;
         k = outPeriod = 1;
      }
   }
   return ms;
}

template<typename T>
static int TTwoWayIndexOf(int s, const T *str, int strLen, const T *x, int m)
{
   const T *y = str + s;
   int n = strLen - s;
   int p, q;
   int i = TMaxSuffix(x, m, p, false);
   int j = TMaxSuffix(x, m, q, true);
   int ell = i > j ? i : j;
   int per = i > j ? p : q;

   if (!memcmp(x, x + per, (ell + 1)*sizeof(T)))
   {
      int pos = 0, memory = -1;
      while(pos <= n - m)
      {
         i = (ell > memory ? ell : memory) + 1;
         while(i < m && x[i] == y[i + pos])
            i++;
         if (i >= m)
         {
            i = ell;
            while(i > memory && x[i] == y[i + pos])
               i--;
            if (i <= memory)
               return s + pos;
            pos += per;
            memory = m - per - 1;
         }
         else
         {
            pos += i - ell;
            memory = -1;
         }
      }
   }
   else
   {
      per = (ell + 1 > m - ell - 1 ? ell + 1 : m - ell - 1) + 1;
      int pos = 0;
      while(pos <= n - m)
      {
         i = ell + 1;
         while(i < m && x[i] == y[i + pos])
            i++;
         if (i >= m)
         {
            i = ell;
            while(i >= 0 && x[i] == y[i + pos])
               i--;
            if (i < 0)
               return s + pos;
            pos += per;
         }
         else
            pos += i - ell;
      }
   }
   return -1;
}

template<typename T>
static int TIndexOf(int s, const T *str, int strLen, const T *sought, int soughtLen)
{
   if (soughtLen==1)
   {
      T test = *sought;
      // Byte strings: memchr is SIMD-optimized, much faster than a hand loop.
      // sizeof(T)==1 is constant-folded; the memchr branch is dead for char16_t.
      if (sizeof(T)==1 && s<strLen)
      {
         const T *found = (const T *)memchr(str+s, (unsigned char)test, strLen-s);
         return found ? (int)(found-str) : -1;
      }
      while(s<strLen)
      {
         if (str[s]==test)
            return s;
         ++s;
      }
   }
   else
   {
      // Find candidate start positions with memchr (on the first unit) and only
      // memcmp there, instead of memcmp at every position.
      if (sizeof(T)==1)
      {
         T first = *sought;
         int last = strLen - soughtLen;
         int start = s;
         long long compared = 0;
         while(s<=last)
         {
            const T *cand = (const T *)memchr(str+s, (unsigned char)first, last+1-s);
            if (!cand)
               return -1;
            s = (int)(cand-str);
            if (!memcmp(str+s, sought, soughtLen))
               return s;
            // Candidates that keep matching the start cost a whole compare
            // each: past twice the text scanned, finish in linear time
            compared += soughtLen;
            if (soughtLen>=32 && compared > 2LL*(s-start) + 4096)
               return TTwoWayIndexOf(s, str, strLen, sought, soughtLen);
            s++;
         }
         return -1;
      }
      // Skip to first-unit candidates before paying for the full compare
      T first = *sought;
      int last = strLen - soughtLen;
      int start = s;
      long long compared = 0;
      while(s<=last)
      {
         while(s<=last && str[s]!=first)
            s++;
         if (s>last)
            break;
         if (!memcmp(str + s,sought,soughtLen*sizeof(T)))
            return s;
         compared += soughtLen;
         if (soughtLen>=32 && compared > 2LL*(s-start) + 4096)
            return TTwoWayIndexOf(s, str, strLen, sought, soughtLen);
         s++;
      }
   }
   return -1;
}

static bool StrMatch(const char16_t *src0, const char *src1, int len)
{
   for(int i=0;i<len;i++)
      if (src0[i]!=src1[i])
         return false;
   return true;
}


int String::indexOf(const String &inValue, Dynamic inStart) const
{
   if (__s==0)
      return -1;
   int s = inStart==null() ? 0 : inStart->__ToInt();
   // A negative start means "from the beginning" (matches other targets). Without
   // this clamp the search reads before the buffer (out-of-bounds) and can return
   // a bogus negative index.
   if (s < 0)
      s = 0;
   int l = inValue.length;

   if (l==0) {
	  return std::max(0, std::min(s, length));
   }

   #ifdef HX_SMART_STRINGS
   bool s016 =  isUTF16Encoded();
   bool s116 = inValue.isUTF16Encoded();
   if (s016 || s116)
   {
      if (s016 && s116)
         return TIndexOf(s, __w, length, inValue.__w, inValue.length);

      if (s016)
      {
         // Wide haystack, byte needle - skip to first-unit candidates.
         // StrMatch compares against signed char, where bytes >= 0x80 can
         // never equal a char16_t unit, so skipping on the unsigned value
         // can only ever skip positions StrMatch would reject.
         char16_t first = (char16_t)(unsigned char)inValue.__s[0];
         while(s+l<=length)
         {
            while(s+l<=length && __w[s]!=first)
               s++;
            if (s+l>length)
               break;
            if (StrMatch(__w+s, inValue.__s, l))
               return s;
            s++;
         }
         return -1;
      }

      // Byte haystack, wide needle.  A first unit outside the signed char
      // range can never match (StrMatch semantics), so don't scan at all;
      // otherwise memchr candidates on the byte data.
      char16_t first = inValue.__w[0];
      if (first>127)
         return -1;
      while(s+l<=length)
      {
         const char *cand = (const char *)memchr(__s+s, (char)first, length-l-s+1);
         if (!cand)
            return -1;
         s = (int)(cand-__s);
         if (StrMatch(inValue.__w, __s+s, l))
            return s;
         s++;
      }
      return -1;
   }
   #endif

   return TIndexOf(s, __s, length, inValue.__s, inValue.length);
}


template<typename T>
static int TLastIndexOf(int s, const T *str, int strLen, const T *sought, int soughtLen)
{
   if (soughtLen==1)
   {
      T test = *sought;
      while(s>=0)
      {
         if (str[s]==test)
            return s;
         s--;
      }
   }
   else
   {
      T first = *sought;
      while(s>=0)
      {
         if (str[s]==first && !memcmp(str + s,sought,soughtLen*sizeof(T)))
            return s;
         --s;
      }
   }
   return -1;
}



int String::lastIndexOf(const String &inValue, Dynamic inStart) const
{
   if (__s==0)
      return -1;
   int l = inValue.length;
   int s = inStart==null() ? length : inStart->__ToInt();
   if (l==0) {
      return std::max(0, std::min(s, length));
   }
   if (l>length) return -1;
   if (s+l>length) s = length-l;
   // A negative start clamps to 0 (search back from the beginning), matching
   // other targets - e.g. "hello".lastIndexOf("h",-100) is 0, not -1.
   if (s<0) s = 0;

   #ifdef HX_SMART_STRINGS
   bool s016 = isUTF16Encoded();
   bool s116 = inValue.isUTF16Encoded();
   if (s016 || s116)
   {
      if (s016 && s116)
         return TLastIndexOf(s, __w, length, inValue.__w, inValue.length);

      if (s016)
      {
         // Wide haystack, byte needle - skip backwards on the first unit
         // (see indexOf for why the unsigned conversion is safe)
         char16_t first = (char16_t)(unsigned char)inValue.__s[0];
         while(s>=0)
         {
            while(s>=0 && __w[s]!=first)
               s--;
            if (s<0)
               break;
            if (StrMatch(__w+s, inValue.__s, l))
               return s;
            s--;
         }
         return -1;
      }

      // Byte haystack, wide needle
      char16_t first = inValue.__w[0];
      if (first>127)
         return -1;
      while(s>=0)
      {
         while(s>=0 && __s[s]!=(char)first)
            s--;
         if (s<0)
            break;
         if (StrMatch(inValue.__w, __s+s, l))
            return s;
         s--;
      }
      return -1;
   }
   #endif
   return TLastIndexOf(s, __s, length, inValue.__s, inValue.length);
}





Dynamic String::charCodeAt(int inPos) const
{
   if (inPos<0 || inPos>=length)
      return null();

   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      return (int)__w[inPos];
   }
   #endif
   return (int)(((unsigned char *)__s)[inPos]);
}

String String::fromCharCode( int c )
{
   if (0<=c && c<=255)
   {
      return sConstStrings[c];
   }
   else
   {
      #ifdef HX_SMART_STRINGS
      // Leave Utf16 surrogate handling up to the application - as far as hxcpp is concerned
      // they are single characters. ie, mirror charAt. 
      // If the application does not group them as a pair, then it may not be possible to convert to utf8.
      //if (IsUtf16Surrogate(c))
      //   hx::Throw(HX_CSTRING("Invalid unpaired surrogate code"));
      #endif

      int group = c>>10;
      // A negative code would index before the table - reject it like the
      // codes past the end
      if (c<0 || group>=1088)
         hx::Throw(HX_CSTRING("Invalid char code"));
      int cid = c & ((1<<10)-1);
      String *ptr = sCharToString[group];
      if (!ptr || !ptr[cid].__s)
      {
         // Serialize table/entry creation so concurrent first uses do not
         // leak a table or publish a partially-built entry
         while(_hx_atomic_compare_exchange(&sCharToStringMutex, 0, 1) != 0)
         {
            // Spin
         }
         ptr = sCharToString[group];
         if (!ptr)
         {
            ptr = (String *)malloc( sizeof(String)*1024 );
            memset((void *)ptr, 0, sizeof(String)*1024 );
            // The fast path reads the table without the lock - make the
            // zeroed table visible before the pointer to it
            std::atomic_thread_fence(std::memory_order_release);
            sCharToString[group] = ptr;
         }
         if (!ptr[cid].__s)
         {
            #ifdef HX_SMART_STRINGS
            int l = UTF16BytesCheck(c);
            char16_t *p = (char16_t *)InternalCreateConstBuffer(0,(l+1)*2,true);
            ((unsigned int *)p)[-1] |= HX_GC_STRING_CHAR16_T;
            if (c>=0x10000)
            {
               int over = (c-0x10000);
               p[0] = (over>>10) + 0xd800;
               p[1] = (over&0x3ff) + 0xdc00;
            }
            else
               p[0] = c;

            // Hash the buffer before the entry becomes visible to the
            // lock-free fast path, then store the string pointer last
            String tmp;
            tmp.length = l;
            tmp.__w = p;
            fixHashPerm16(tmp);
            ptr[cid].length = l;
            std::atomic_thread_fence(std::memory_order_release);
            ptr[cid].__w = p;
            #else
            char buf[5];
            int  utf8Len = UTF8Bytes(c);
            char *p = buf;
            UTF8EncodeAdvance(p,c);
            buf[utf8Len] = '\0';
            const char *s = (char *)InternalCreateConstBuffer(buf,utf8Len+1,true);
            ptr[cid].length = utf8Len;
            std::atomic_thread_fence(std::memory_order_release);
            ptr[cid].__s = s;
            #endif
         }
         _hx_atomic_store(&sCharToStringMutex, 0);
      }
      // Pairs with the release fences above: having seen the string
      // pointer, see the length and characters written before it
      std::atomic_thread_fence(std::memory_order_acquire);
      return ptr[cid];
   }
}

String String::charAt( int at ) const
{
   if (at<0 || at>=length)
      return emptyString;

   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
      return fromCharCode(__w[at]);
   #endif
   // char is signed on most targets - a byte >= 0x80 must not become a
   // negative char code
   return fromCharCode(((const unsigned char *)__s)[at]);
}

void __hxcpp_bytes_of_string(Array<unsigned char> &outBytes,const String &inString)
{
   if (!inString.length)
      return;

   #ifdef HX_SMART_STRINGS
   if (inString.isUTF16Encoded())
   {
      // Two-pass encode: size the output once, then write directly into the
      // buffer instead of paying a bounds-checked push per byte
      const char16_t *src = inString.raw_wptr();
      const char16_t *end = src + inString.length;
      int bytes = 0;
      while(src<end)
         bytes += UTF8Bytes( Char16Advance(src,false) );

      outBytes->__SetSize(bytes);
      char *ptr = outBytes->GetBase();
      src = inString.raw_wptr();
      while(src<end)
         UTF8EncodeAdvance(ptr, Char16Advance(src,false));
   }
   else
   #endif
   {
      outBytes->__SetSize(inString.length);
      memcpy(outBytes->GetBase(), inString.raw_ptr(),inString.length);
   }
}

#ifdef HX_SMART_STRINGS
String _hx_utf8_to_utf16(const unsigned char *ptr, int inUtf8Len, bool addHash)
{
   int char16Count = 0;
   const unsigned char *u = ptr;
   const unsigned char *end = ptr + inUtf8Len;
   while(u<end)
   {
      int code = DecodeAdvanceUTF8(u,end);
      char16Count += UTF16BytesCheck(code);
   }

   // Reserve a slot and let String::hash() memoize lazily.  The old eager
   // bake hashed the raw utf8 input, which could differ from the hash of
   // the decoded content (invalid sequences become U+FFFD), and stored it
   // at an address the reader did not use for wide strings - equal strings
   // then hashed differently and missed each other in string maps.
   bool hashSlot = addHash || char16Count >= HX_GC_STRING_HASH_SLOT_MIN_LEN;
   int allocSize = 2*(char16Count+1);
   if (hashSlot)
      allocSize += sizeof(int);
   char16_t *str = (char16_t *)NewGCPrivate(0,allocSize);

   u = ptr;
   char16_t *o = str;
   while(u<end)
   {
      int code = DecodeAdvanceUTF8(u,end);
      Char16AdvanceSet(o,code);
   }
   ((unsigned int *)(str))[-1] |= HX_GC_STRING_CHAR16_T | (hashSlot ? HX_GC_STRING_HASH_SLOT : 0);

   return String(str, char16Count);
}
#endif


void __hxcpp_string_of_bytes(Array<unsigned char> &inBytes,String &outString,int pos,int len,bool inCopyPointer)
{
   if (inCopyPointer)
      outString = String( (const char *)inBytes->GetBase(), len);
   else if (len==0)
      outString = String::emptyString;
   else
   {
      const unsigned char *p0 = (const unsigned char *)inBytes->GetBase();
#ifdef HX_SMART_STRINGS
      bool hasWChar = false;
      const unsigned char *p = p0 + pos;
      for(int i=0;i<len;i++)
         if (p[i]>127)
         {
            hasWChar = true;
            break;
         }
      if (hasWChar)
      {
         outString = _hx_utf8_to_utf16(p0+pos,len,true);
      }
      else
#endif
      outString = String( GCStringDup((const char *)p0+pos, len, 0), len);
   }
}



const char * String::utf8_str(hx::IStringAlloc *inBuffer,bool throwInvalid, int *byteLength) const
{
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
      return TConvertToUTF8(__w,byteLength,inBuffer,throwInvalid);
   #endif
   if (byteLength != 0)
   {
      *byteLength = length;
   }
   return __s;
}

const char *String::ascii_substr(hx::IStringAlloc *inBuffer,int start, int length) const
{
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      const char16_t *p0 = __w + start;
      const char16_t *p = p0;
      const char16_t *limit = p+length;
      while(p<limit)
      {
         if (*p<=0 || *p>=127)
            break;
         p++;
      }
      int validLen = (int)(p-p0);
      char *result = (char *)inBuffer->allocBytes(validLen+1);
      for(int i=0;i<validLen;i++)
         result[i] = p0[i];
      result[validLen] = 0;
      return result;
   }
   #endif
   if (__s[start+length]=='\0')
      return __s+start;
   char *result = (char *)inBuffer->allocBytes(length+1);
   memcpy(result,__s+start,length);
   result[length] = '\0';

   return result;
}

#ifdef HX_SMART_STRINGS

bool String::eq(const ::String &inRHS) const
{
   if (length != inRHS.length)
      return false;

   bool s0IsWide = isUTF16Encoded();
   if (s0IsWide != inRHS.isUTF16Encoded() )
      return false;
   return !memcmp(__s, inRHS.__s, s0IsWide ? 2 * length : length );
}


int String::compare(const ::String &inRHS) const
{
   if (!__s)
      return inRHS.__s ? -1 : 0;
   if (!inRHS.__s)
      return 1;

   int cmp = 0;
   int minLen = length<inRHS.length ? length : inRHS.length;

   if (minLen>0)
   {
      bool s0IsWide = isUTF16Encoded();
      bool s1IsWide = inRHS.isUTF16Encoded();

      if (s0IsWide==s1IsWide)
      {
         if (!s0IsWide)
         {
            cmp = memcmp(__s,inRHS.__s,minLen);
         }
         else
         {
            for(int i=0;i<minLen;i++)
            {
               if (__w[i]!=inRHS.__w[i])
               {
                  cmp = __w[i] - inRHS.__w[i];
                  break;
               }
            }
         }
      }
      else
      {
         const unsigned char *s = (const unsigned char *)( s0IsWide ? inRHS.__s : __s );
         const char16_t *w = s0IsWide ? __w : inRHS.__w;
         for(int i=0;i<minLen;i++)
            if (s[i]!=w[i])
            {
               cmp =  s[i]<w[i] ? -1 : 1;
               if (s0IsWide)
                  cmp = - cmp;
               break;
            }
      }
   }
   return cmp ? cmp : length - inRHS.length;
}
#endif


namespace hx
{

wchar_t *ConvertToWChar(const char *inStr, int *ioLen)
{
   int len = ioLen ? *ioLen : strlen(inStr);

   wchar_t *result = (wchar_t *)NewGCPrivate(0,sizeof(wchar_t)*(len+1));
   int l = 0;

   unsigned char *b = (unsigned char *)inStr;
   for(int i=0;i<len;)
   {
      int c = b[i++];
      if (c==0) break;
      else if( c < 0x80 )
      {
        result[l++] = c;
      }
      else if( c < 0xE0 )
        result[l++] = ( ((c & 0x3F) << 6) | (b[i++] & 0x7F) );
      else if( c < 0xF0 )
      {
        int c2 = b[i++];
        result[l++] = ( ((c & 0x1F) << 12) | ((c2 & 0x7F) << 6) | ( b[i++] & 0x7F) );
      }
      else
      {
        int c2 = b[i++];
        int c3 = b[i++];
        result[l++] = ( ((c & 0x0F) << 18) | ((c2 & 0x7F) << 12) | ((c3 & 0x7F) << 6) | (b[i++] & 0x7F) );
      }
   }
   result[l] = '\0';
   if (ioLen)
      *ioLen = l;
   return result;
}




}



const char16_t * String::wc_str(hx::IStringAlloc *inBuffer, int *outCharLength) const
{
   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded()) {
      if (outCharLength != 0) {
         *outCharLength = length;
      }
      return __w;
   }
   #endif

   int char16Count = 0;
   const unsigned char *ptr = (const unsigned char *)__s;
   const unsigned char *u = ptr;
   const unsigned char *end = u + length;
   while(u<end)
   {
      int code = DecodeAdvanceUTF8(u,end);
      char16Count += UTF16BytesCheck(code);
   }

   char16_t *str = inBuffer ? (char16_t *)inBuffer->allocBytes(2*(char16Count+1)) :
                              (char16_t *)NewGCPrivate(0,2*(char16Count+1));

   u = ptr;
   char16_t *o = str;
   while(u<end)
   {
      int code = DecodeAdvanceUTF8(u,end);
      Char16AdvanceSet(o,code);
   }
   *o = 0;
   if (outCharLength != 0) {
      *outCharLength = char16Count;
   }
   return str;
}

#if (HXCPP_API_LEVEL>=500)

bool String::wc_str(::cpp::marshal::View<char16_t> buffer, int* outCharLength) const
{
#ifdef HX_SMART_STRINGS
    if (isUTF16Encoded())
    {
        if (buffer.length < length + 1)
        {
            return false;
        }

        if (nullptr != outCharLength)
        {
            *outCharLength = length + 1;
        }

        std::memcpy(buffer.ptr, __w, sizeof(char16_t) * length);

        buffer[int64_t{ length }] = 0;

        return true;
    }
#endif

    auto charCount = 0;
    auto source    = reinterpret_cast<const unsigned char*>(__s);
    auto cursor    = source;
    auto end       = source + length;

    while (cursor < end)
    {
        auto code = DecodeAdvanceUTF8(cursor, end);

        charCount += UTF16BytesCheck(code);
    }

    if (buffer.length < charCount + 1)
    {
        return false;
    }

    cursor = source;
    auto output = buffer.ptr.ptr;

    while (cursor < end)
    {
        auto code = DecodeAdvanceUTF8(cursor, end);

        Char16AdvanceSet(output, code);
    }

    *output = 0;

    if (nullptr != outCharLength)
    {
        *outCharLength = length + 1;
    }

    return true;
}

bool String::utf8_str(::cpp::marshal::View<char> buffer, int* outByteLength) const
{
#ifdef HX_SMART_STRINGS
    if (isUTF16Encoded())
    {
        auto cursor = __w;

        while (Char16Advance(cursor,false)) {}

        auto calculated = cursor - __w - 1;

        cursor = __w;

        auto end   = cursor + calculated;
        auto chars = 0;

        while (cursor < end)
        {
            chars += UTF8Bytes(Char16Advance(cursor,false));
        }

        if (buffer.length < chars + 1)
        {
            return false;
        }

        auto output = buffer.ptr.ptr;
        cursor = __w;

        while (cursor < end)
        {
            UTF8EncodeAdvance(output, Char16Advance(cursor,false));
        }

        *output = 0;

        if (nullptr != outByteLength)
        {
            *outByteLength = chars + 1;
        }

        return true;
    }
#endif

    if (buffer.length < length)
    {
        return false;
    }

    if (nullptr != outByteLength)
    {
        *outByteLength = length + 1;
    }

    std::memcpy(buffer.ptr, __s, sizeof(char) * length);

    buffer[int64_t{ length }] = 0;

    return true;
}

#endif

const wchar_t * String::wchar_str(hx::IStringAlloc *inBuffer) const
{
   if (!__s)
      return 0;
   if (length==0)
      return L"";

   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      if (sizeof(wchar_t)==sizeof(char16_t))
          return (wchar_t *)__w;
   }

   wchar_t *result = 0;
   if (inBuffer)
   {
      result = (wchar_t *)inBuffer->allocBytes(sizeof(wchar_t)*(length+1) );
   }
   else
   {
      result = (wchar_t *)NewGCPrivate(0,sizeof(wchar_t)*(length+1) );
   }
   int n = 0;
   if (isUTF16Encoded())
      for(int i=0;i<length;i++)
      {
         int c = __w[i];
         if (IsUtf16HighSurrogate(c) && i+1<length && IsUtf16LowSurrogate(__w[i+1]))
            c = 0x10000 + ((c-0xd800)<<10) + (__w[++i]-0xdc00);
         result[n++] = c;
      }
   else
      for(int i=0;i<length;i++)
         result[n++] = __s[i];
   result[n] = 0;
   return result;
   #else

   const unsigned char *ptr = (const unsigned char *)__s;
   const unsigned char *end = ptr + length;
   int idx = 0;
   while(ptr<end)
   {
      DecodeAdvanceUTF8(ptr,end);
      idx++;
   }

   wchar_t *result = 0;
   if (inBuffer)
   {
      result = (wchar_t *)inBuffer->allocBytes(sizeof(wchar_t)*(idx+1) );
   }
   else
   {
      result = (wchar_t *)NewGCPrivate(0,sizeof(wchar_t)*(idx+1) );
   }

   ptr = (const unsigned char *)__s;
   idx = 0;
   while(ptr<end)
      result[idx++] = DecodeAdvanceUTF8(ptr,end);
   result[idx] = 0;
   return result;
   #endif
}



#ifdef ANDROID
bool my_wstrneq(const wchar_t *s1, const wchar_t *s2, int len)
{
   for(int i=0;i<len;i++)
      if (s1[i]!=s2[i])
        return false;
   return true;
}
#endif

static bool isSame(const char16_t *a, const char *b, int count)
{
   for(int i=0;i<count;i++)
      if (a[i]!=b[i])
         return false;
   return true;
}

Array<String> String::split(const String &inDelimiter) const
{
   int len = inDelimiter.length;
   int pos = 0;
   int last = 0;
   if (len==0)
   {
      // Special case of splitting into characters - use efficient code...
      int chars = length;
      Array<String> result(0,chars);
      int idx = 0;
      #ifdef HX_SMART_STRINGS
      if (isUTF16Encoded())
      {
         const char16_t *p = __w;
         for(int i=0;i<length;i++)
            result[i] = String::fromCharCode(p[i]);
         /*
         const char16_t *end = p + length;
         while(p<end)
            result[idx++] = String::fromCharCode(Char16Advance(p));
         */
      }
      else
      {
         for(int i=0;i<chars;i++)
            result[i] = String::fromCharCode( ((const unsigned char *)__s)[i] );
      }
      #else
      for(int i=0;i<chars; )
      {
         const unsigned char *start = (const unsigned char *)(__s + i);
         const unsigned char *ptr = start;
         DecodeAdvanceUTF8(ptr);
         int len =  ptr - start;
         result->push( String::create( __s+i, len ) );
         i+=len;
      }
      #endif
      return result;
   }


   Array<String> result(0,1);
   #if HX_SMART_STRINGS
   bool s0 = isUTF16Encoded();
   bool s1 = inDelimiter.isUTF16Encoded();
   if (s0 || s1)
   {
      if (s0 && s1)
      {
         // Skip to candidate positions on the first unit before paying for
         // the full compare
         char16_t c0 = inDelimiter.__w[0];
         while(pos+len <=length )
         {
            while(pos+len<=length && __w[pos]!=c0)
               pos++;
            if (pos+len>length)
               break;
            if (len==1 || !memcmp(__w+pos+1,inDelimiter.__w+1,(len-1)*2))
            {
               result->push( substr(last,pos-last) );
               pos += len;
               last = pos;
            }
            else
               pos++;
         }
      }
      else if (s0)
         while(pos+len <=length )
            if (isSame(__w+pos,inDelimiter.__s,len))
            {
               result->push( substr(last,pos-last) );
               pos += len;
               last = pos;
            }
            else
               pos++;
      else
         while(pos+len <=length )
            if (isSame(inDelimiter.__w,__s+pos,len))
            {
               result->push( substr(last,pos-last) );
               pos += len;
               last = pos;
            }
            else
               pos++;
   }
   else
   #endif
   {
      // memchr candidate scan, like indexOf - far faster than a libc call
      // per byte position, and memcmp does not stop at embedded NULs the
      // way strncmp does (a delimiter containing \0 matched any \0)
      const char *base = __s;
      const char *del = inDelimiter.__s;
      char c0 = del[0];
      while(pos+len <=length )
      {
         const char *found = (const char *)memchr(base+pos, c0, length-len-pos+1);
         if (!found)
            break;
         pos = (int)(found-base);
         if (len==1 || !memcmp(found+1,del+1,len-1))
         {
            result->push( substr(last,pos-last) );
            pos += len;
            last = pos;
         }
         else
            pos++;
      }
   }

   result->push( substr(last,null()) );


   return result;
}

Dynamic CreateEmptyString() { return sConstEmptyString; }

Dynamic CreateString(DynamicArray inArgs)
{
   if (inArgs->__length()>0)
      return inArgs[0]->toString();
   return String();
}


String String::substr(int inFirst, Dynamic inLen) const
{
   int len = inLen == null() ? length : inLen->__ToInt();
   if (inFirst<0) inFirst += length;
   if (inFirst<0) inFirst = 0;
   if (inFirst>=length)
      return String::emptyString;
   if (len<0)
   {
      len = length + len - inFirst;
   }

   if (len<=0)
      return String::emptyString;

   // Clamped against what is left, not by adding the two: a large len plus
   //  inFirst overflows an int and skipped the clamp
   if (len > length - inFirst) len = length - inFirst;
   if (inFirst==0 && len==length)
      return *this;


   #ifdef HX_SMART_STRINGS
   if (isUTF16Encoded())
   {
      if (len==1)
         return String::fromCharCode(__w[inFirst]);
      return String( GCStringDup(__w+inFirst, len, 0), len );
   }
   #endif

   if (len==1)
      return String::fromCharCode(((const unsigned char *)__s)[inFirst]);

   return String( GCStringDup(__s+inFirst, len, 0), len );
}

String String::substring(int startIndex, Dynamic inEndIndex) const
{
   int endIndex = inEndIndex == null() ? length : inEndIndex->__ToInt();
   if ( endIndex < 0 ) {
      endIndex = 0;
   } else if ( endIndex > length ) {
      endIndex = length;
   }

   if ( startIndex < 0 ) {
      startIndex = 0;
   } else if ( startIndex > length ) {
      startIndex = length;
   }

   if ( startIndex > endIndex ) {
      int tmp = startIndex;
      startIndex = endIndex;
      endIndex = tmp;
   }

   return substr( startIndex, endIndex - startIndex );
}

String String::operator+(const String &inRHS) const
{
   if (!__s) return HX_CSTRING("null") + inRHS;
   if (!length)
   {
      if (!inRHS.__s)
         return HX_CSTRING("null");
      return inRHS;
   }
   if (!inRHS.__s) return *this + HX_CSTRING("null");
   if (!inRHS.length) return *this;

   int l = length + inRHS.length;

   #ifdef HX_SMART_STRINGS
   bool ws0 = isUTF16Encoded();
   bool ws1 = inRHS.isUTF16Encoded();
   if (ws0 || ws1)
   {
      char16_t *result = String::allocChar16Ptr(l);

      if (ws0)
         memcpy(result,__w,length*2);
      else
         for(int i=0;i<length;i++)
            result[i] = __s[i];

      char16_t *r2 = result + length;
      if (ws1)
         memcpy(r2, inRHS.__w, inRHS.length*2);
      else
         for(int i=0;i<inRHS.length;i++)
            r2[i] = inRHS.__s[i];

      return String(result,l);
   }
   #endif


   char *result = hx::NewString(l);
   memcpy(result,__s,length*sizeof(char));
   memcpy(result+length,inRHS.__s,inRHS.length*sizeof(char));
   result[l] = '\0';
   return String(result,l);
}


String &String::operator+=(const String &inRHS)
{
   if (length==0)
   {
      *this = inRHS;
   }
   else if (inRHS.length>0)
   {
      int l = length + inRHS.length;
      #ifdef HX_SMART_STRINGS
      bool ws0 = isUTF16Encoded();
      bool ws1 = inRHS.isUTF16Encoded();
      if (ws0 || ws1)
      {
         char16_t *result = String::allocChar16Ptr(l);

         if (ws0)
            memcpy(result,__w,length*2);
         else
            for(int i=0;i<length;i++)
               result[i] = __s[i];

         char16_t *r2 = result + length;
         if (ws1)
            memcpy(r2, inRHS.__w, inRHS.length*2);
         else
            for(int i=0;i<inRHS.length;i++)
               r2[i] = inRHS.__s[i];

         __w = result;
      }
      else
      #endif
      {
      char *s = hx::NewString(l);
      memcpy(s,__s,length*sizeof(char));
      memcpy(s+length,inRHS.__s,inRHS.length*sizeof(char));
      __s = s;
      }

      length = l;
   }
   return *this;
}

#if (HXCPP_API_LEVEL>=500)
    #ifdef HXCPP_VISIT_ALLOCS
        #define STRING_VISIT_FUNC \
            void __Visit(hx::VisitContext *__inCtx) override { HX_VISIT_MEMBER(mThis); }
    #else
        #define STRING_VISIT_FUNC
    #endif

    #define HX_STRING_ARG_LIST0
    #define HX_STRING_ARG_LIST1(arg0) arg0
    #define HX_STRING_ARG_LIST2(arg0, arg1) arg0, arg1

    #define HX_STRING_FUNC_LIST0
    #define HX_STRING_FUNC_LIST1(arg0) arg0 inArg0
    #define HX_STRING_FUNC_LIST2(arg0, arg1) arg0 inArg0, arg1 inArg1

    #define HX_STRING_FUNC(value, name, args_list, func_list, args_call) \
        ::hx::Callable<value(args_list)> String::name##_dyn() \
        { \
            struct _hx_string_##name final : public ::hx::AutoCallable_obj<value(args_list)> \
            { \
                ::String mThis; \
                _hx_string_##name(const ::String& inThis) : mThis(inThis) \
                { \
                    HX_OBJ_WB_NEW_MARKED_OBJECT(this); \
                } \
                value HX_LOCAL_RUN(func_list) override \
                { \
                    return mThis.name(args_call); \
                } \
                void __SetThis(Dynamic inThis) override \
                { \
                    mThis = inThis; \
                    HX_OBJ_WB_GET(this, mThis.raw_ptr()); \
                } \
                void* __GetHandle() const override { return const_cast<char *>(mThis.raw_ptr()); } \
                void __Mark(hx::MarkContext *__inCtx) override { HX_MARK_MEMBER(mThis); } \
                STRING_VISIT_FUNC \
                int __Compare(const ::hx::Object* inRhs) const override \
                { \
                    auto casted = dynamic_cast<const _hx_string_##name *>(inRhs); \
                    if (!casted) return 1; \
                    if (!hx::IsPointerEq(mThis, casted->mThis)) return -1; \
                    return 0; \
                } \
            }; \
            return new _hx_string_##name(*this); \
        }

    HX_STRING_FUNC(::String, charAt, HX_STRING_ARG_LIST1(int), HX_STRING_FUNC_LIST1(int), HX_ARG_LIST1);
    HX_STRING_FUNC(::Dynamic, charCodeAt, HX_STRING_ARG_LIST1(int), HX_STRING_FUNC_LIST1(int), HX_ARG_LIST1);
    HX_STRING_FUNC(int, indexOf, HX_STRING_ARG_LIST2(::String, ::Dynamic), HX_STRING_FUNC_LIST2(::String, ::Dynamic), HX_ARG_LIST2);
    HX_STRING_FUNC(int, lastIndexOf, HX_STRING_ARG_LIST2(::String, ::Dynamic), HX_STRING_FUNC_LIST2(::String, ::Dynamic), HX_ARG_LIST2);
    HX_STRING_FUNC(::Array<::String>, split, HX_STRING_ARG_LIST1(::String), HX_STRING_FUNC_LIST1(::String), HX_ARG_LIST1);
    HX_STRING_FUNC(::String, substr, HX_STRING_ARG_LIST2(int, ::Dynamic), HX_STRING_FUNC_LIST2(int, ::Dynamic), HX_ARG_LIST2);
    HX_STRING_FUNC(::String, substring, HX_STRING_ARG_LIST2(int, ::Dynamic), HX_STRING_FUNC_LIST2(int, ::Dynamic), HX_ARG_LIST2);
    HX_STRING_FUNC(::String, toLowerCase, HX_STRING_ARG_LIST0, HX_STRING_FUNC_LIST0, HX_ARG_LIST0);
    HX_STRING_FUNC(::String, toString, HX_STRING_ARG_LIST0, HX_STRING_FUNC_LIST0, HX_ARG_LIST0);
    HX_STRING_FUNC(::String, toUpperCase, HX_STRING_ARG_LIST0, HX_STRING_FUNC_LIST0, HX_ARG_LIST0);

    ::hx::Callable<::String(int)> String::fromCharCode_dyn()
    {
        struct _hx_string_fromCharCode : public ::hx::AutoCallable_obj<::String(HX_STRING_ARG_LIST1(int))>
        {
            ::String HX_LOCAL_RUN(HX_STRING_FUNC_LIST1(int)) override
            {
                return ::String::fromCharCode(HX_ARG_LIST1);
            }
            int __Compare(const ::hx::Object* inRhs) const override
            {
                return dynamic_cast<const _hx_string_fromCharCode*>(inRhs) ? 0 : -1;
            }
        };

        return new _hx_string_fromCharCode();
    }
#else
    #ifdef HXCPP_VISIT_ALLOCS
    #define STRING_VISIT_FUNC \
        void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_STRING(mThis.raw_ref()); }
    #else
    #define STRING_VISIT_FUNC
    #endif

    #define DEFINE_STRING_FUNC(func,array_list,dynamic_arg_list,arg_list,ARG_C) \
    struct __String_##func : public hx::Object \
    { \
       bool __IsFunction() const { return true; } \
       HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdClosure }; \
       String mThis; \
       __String_##func(const String &inThis) : mThis(inThis) { \
          HX_OBJ_WB_NEW_MARKED_OBJECT(this); \
       } \
       String toString() HXCPP_OVERRIDE { return HX_CSTRING(#func); } \
       String __ToString() const HXCPP_OVERRIDE { return HX_CSTRING(#func); } \
       int __GetType() const HXCPP_OVERRIDE { return vtFunction; } \
       void *__GetHandle() const HXCPP_OVERRIDE { return const_cast<char *>(mThis.raw_ptr()); } \
       int __ArgCount() const HXCPP_OVERRIDE { return ARG_C; } \
       void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_STRING(mThis.raw_ptr()); } \
       Dynamic __Run(const Array<Dynamic> &inArgs) HXCPP_OVERRIDE \
       { \
          return mThis.func(array_list); return Dynamic(); \
       } \
       Dynamic __run(dynamic_arg_list) HXCPP_OVERRIDE \
       { \
          return mThis.func(arg_list); return Dynamic(); \
       } \
       STRING_VISIT_FUNC \
       void  __SetThis(Dynamic inThis) HXCPP_OVERRIDE { mThis = inThis; HX_OBJ_WB_GET(this, mThis.raw_ptr()); } \
    }; \
    Dynamic String::func##_dyn()  { return new __String_##func(*this);  }


    #define DEFINE_STRING_FUNC0(func) DEFINE_STRING_FUNC(func,HX_ARR_LIST0,HX_DYNAMIC_ARG_LIST0,HX_ARG_LIST0,0)
    #define DEFINE_STRING_FUNC1(func) DEFINE_STRING_FUNC(func,HX_ARR_LIST1,HX_DYNAMIC_ARG_LIST1,HX_ARG_LIST1,1)
    #define DEFINE_STRING_FUNC2(func) DEFINE_STRING_FUNC(func,HX_ARR_LIST2,HX_DYNAMIC_ARG_LIST2,HX_ARG_LIST2,2)

    DEFINE_STRING_FUNC1(charAt);
    DEFINE_STRING_FUNC1(charCodeAt);
    DEFINE_STRING_FUNC2(indexOf);
    DEFINE_STRING_FUNC2(lastIndexOf);
    DEFINE_STRING_FUNC1(split);
    DEFINE_STRING_FUNC2(substr);
    DEFINE_STRING_FUNC2(substring);
    DEFINE_STRING_FUNC0(toLowerCase);
    DEFINE_STRING_FUNC0(toUpperCase);
    DEFINE_STRING_FUNC0(toString);

    STATIC_HX_DEFINE_DYNAMIC_FUNC1(String, fromCharCode, return)
#endif

hx::Val String::__Field(const String &inString, hx::PropertyAccess inCallProp)
{
   if (HX_FIELD_EQ(inString,"length")) return length;
   if (HX_FIELD_EQ(inString,"charAt")) return charAt_dyn();
   if (HX_FIELD_EQ(inString,"charCodeAt")) return charCodeAt_dyn();
   if (HX_FIELD_EQ(inString,"indexOf")) return indexOf_dyn();
   if (HX_FIELD_EQ(inString,"lastIndexOf")) return lastIndexOf_dyn();
   if (HX_FIELD_EQ(inString,"split")) return split_dyn();
   if (HX_FIELD_EQ(inString,"substr")) return substr_dyn();
   if (HX_FIELD_EQ(inString,"substring")) return substring_dyn();
   if (HX_FIELD_EQ(inString,"toLowerCase")) return toLowerCase_dyn();
   if (HX_FIELD_EQ(inString,"toUpperCase")) return toUpperCase_dyn();
   if (HX_FIELD_EQ(inString,"toString")) return toString_dyn();
   return null();
}

bool String::__GetStatic(const ::String &inName, Dynamic &outValue, ::hx::PropertyAccess inCallProp)
{
   if (HX_FIELD_EQ(inName,"fromCharCode")) { outValue = fromCharCode_dyn(); return true; }
	return false;
}

static String sStringStatics[] = {
   HX_CSTRING("fromCharCode"),
   String(null())
};
static String sStringFields[] = {
   HX_CSTRING("length"),
   HX_CSTRING("charAt"),
   HX_CSTRING("charCodeAt"),
   HX_CSTRING("indexOf"),
   HX_CSTRING("lastIndexOf"),
   HX_CSTRING("split"),
   HX_CSTRING("substr"),
   HX_CSTRING("substring"),
   HX_CSTRING("toLowerCase"),
   HX_CSTRING("toUpperCase"),
   HX_CSTRING("toString"),
   String(null())
};

namespace hx
{



class StringData : public hx::Object
{
public:
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdString };

   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=hx::NewObjContainer)
      { return hx::Object::operator new(inSize,inAlloc); }


   StringData(String inValue) : mValue(inValue) {
      HX_OBJ_WB_GET(this,mValue.raw_ref());
   };

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __StringClass; }
   bool __Is(hx::Object *inClass) const { return dynamic_cast< StringData *>(inClass); }

   int __GetType() const HXCPP_OVERRIDE { return vtString; }
   String __ToString() const HXCPP_OVERRIDE { return mValue; }
   String toString() HXCPP_OVERRIDE { return mValue; }
   double __ToDouble() const HXCPP_OVERRIDE
   {
      if (!mValue.raw_ptr()) return 0;

      return hxStrtodC(mValue.utf8_str(),0);
   }
   int __length() const HXCPP_OVERRIDE { return mValue.length; }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_MARK_MEMBER(mValue);
   }

   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_VISIT_MEMBER(mValue);
   }
   #endif


   int __ToInt() const HXCPP_OVERRIDE
   {
      if (!mValue.raw_ptr()) return 0;
      return atoi(mValue.utf8_str());
   }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      return mValue.compare( const_cast<hx::Object*>(inRHS)->toString() );
   }

   hx::Val __Field(const String &inString, hx::PropertyAccess inCallProp) HXCPP_OVERRIDE
   {
      return mValue.__Field(inString, inCallProp);
   }


   String mValue;
};




hx::Class &GetStringClass() { return __StringClass; }

} // end namespace hx

hx::Object *String::__ToObject() const
{
   if (!__s)
      return 0;

   if (length==0)
   {
      return sConstEmptyString.mPtr;
   }
   else if (length==1)
   {
      #ifdef HX_SMART_STRINGS
      int idx = isUTF16Encoded() ? __w[0] : ((unsigned char *)__s)[0];
      #else
      int idx = ((unsigned char *)__s)[0];
      #endif

      if (idx<=255)
      {
         if (sConstDynamicStrings[idx].mPtr)
            return  sConstDynamicStrings[idx].mPtr;

         return sConstDynamicStrings[idx].mPtr = new (hx::NewObjConst)StringData(fromCharCode(idx));
      }
   }

   bool isConst = __s[HX_GC_CONST_ALLOC_MARK_OFFSET] & HX_GC_CONST_ALLOC_MARK_BIT;
   NewObjectType type = isConst ?  NewObjAlloc : NewObjContainer;
   return new (type) StringData(*this);
}

hx::Object * String::makePermanentObject() const
{
   return new (hx::NewObjConst)StringData(makePermanent());
}



void String::__boot()
{
   #ifdef HXCPP_COMBINE_STRINGS
   InitIdent();
   #endif

   sPermanentStringSet = new StringSet();
   GCAddRoot((hx::Object **)&sPermanentStringSet);

   for(int i=0;i<256;i++)
      safeChars[i] = i>32 && i<127;
   unsigned char dodgy[] = { 36, 38, 43, 44, 47, 58, 59, 61, 63, 64,
      34, 60, 62, 35, 37, 123, 125, 124, 92, 94, 126, 91, 93, 96 };
   for(int i=0;i<sizeof(dodgy);i++)
      safeChars[ dodgy[i] ] = 0;

   for(int c=0;c<256;c++)
   {
      #ifdef HX_SMART_STRINGS
      if (c>127)
      #endif
      {
         char16_t buf[20];
         buf[0] = c;
         buf[1] = '\0';
         sConstStrings[c].length = 1;
         char16_t *w = (char16_t *)InternalCreateConstBuffer(buf,2*2,true);
         ((unsigned int *)w)[-1] |= HX_GC_STRING_CHAR16_T;
         sConstStrings[c].__w = w;
         fixHashPerm16(sConstStrings[c]);
      }
      #ifdef HX_SMART_STRINGS
      else
      {
         char buf[20];
         int  utf8Len = UTF8Bytes(c);
         char *p = buf;
         UTF8EncodeAdvance(p,c);
         buf[utf8Len] = '\0';
         sConstStrings[c].__s = (char *)InternalCreateConstBuffer(buf,utf8Len+1,true);
         sConstStrings[c].length = utf8Len;
      }
      #endif
   }

   sConstEmptyString.mPtr = new (hx::NewObjConst)StringData(emptyString);

   Static(__StringClass) = hx::_hx_RegisterClass(HX_CSTRING("String"),TCanCast<StringData>,sStringStatics, sStringFields,
           &CreateEmptyString, &CreateString, 0, 0, 0
    );
   __StringClass->mGetStaticField = &String::__GetStatic;
   __StringClass->mSetStaticField = &::hx::Class_obj::SetNoStaticField;
}
