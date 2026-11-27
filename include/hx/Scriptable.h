#ifndef INCLUDED_HX_SCRIPTABLE
#define INCLUDED_HX_SCRIPTABLE

#include <typeinfo>
#include <string.h>
#ifdef __clang__
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif


namespace hx
{

extern bool gEnableJit;
inline void EnableJit(bool inEnable) { gEnableJit = inEnable; }
// Whether the JIT compiles each script function when it is first called, rather than
// all of a module's functions when it loads. -D HXCPP_CPPIA_EAGER_JIT turns it off
extern bool gLazyJit;

#define HXCPP_CPPIA_SUPER_ARG(x) , (x)

struct ScriptNamedFunction : public ScriptFunction
{
   ScriptNamedFunction(const ScriptFunction &s) : ScriptFunction(s), name(0), isStatic(false), superExecute(0) { }

#if (HXCPP_API_LEVEL >= 500)
   ScriptNamedFunction(const char *inName=0,StackExecute inExe=0,const char *inSig=0, bool inIsStatic=false, StackExecute superExecute=0, bool inIsOverride=false)
      : ScriptFunction(inExe, inSig), name(inName), isStatic(inIsStatic), isOverride(inIsOverride), superExecute(superExecute)  { }
#else
   ScriptNamedFunction(const char *inName=0,StackExecute inExe=0,const char *inSig=0, bool inIsStatic=false, StackExecute superExecute=0)
      : ScriptFunction(inExe, inSig), name(inName), isStatic(inIsStatic), superExecute(superExecute)  { }
#endif

   const char *name;
   bool isStatic;
#if (HXCPP_API_LEVEL >= 500)
   bool isOverride;
#endif
   StackExecute superExecute;
};


// cppia frames pack values, so a Float can be at any 4-byte offset. memcpy
// reads and writes it there on every target, and compiles to a single load or
// store wherever the target allows that. The HXCPP_ALIGN_FLOAT version copied
// the two halves by hand and wrote only the high one, so on 32-bit ARM and
// Emscripten a Float stored in a frame kept a stale low half.
inline void SetFloatAligned(void *inPtr, const Float &inValue)
{
   memcpy(inPtr, &inValue, sizeof(Float));
}


inline Float GetFloatAligned(const void *inPtr)
{
   Float result;
   memcpy(&result, inPtr, sizeof(Float));
   return result;
}


inline void StackContext::pushFloat(Float f)
{
   SetFloatAligned(pointer, f);
   pointer += sizeof(Float);
}
inline void StackContext::pushString(const String &s)
{
   setSlot(pointer, s);
   pointer += sizeof(String);
}

inline void StackContext::pushObject(Dynamic d)
{
   setSlot(pointer, d.mPtr);
   pointer += sizeof(hx::Object *);
}

inline void StackContext::returnFloat(Float f)
{
   SetFloatAligned(frame, f);
}
inline void StackContext::returnString(const String &s)
{
   setSlot(frame, s);
}
inline void StackContext::returnObject(Dynamic d)
{
   setSlot(frame, d.mPtr);
}

inline hx::Object *StackContext::getThis(bool inCheckPtr)
{
   hx::Object *thiz = getSlot<hx::Object *>(frame);
   #ifdef HXCPP_CHECK_POINTER
      if (inCheckPtr)
      {
         if (!thiz) NullReference("This", false);
         #ifdef HXCPP_GC_CHECK_POINTER
         GCCheckPointer(thiz);
         #endif
      }
   #endif
   return thiz;
}


inline Float StackContext::getFloat(int inPos)
{
   return GetFloatAligned(frame+inPos);
}
inline String StackContext::getString(int inPos)
{
   return getSlot<String>(frame+inPos);
}
inline Dynamic StackContext::getObject(int inPos)
{
   return getSlot<hx::Object *>(frame+inPos);
}


enum SignatureChar
{
   sigVoid = 'v',
   sigBool = 'b',
   sigInt = 'i',
   sigFloat = 'f',
   sigString = 's',
   sigObject = 'o',
};



struct AutoStack
{
   CppiaCtx *ctx;
   unsigned char *pointer;
   unsigned char *frame;

   AutoStack(CppiaCtx *inCtx) : ctx(inCtx)
   {
      frame = ctx->frame;
      pointer = ctx->pointer;
      ctx->frame = pointer;
   }
   AutoStack(CppiaCtx *inCtx,unsigned char *inPointer) : ctx(inCtx)
   {
      frame = ctx->frame;
      pointer = inPointer;
      ctx->frame = pointer;
   }

   ~AutoStack()
   {
      ctx->pointer = pointer;
      ctx->frame = frame;
   }
};





typedef hx::Object * (*ScriptableClassFactory)(void **inVTable,int inDataSize);
typedef hx::Object * (*ScriptableInterfaceFactory)(void **inVTable,::hx::Object *);

void ScriptableRegisterClass( String inName, int inBaseSize, ScriptNamedFunction *inFunctions, ScriptableClassFactory inFactory, ScriptFunction inConstruct);


void ScriptableRegisterInterface( String inName, ScriptNamedFunction *inFunctions, void *inInterfacePointers);
void ScriptableRegisterNameSlots(const char *inNames[], int inLength);

::String ScriptableToString(void *);
hx::Class ScriptableGetClass(void *);
int ScriptableGetType(void *);
void ScriptableMark(void *, hx::Object *, HX_MARK_PARAMS);
void ScriptableVisit(void *, hx::Object *, HX_VISIT_PARAMS);
bool ScriptableField(hx::Object *, const ::String &,hx::PropertyAccess inCallProp,Dynamic &outResult);
bool ScriptableField(hx::Object *, int inName,hx::PropertyAccess inCallProp,Float &outResult);
bool ScriptableField(hx::Object *, int inName,hx::PropertyAccess inCallProp,Dynamic &outResult);
void ScriptableGetFields(hx::Object *inObject, Array< ::String> &outFields);
bool ScriptableSetField(hx::Object *, const ::String &, Dynamic inValue,hx::PropertyAccess inCallProp, Dynamic &outValue);


class CppiaLoadedModule_obj : public ::hx::Object
{
public:
   virtual void run() = 0;
   virtual void boot() = 0;
   virtual ::hx::Class resolveClass( ::String inName) = 0;
   // Unregisters the module's classes and resources and clears its statics.
   // Its code is freed once nothing from it is in use (docs/CppiaModuleRelease.md).
   virtual void release() { }
};
typedef ::hx::ObjectPtr<CppiaLoadedModule_obj> CppiaLoadedModule;



} // End namespace hx

void __scriptable_load_neko(String inName);
void __scriptable_load_cppia(String inCode);
::hx::CppiaLoadedModule __scriptable_cppia_from_string(String inCode);
::hx::CppiaLoadedModule __scriptable_cppia_from_data(Array<unsigned char> inBytes);
// Teaches the cppia loader another script format. A script given to
// __scriptable_cppia_from_data or _from_string (Module.fromData and fromString,
// so Host.runFile too) that starts with inMagic, and not with CPPIA or CPPIB,
// goes to inTranslate as bytes, and the cppia that returns is loaded in its
// place: text as a String, or text or binary cppia as bytes. Registering a
// magic again replaces its translator, and null removes the format
void __scriptable_cppia_register_format(String inMagic, Dynamic inTranslate);
void __scriptable_load_neko_bytes(Array<unsigned char> inBytes);
void __scriptable_load_abc(Array<unsigned char> inBytes);
// Modules whose native memory has not been freed yet
int __scriptable_cppia_live_modules();
// Collects, then lists each released module still loaded, with what of it is
// still live: { module, instances: [{ name, count, bytes }], closures: { count,
// bytes } }. docs/CppiaModuleRelease.md says what each counts
Array<Dynamic> __scriptable_cppia_retained();
// Whether modules loaded now are compiled by the JIT: it is built in, enabled
// with cpp.cppia.Host.enableJit, and can get executable memory on this platform
bool __scriptable_cppia_jit_active();
// 16 hex digits standing for the classes, functions and fields this host gives
// scripts. A module with a "__cppia_host" resource loads only into the host
// whose fingerprint that resource holds
String __scriptable_host_fingerprint();
// The host functions that the scripts loaded so far call by name, each as
// "package.Class.function", sorted. Such a call looks the function up and boxes
// its arguments each time it runs: it is one cppia found no typed function for,
// as with a function whose name is overloaded in C++, or one whose class the
// script knows only as a private interface
Array<String> __scriptable_cppia_calls_by_name();
// Sets the size in bytes of the script stack, which holds script frames, for
// threads that attach from now on and for the calling thread if no script frame
// is on it (docs/ThreadsAndStacks.md). The size starts at HXCPP_CPPIA_STACK_SIZE,
// 128 KB by default
void __scriptable_cppia_set_stack_size(int inBytes);
// The size of the calling thread's script stack
int __scriptable_cppia_stack_size();
// Defined where the two calls above exist, for hosts that build against older
// runtimes too
#define HXCPP_CPPIA_STACK_SIZE_API 1

#define HX_SCRIPTABLE_REGISTER_INTERFACE(name,class) \
   ::hx::ScriptableRegisterInterface( HX_CSTRING(name), __scriptableFunctions, & class##_scriptable )

#define HX_SCRIPTABLE_REGISTER_CLASS(name,class) \
   ::hx::ScriptableRegisterClass( HX_CSTRING(name), (int)offsetof(class##__scriptable,__scriptVTable) + sizeof(void *), __scriptableFunctions, class##__scriptable::__script_create, class##__scriptable::__script_construct )


#ifdef HXCPP_VISIT_ALLOCS
#define SCRIPTABLE_VISIT_FUNCTION \
void __Visit(HX_VISIT_PARAMS) HXCPP_OVERRIDE { super::__Visit(HX_VISIT_ARG); ::hx::ScriptableVisit(__scriptVTable[-1],this,HX_VISIT_ARG); }
#else
#define SCRIPTABLE_VISIT_FUNCTION
#endif


/*
 * A Haxe `toString` with parameters, whatever they are, compiles to a C++ overload that
 * hides `hx::Object::toString()` without overriding it. In a class that inherits it,
 * `__superString` is that class, so `__superString::toString()` does not compile.
 * Native code calling `toString()` on such an object reaches `hx::Object::toString()`,
 * so the scriptable wrapper calls that one in this case.
 */
template<typename T>
inline auto _hx_scriptable_super_toString(T *inSelf, int) -> decltype(inSelf->T::toString(), ::String())
{
   return inSelf->T::toString();
}

template<typename T>
inline ::String _hx_scriptable_super_toString(T *inSelf, ...)
{
   return inSelf->::hx::Object::toString();
}


#define HX_DEFINE_SCRIPTABLE(ARG_LIST) \
   inline void *operator new( size_t inSize, int inExtraDataSize ) \
   { \
      return ::hx::InternalNew(inSize + inExtraDataSize,true); \
   } \
   inline void operator delete(void *,int) {} \
   public: \
   void **__scriptVTable; \
   static ::hx::Object *__script_create(void **inVTable, int inExtra) { \
    __ME *result = new (inExtra) __ME(); \
    result->__scriptVTable = inVTable; \
   return result; } \
   void ** __GetScriptVTable() HXCPP_OVERRIDE { return __scriptVTable; } \
   ::String toString() HXCPP_OVERRIDE {  if (__scriptVTable[0] ) \
     { ::hx::CppiaCtx *ctx = ::hx::CppiaCtx::getCurrent(); ::hx::AutoStack a(ctx); ctx->pushObject(this); return ctx->runString(__scriptVTable[0]); } \
      else return _hx_scriptable_super_toString<__superString>(static_cast<__superString *>(this), 0); } \
   ::String __ToString() const HXCPP_OVERRIDE { return ::hx::ScriptableToString(__scriptVTable[-1]); } \
   ::hx::Class __GetClass() const HXCPP_OVERRIDE { return ::hx::ScriptableGetClass(__scriptVTable[-1]); } \
   int __GetType() const HXCPP_OVERRIDE { return ::hx::ScriptableGetType(__scriptVTable[-1]); } \
   void __Mark(HX_MARK_PARAMS) HXCPP_OVERRIDE { super::__Mark(HX_MARK_ARG); ::hx::ScriptableMark(__scriptVTable[-1],this,HX_MARK_ARG); } \
   SCRIPTABLE_VISIT_FUNCTION



#define HX_DEFINE_SCRIPTABLE_INTERFACE \
   void **__scriptVTable; \
   ::Dynamic mDelegate; \
   ::hx::Object *__GetRealObject() { return mDelegate.mPtr; } \
   SCRIPTABLE_VISIT_FUNCTION \
   void ** __GetScriptVTable() { return __scriptVTable; } \
   public: \
   static ::hx::Object *__script_create(void **inVTable,::hx::Object *inDelegate) { \
    __ME *result = new __ME(); \
    result->__scriptVTable = inVTable; \
    result->mDelegate = inDelegate; \
    return result; }



#define HX_DEFINE_SCRIPTABLE_DYNAMIC \
 \
	::hx::Val __Field(const ::String &inName,::hx::PropertyAccess inCallProp) HXCPP_OVERRIDE \
      { ::Dynamic result; if (::hx::ScriptableField(this,inName,inCallProp,result)) return result; return super::__Field(inName,inCallProp); } \
	::Float __INumField(int inFieldID) \
		{ ::Float result; if (::hx::ScriptableField(this,inFieldID,::hx::paccAlways,result)) return result; return super::__INumField(inFieldID); } \
	::Dynamic __IField(int inFieldID) \
		{ ::Dynamic result; if (::hx::ScriptableField(this,inFieldID,::hx::paccAlways,result)) return result; return super::__IField(inFieldID); } \
   ::hx::Val __SetField(const ::String &inName,const ::hx::Val &inValue,::hx::PropertyAccess inCallProp) HXCPP_OVERRIDE \
   { \
      ::Dynamic value; \
      if (::hx::ScriptableSetField(this, inName, inValue,inCallProp,value)) \
         return value; \
		return super::__SetField(inName,inValue,inCallProp); \
   } \
	void __GetFields(::Array< ::String> &outFields) HXCPP_OVERRIDE \
		{ super::__GetFields(outFields); ::hx::ScriptableGetFields(this,outFields); }




#include <hx/ScriptCall.h>

#endif
