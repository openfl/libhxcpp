#include <hxcpp.h>
#include <math.h>
#include <hxMath.h>
#include <stdio.h>
#include <atomic>

using namespace hx;



extern hx::Class __StringClass;
namespace hx
{

extern hx::Class hxEnumBase_obj__mClass;
extern hx::Class Object__mClass;


hx::Class __BoolClass;
hx::Class __IntClass;
hx::Class __FloatClass;
hx::Class __Int64Class;
hx::Class __PointerClass;
hx::Class __VoidClass;
#ifdef HXCPP_OBJC
hx::Class __ObjcClass;
#endif


hx::Class &GetBoolClass() { return __BoolClass; }
hx::Class &GetIntClass() { return __IntClass; }
hx::Class &GetFloatClass() { return __FloatClass; }
hx::Class &GetInt64Class() { return __Int64Class; }
hx::Class &GetPointerClass() { return __PointerClass; }
hx::Class &GetVoidClass() { return __VoidClass; }



// --- "Simple" Data Objects ---------------------------------------------------


Dynamic DynTrue;
Dynamic DynFalse;

class IntData : public hx::Object
{
public:
   enum { _hx_ClassId = hx::clsIdInt };

   bool _hx_isInstanceOf(int inClassId) HXCPP_OVERRIDE
   {
      return inClassId==1 || inClassId==(int)_hx_ClassId || inClassId==(int)hx::clsIdFloat;
   }


   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjAlloc, const char *inName="Int")
      { return hx::Object::operator new(inSize,inAlloc,inName); }
   IntData(int inValue=0) : mValue(inValue) {};

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __IntClass; }

   int __GetType() const HXCPP_OVERRIDE { return vtInt; }

   String toString() HXCPP_OVERRIDE { return String(mValue); }
   String __ToString() const HXCPP_OVERRIDE { return String(mValue); }
   double __ToDouble() const HXCPP_OVERRIDE { return mValue; }
   int __ToInt() const HXCPP_OVERRIDE { return mValue; }
   cpp::Int64 __ToInt64() const HXCPP_OVERRIDE { return mValue; }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      double diff = mValue - inRHS->__ToDouble();
      return diff < 0 ? -1 : diff==0 ? 0 : 1;
   }


   int mValue;
};


class BoolData : public hx::Object
{
public:
   HX_IS_INSTANCE_OF enum { _hx_ClassId = clsIdBool };

   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjAlloc,const char *inName="Bool")
      { return hx::Object::operator new(inSize,inAlloc,"Bool"); }
   BoolData(bool inValue=false) : mValue(inValue) {};

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __BoolClass; }

   int __GetType() const HXCPP_OVERRIDE { return vtBool; }

   String __ToString() const  HXCPP_OVERRIDE { return mValue ? HX_CSTRING("true") : HX_CSTRING("false"); }
   String toString() HXCPP_OVERRIDE { return mValue ? HX_CSTRING("true") : HX_CSTRING("false"); }
   double __ToDouble() const HXCPP_OVERRIDE { return mValue; }
   int __ToInt() const HXCPP_OVERRIDE { return mValue; }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      double diff = (double)mValue - inRHS->__ToDouble();
      return diff < 0 ? -1 : diff==0 ? 0 : 1;
   }


   bool mValue;
};



class DoubleData : public hx::Object
{
public:
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdFloat };

   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjAlloc,const char *inName="Float")
      { return hx::Object::operator new(inSize,inAlloc,inName); }
   DoubleData(double inValue=0) : mValue(inValue) {};

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __FloatClass; }

   int __GetType() const HXCPP_OVERRIDE { return vtFloat; }
   String toString() HXCPP_OVERRIDE { return String(mValue); }
   String __ToString() const HXCPP_OVERRIDE { return String(mValue); }
   double __ToDouble() const HXCPP_OVERRIDE { return mValue; }
   int __ToInt() const HXCPP_OVERRIDE { return (int)mValue; }
   cpp::Int64 __ToInt64() const HXCPP_OVERRIDE { return (cpp::Int64)mValue; }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      double rval = inRHS->__ToDouble();
      if (rval==mValue)
         return 0;

      return mValue < rval ? -1 :  1;
   }


   double mValue;
};


class Int64Data : public hx::Object
{
public:
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdInt64 };

   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjAlloc,const char *inName="Int64")
      { return hx::Object::operator new(inSize,inAlloc,inName); }
   Int64Data(cpp::Int64 inValue=0) : mValue(inValue) {};

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __Int64Class; }

   int __GetType() const HXCPP_OVERRIDE { return vtInt64; }
   String toString() HXCPP_OVERRIDE { return String(mValue); }
   String __ToString() const HXCPP_OVERRIDE { return String(mValue); }
   double __ToDouble() const HXCPP_OVERRIDE { return (double)mValue; }
   int __ToInt() const HXCPP_OVERRIDE { return (int)mValue; }
   cpp::Int64 __ToInt64() const HXCPP_OVERRIDE { return mValue; }
   void __GetFields(Array<String>& outFields) HXCPP_OVERRIDE
   {
      outFields->push( HX_HCSTRING("hi","\x01","\x5b","\x00","\x00") );
      outFields->push( HX_HCSTRING("lo","\x83","\x5e","\x00","\x00") );
   }
   hx::Val __Field(const String &inName, hx::PropertyAccess inCallProp) HXCPP_OVERRIDE
   {
      if (HX_FIELD_EQ(inName,"hi") ) { return hx::Val( (int)(mValue>>32)); }
      if (HX_FIELD_EQ(inName,"lo") ) { return hx::Val( (int)(mValue&0xffffffff)); }
      return hx::Object::__Field(inName,inCallProp);
   }
   hx::Val __SetField(const String &inName,const hx::Val &inValue, hx::PropertyAccess inCallProp) HXCPP_OVERRIDE
   {
      // Boxes from -1 to 255 are constants every boxing of that value shares,
      //  so writing one would change all of them: only a box of its own can change
      if ( (HX_FIELD_EQ(inName,"hi") || HX_FIELD_EQ(inName,"lo")) &&
           (((unsigned int *)this)[-1] & HX_GC_CONST_ALLOC_BIT) )
         hx::Throw( HX_CSTRING("Cannot change a shared boxed Int64 constant") );
      if (HX_FIELD_EQ(inName,"hi") )
      {
         // Combined unsigned, so a low word with its top bit set is not
         //  sign-extended over the new high word
         mValue = (cpp::Int64)( ((cpp::UInt64)(unsigned int)inValue.Cast< int >() << 32) | (cpp::UInt64)(unsigned int)mValue );
         return inValue;
      }
      if (HX_FIELD_EQ(inName,"lo") )
      {
         mValue = (mValue & (cpp::Int64(0xffffffff)<<32) ) | cpp::Int64( (unsigned int)(inValue.Cast< int >()));
         return inValue;
      }
      return hx::Object::__SetField(inName,inValue,inCallProp);
   }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      // Compare as Int64 - going through double loses precision above 2^53,
      // making distinct large values compare equal
      cpp::Int64 rval = inRHS->__ToInt64();
      if (rval==mValue)
         return 0;

      return mValue < rval ? -1 :  1;
   }




    cpp::Int64 mValue;
};



class PointerData : public hx::Object
{
public:
   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjContainer,const char *inName="cpp.Pointer")
      { return hx::Object::operator new(inSize,inAlloc,inName); }

   PointerData(void *inValue) : mValue(inValue) {};

    HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdPointer };

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __PointerClass; }

   // k_cpp_pointer
   int __GetType() const HXCPP_OVERRIDE { return vtAbstractBase + 2; }
   void * __GetHandle() const HXCPP_OVERRIDE { return mValue; }
   String toString() HXCPP_OVERRIDE
   {
      char buf[100];
      snprintf(buf,sizeof(buf),"Pointer(%p)", mValue);
      return String(buf);
   }
   String __ToString() const HXCPP_OVERRIDE { return String(mValue); }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      void *r = inRHS==0 ? 0 : inRHS->__GetHandle();
      return mValue < r ? -1 : mValue==r ? 0 : 1;
   }

   // A boxed pointer into the GC heap keeps what it points into, as a
   //  cpp.Pointer field does: a script's haxe.atomic.AtomicInt is one
   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      if (mValue)
         hx::MarkConservativePointer(mValue,__inCtx);
   }

   void *mValue;
};


class StructData : public hx::Object
{
public:
   inline void *operator new( size_t inSize, hx::NewObjectType inAlloc=NewObjContainer,const char *inName="cpp.Struct")
      { return hx::Object::operator new(inSize,inAlloc,inName); }

   StructData(const void *inValue,int inLength, cpp::DynamicHandlerFunc inHandler)
   {
      mLength= inLength;
      mValue = InternalNew(inLength,false);
      HX_OBJ_WB_GET(this, mValue);
      memcpy(mValue, inValue, inLength);
      mHandler = inHandler;
   }

   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdStruct };

   hx::Class __GetClass() const HXCPP_OVERRIDE { return __PointerClass; }

   // k_cpp_struct
   int __GetType() const HXCPP_OVERRIDE { return vtAbstractBase + 3; }
   void * __GetHandle() const HXCPP_OVERRIDE { return mValue; }
   String toString() HXCPP_OVERRIDE
   {
      return __ToString();
   }
   String __ToString() const HXCPP_OVERRIDE
   {
      String result;
      mHandler(cpp::dhoToString, mValue, 0, &result );
      return result;
   }
   const char *__CStr() const HXCPP_OVERRIDE
   {
      const char *result = "unknown";
      mHandler(cpp::dhoGetClassName, mValue, 0, &result );
      return result;
   }

   int __Compare(const hx::Object *inRHS) const HXCPP_OVERRIDE
   {
      if (!inRHS)
         return 1;

      int diff = __length() - inRHS->__length();
      if (diff==0)
         diff = __GetType() - inRHS->__GetType();
      if (diff==0)
         diff = memcmp( mValue, inRHS->__GetHandle(), mLength );

      if (diff<0) return -1;
      if (diff>0) return 1;
      return 0;
   }

   int __length() const HXCPP_OVERRIDE { return mLength; }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_MARK_ARRAY(mValue);
   }

   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_VISIT_ARRAY(mValue);
   }
   #endif

   int  mLength;
   void *mValue;
   cpp::DynamicHandlerFunc mHandler;
};





}


namespace cpp
{
// --- Pointer -------------------------------------------------

Dynamic CreateDynamicPointer(void *inValue) {
   if (!inValue)
      return Dynamic();
   return new hx::PointerData(inValue);
}

// --- Struct -------------------------------------------------

Dynamic CreateDynamicStruct(const void *inValue, int inSize, DynamicHandlerFunc inFunc)

{
   return new hx::StructData(inValue,inSize,inFunc); }
}



// --- Dynamic -------------------------------------------------

// Boxed values from -1 to 255, made once and shared by every thread. A slot is
// published with a release and read with an acquire, so a thread that finds a
// box also sees it built. Two threads may both make one: the first is kept, and
// the other is a small constant allocation that is never used.
static std::atomic<hx::Object *> sConstDynamicInts[256+1];
static std::atomic<hx::Object *> sConstDynamicInt64s[256+1];

template<typename DATA>
static hx::Object *constBox(std::atomic<hx::Object *> &ioSlot, int inVal)
{
   hx::Object *result = ioSlot.load(std::memory_order_acquire);
   if (!result)
   {
      hx::Object *made = new (hx::NewObjConst)DATA(inVal);
      if (ioSlot.compare_exchange_strong(result, made, std::memory_order_acq_rel, std::memory_order_acquire))
         result = made;
   }
   return result;
}

static hx::Object *fromSmallInt64(int inVal)
{
   return constBox<Int64Data>(sConstDynamicInt64s[inVal+1], inVal);
}

static hx::Object *fromInt(int inVal)
{
   if (inVal>=-1 && inVal<256)
      return constBox<IntData>(sConstDynamicInts[inVal+1], inVal);
   return (hx::Object *)new IntData(inVal);
}

Dynamic::Dynamic(bool inVal) : super( inVal ? hx::DynTrue.mPtr : hx::DynFalse.mPtr ) { }


Dynamic::Dynamic(int inVal)
{
   mPtr = fromInt(inVal);
}

Dynamic::Dynamic(short inVal)
{
   mPtr = fromInt(inVal);
}

#if !defined(__GNUC__) || defined(__MINGW32__) || defined(__APPLE__) || defined(__EMSCRIPTEN__) || (defined(__WORDSIZE) && (__WORDSIZE != 64))
// long is 64 bits on 64-bit macOS, where int64_t is long long. Emscripten
// defines no __WORDSIZE, and there long is 32 bits but not int
Dynamic::Dynamic(unsigned long inVal)
{
   mPtr = inVal==(unsigned int)inVal ? fromInt((unsigned int)inVal) : Dynamic((cpp::UInt64)inVal).mPtr;
}
Dynamic::Dynamic(long inVal)
{
   mPtr = inVal==(int)inVal ? fromInt((int)inVal) : Dynamic((cpp::Int64)inVal).mPtr;
}
#endif

Dynamic::Dynamic(unsigned int inVal)
{
   mPtr = fromInt(inVal);
}

Dynamic::Dynamic(unsigned short inVal)
{
   mPtr = fromInt(inVal);
}

Dynamic::Dynamic(unsigned char inVal)
{
   mPtr = fromInt(inVal);
}

Dynamic::Dynamic(signed char inVal)
{
   mPtr = fromInt(inVal);
}

Dynamic::Dynamic(char16_t inVal)
{
    mPtr = fromInt(inVal);
}

Dynamic::Dynamic(char32_t inVal)
{
    mPtr = fromInt(inVal);
}

Dynamic::Dynamic(double inVal)
{
   if ( inVal>=-1 && inVal<256 && (int)inVal==inVal && !(inVal==0 && signbit(inVal)) )
   {
      mPtr = constBox<IntData>(sConstDynamicInts[(int)inVal+1], (int)inVal);
   }
   else
      mPtr = (hx::Object *)new DoubleData(inVal);
}


Dynamic::Dynamic(cpp::Int64 inVal)
{
   int ival = (int)inVal;
   if ( ival==inVal && ival>=-1 && ival<256 )
      mPtr = fromSmallInt64(ival);
   else
      mPtr = (hx::Object *)new Int64Data(inVal);
}


Dynamic::Dynamic(cpp::UInt64 inVal)
{
   if ( inVal<256 )
      mPtr = fromSmallInt64((int)inVal);
   else
      mPtr = (hx::Object *)new Int64Data(inVal);
}





Dynamic::Dynamic(float inVal)
{
   mPtr = Dynamic( (double) inVal ).mPtr;
}

Dynamic::Dynamic(const cpp::CppInt32__ &inVal) :
  super(  Dynamic(inVal.mValue).mPtr ) { }

Dynamic::Dynamic(const String &inVal) :
  super( inVal.raw_ptr() ? inVal.__ToObject() : 0 ) { }

Dynamic::Dynamic(const HX_CHAR *inVal) :
  super( inVal ? String(inVal).__ToObject() : 0 ) { }


Dynamic Dynamic::operator+(const Dynamic &inRHS) const
{
   int t1 = mPtr ? mPtr->__GetType() : vtNull;
   int t2 = inRHS.mPtr ? inRHS.mPtr->__GetType() : vtNull;

   if (t1==vtInt && t2==vtInt)
   {
      // Keep the result typed vtInt with 32-bit wrap, consistent with
      // operator- and operator* (DYNAMIC_ARITH), and avoid boxing a double
      return Dynamic( mPtr->__ToInt() + inRHS.mPtr->__ToInt() );
   }
   if ( (t1==vtInt64 || t1==vtInt)  &&  (t2==vtInt64 || t2==vtInt) )
   {
      // Boxed Int64 is numeric - previously it fell through to string
      // concatenation, so adding two Int64 Dynamics produced a string
      return Dynamic( mPtr->__ToInt64() + inRHS.mPtr->__ToInt64() );
   }
   if ( (t1==vtInt || t1==vtFloat || t1==vtInt64)  &&  (t2==vtInt || t2==vtFloat || t2==vtInt64) )
   {
      return mPtr->__ToDouble() + inRHS.mPtr->__ToDouble();
   }
   if (!mPtr)
      return String() + inRHS;
   if (!inRHS.mPtr)
      return *this + String();

   return const_cast<hx::Object*>(mPtr)->toString() + const_cast<Dynamic&>(inRHS)->toString();
}

#define DYN_OP_ADD(TYPE) \
Dynamic Dynamic::operator+(const TYPE &i) const \
{ \
   int t = mPtr ? mPtr->__GetType() : vtNull; \
   if (t==vtString) \
      return Cast<String>() + String(i); \
   return Cast<double>() + i; \
}

// Int-ish scalars take the same vtInt fast path as DYNAMIC_ARITH, and a
// vtInt64 lhs stays in 64-bit math instead of losing precision via double
#define DYN_OP_ADD_INT(TYPE) \
Dynamic Dynamic::operator+(const TYPE &i) const \
{ \
   int t = mPtr ? mPtr->__GetType() : vtNull; \
   if (t==vtString) \
      return Cast<String>() + String(i); \
   if (t==vtInt) \
      return Dynamic( (int)(*this) + i ); \
   if (t==vtInt64) \
      return Dynamic( mPtr->__ToInt64() + (cpp::Int64)i ); \
   return Cast<double>() + i; \
}

#define DYN_OP_ADD_INT64(TYPE) \
Dynamic Dynamic::operator+(const TYPE &i) const \
{ \
   int t = mPtr ? mPtr->__GetType() : vtNull; \
   if (t==vtString) \
      return Cast<String>() + String(i); \
   if (t==vtInt || t==vtInt64) \
      return Dynamic( (cpp::Int64)(mPtr->__ToInt64() + (cpp::Int64)i) ); \
   return Cast<double>() + i; \
}

DYN_OP_ADD(double)
DYN_OP_ADD(float)
DYN_OP_ADD_INT(int)
DYN_OP_ADD_INT(unsigned int)
DYN_OP_ADD_INT(short)
DYN_OP_ADD_INT(unsigned short)
DYN_OP_ADD_INT(signed char)
DYN_OP_ADD_INT(unsigned char)
DYN_OP_ADD_INT(char16_t)
DYN_OP_ADD_INT(char32_t)
DYN_OP_ADD_INT64(cpp::Int64)
DYN_OP_ADD_INT64(cpp::UInt64)

Dynamic Dynamic::operator+(const cpp::Variant &v) const
{
   int t = mPtr ? mPtr->__GetType() : vtNull;
   if (t==vtString || v.type == cpp::Variant::typeString)
      return Cast<String>() + v.asString();
   if (t==vtInt && v.isInt())
      return Dynamic( mPtr->__ToInt() + (int)v );
   return Cast<double>() + v.asDouble();
}



double Dynamic::operator%(const Dynamic &inRHS) const
{
   // Guard null operands like operator+ does, so a null Dynamic does not crash
   // here (e.g. from cppia or native callers); a null coerces to 0.
   int t1 = mPtr ? mPtr->__GetType() : vtNull;
   int t2 = inRHS.mPtr ? inRHS.mPtr->__GetType() : vtNull;
   if (t1==vtInt && t2==vtInt)
      return mPtr->__ToInt() % inRHS->__ToInt();
   double lhs = mPtr ? mPtr->__ToDouble() : 0.0;
   double rhs = inRHS.mPtr ? inRHS.mPtr->__ToDouble() : 0.0;
   int even = (int)(lhs/rhs);
   double remain = lhs - even * rhs;
   if (remain<0) remain += fabs(rhs);
   return remain;
}

hx::IndexRef Dynamic::operator[](int inIndex)
{
   return hx::IndexRef(mPtr,inIndex);
}


void Dynamic::ThrowBadFunctionError()
{
   #ifdef HXCPP_DEBUGGER
   NullReference("Function", true);
   #endif
   hx::Throw( HX_NULL_FUNCTION_POINTER );
}

#include <hx/DynamicImpl.h>

namespace cpp
{
CppInt32__::CppInt32__(const Dynamic &inD) : mValue(inD->__ToInt()) { }
}

namespace hx {
null BadCast()
{
   hx::Throw(HX_INVALID_CAST);
   return null();
}

void InvalidInterface()
{
   hx::Throw(HX_INVALID_INTERFACE);
}


}


static bool NoCast(hx::Object *) { return false; }
static bool IsFloat(hx::Object *inPtr)
{
   return inPtr && (TCanCast<IntData>(inPtr) || TCanCast<DoubleData>(inPtr) || TCanCast<Int64Data>(inPtr) );
}
static bool IsInt64(hx::Object *inPtr)
{
   return inPtr && (TCanCast<Int64Data>(inPtr) || TCanCast<IntData>(inPtr));
}

static bool IsPointer(hx::Object *inPtr)
{
   return inPtr && inPtr->__GetType() >= vtAbstractBase;
}

static bool IsInt(hx::Object *inPtr)
{
   if (!inPtr)
      return false;
   if (TCanCast<IntData>(inPtr))
      return true;
   // Cheap virtual type probe instead of RTTI dynamic_cast - this runs for
   // every Std.isOfType(x, Int)
   int t = inPtr->__GetType();
   if (t==vtFloat)
   {
      double val = inPtr->__ToDouble();
      return val>=-2147483648.0 && val<=2147483647.0 && (int)val==val;
   }
   if (t==vtInt64)
   {
      cpp::Int64 val64 = inPtr->__ToInt64();
      int val = (int)val64;
      return val==val64;
   }
   return false;
}

static Dynamic createEmptyInt64()
{
   return new Int64Data();
}

static Dynamic createInt64(hx::DynamicArray inArgs)
{
   return new Int64Data();
}

void Dynamic::__boot()
{
   Static(__VoidClass) = hx::_hx_RegisterClass(HX_CSTRING("Void"),NoCast,sNone,sNone,0,0,0, 0, 0
      #ifdef HXCPP_VISIT_ALLOCS
      ,0
      #endif
   );
   Static(__BoolClass) = hx::_hx_RegisterClass(HX_CSTRING("Bool"),TCanCast<BoolData>,sNone,sNone, 0,0, 0);
   Static(__IntClass) = hx::_hx_RegisterClass(HX_CSTRING("Int"),IsInt,sNone,sNone,0,0, 0 );
   Static(__FloatClass) = hx::_hx_RegisterClass(HX_CSTRING("Float"),IsFloat,sNone,sNone, 0,0,&__IntClass );
   Static(__Int64Class) = hx::_hx_RegisterClass(HX_CSTRING("cpp::Int64"),IsInt64,sNone,sNone, 0,0,&__IntClass );
   __Int64Class->mConstructEmpty = &createEmptyInt64;
   __Int64Class->mConstructArgs = &createInt64;

   Static(__PointerClass) = hx::_hx_RegisterClass(HX_CSTRING("cpp::Pointer"),IsPointer,sNone,sNone, 0,0,&__PointerClass );
   DynTrue = Dynamic( new (hx::NewObjConst) hx::BoolData(true) );
   DynFalse = Dynamic( new (hx::NewObjConst) hx::BoolData(false) );
#ifdef HXCPP_OBJC
   Static(__ObjcClass) = hx::_hx_RegisterClass(HX_CSTRING("objc::BoxedType"),IsPointer,sNone,sNone, 0,0,&__ObjcClass );
#endif
}
