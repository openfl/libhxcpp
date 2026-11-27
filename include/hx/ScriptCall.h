#ifndef INCLUDED_HX_SCRIPT_CALL
#define INCLUDED_HX_SCRIPT_CALL

// The Haxe compiler writes script functions (the typed entries cppia calls a host
//  function through) only for a public class. A private class, and the class behind
//  an abstract, has none, so a script that reaches one through inlined code called it
//  by name, with its arguments boxed. Every function still has HX_DEFINE_DYNAMIC_FUNC,
//  which in a scriptable host also registers a typed entry made here from the
//  function's own C++ signature. cppia's linker looks one up when it finds no script
//  function. The entry reads its arguments and returns its result exactly as the
//  compiler's own script functions do.

#include <type_traits>

namespace hx
{

// One registered function. These are static objects that link themselves into a list
//  as the program starts, so nothing here may allocate.
struct HXCPP_EXTERN_CLASS_ATTRIBUTES ScriptCallEntry
{
   ScriptCallEntry(hx::Class *inClass, const char *inName, StackExecute inExecute, const char *inSignature, bool inIsStatic);

   hx::Class       *classRef;
   const char      *name;
   StackExecute    execute;
   const char      *signature;
   bool            isStatic;
   ScriptCallEntry *next;
};


// --- How one value is read from, or returned to, a script frame -----------------

template<typename T> struct ScriptCallPlain { typedef T Type; enum { usable = 1 }; };
template<typename T> struct ScriptCallPlain<const T> { typedef T Type; enum { usable = 1 }; };
template<typename T> struct ScriptCallPlain<const T &> { typedef T Type; enum { usable = 1 }; };
// A temporary cannot be handed to a function that wants to write its argument
template<typename T> struct ScriptCallPlain<T &> { typedef T Type; enum { usable = 0 }; };

template<typename T> struct ScriptCallArg
{
   enum { size = sizeof(void *), code = 'o', usable = std::is_convertible< ::Dynamic, T >::value };
   static inline ::Dynamic get(CppiaCtx *ctx, int inPos) { return ctx->getObject(inPos); }
};
template<> struct ScriptCallArg<int>
{
   enum { size = sizeof(int), code = 'i', usable = 1 };
   static inline int get(CppiaCtx *ctx, int inPos) { return ctx->getInt(inPos); }
};
template<> struct ScriptCallArg<bool>
{
   enum { size = sizeof(int), code = 'i', usable = 1 };
   static inline bool get(CppiaCtx *ctx, int inPos) { return ctx->getInt(inPos) != 0; }
};
template<> struct ScriptCallArg<double>
{
   enum { size = sizeof(Float), code = 'f', usable = 1 };
   static inline double get(CppiaCtx *ctx, int inPos) { return ctx->getFloat(inPos); }
};
template<> struct ScriptCallArg<float>
{
   enum { size = sizeof(Float), code = 'f', usable = 1 };
   static inline float get(CppiaCtx *ctx, int inPos) { return (float)ctx->getFloat(inPos); }
};
template<> struct ScriptCallArg< ::String >
{
   enum { size = sizeof(::String), code = 's', usable = 1 };
   static inline ::String get(CppiaCtx *ctx, int inPos) { return ctx->getString(inPos); }
};

template<typename T> struct ScriptCallArgOf : public ScriptCallArg< typename ScriptCallPlain<T>::Type >
{
   enum { ok = ScriptCallPlain<T>::usable && ScriptCallArg< typename ScriptCallPlain<T>::Type >::usable };
};

template<typename R> struct ScriptCallReturn
{
   enum { code = 'o', usable = std::is_convertible< R, ::Dynamic >::value };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnObject( CALL::call(ctx) ); }
};
template<> struct ScriptCallReturn<void>
{
   enum { code = 'v', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { CALL::call(ctx); }
};
template<> struct ScriptCallReturn<int>
{
   enum { code = 'i', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnInt( CALL::call(ctx) ); }
};
template<> struct ScriptCallReturn<bool>
{
   enum { code = 'b', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnInt( CALL::call(ctx) ); }
};
template<> struct ScriptCallReturn<double>
{
   enum { code = 'f', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnFloat( CALL::call(ctx) ); }
};
template<> struct ScriptCallReturn<float>
{
   enum { code = 'f', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnFloat( CALL::call(ctx) ); }
};
template<> struct ScriptCallReturn< ::String >
{
   enum { code = 's', usable = 1 };
   template<typename CALL> static inline void run(CppiaCtx *ctx) { ctx->returnString( CALL::call(ctx) ); }
};


// --- The arguments of one function ------------------------------------------------

template<int... I> struct ScriptCallSeq { };
template<int N, int... I> struct ScriptCallCount : public ScriptCallCount<N-1, N-1, I...> { };
template<int... I> struct ScriptCallCount<0, I...> { typedef ScriptCallSeq<I...> Seq; };

// The type of argument I and where it is in the frame. The first slot of a frame is
//  'this', for a static function too.
template<int I, typename... A> struct ScriptCallAt;
template<typename A0, typename... A> struct ScriptCallAt<0, A0, A...>
{
   typedef A0 Type;
   enum { pos = sizeof(void *) };
};
template<int I, typename A0, typename... A> struct ScriptCallAt<I, A0, A...>
{
   typedef typename ScriptCallAt<I-1, A...>::Type Type;
   enum { pos = ScriptCallArgOf<A0>::size + ScriptCallAt<I-1, A...>::pos };
};

template<typename... A> struct ScriptCallAll;
template<> struct ScriptCallAll<> { enum { ok = 1 }; };
template<typename A0, typename... A> struct ScriptCallAll<A0, A...>
{
   enum { ok = ScriptCallArgOf<A0>::ok && ScriptCallAll<A...>::ok };
};


// --- The entry for one function ---------------------------------------------------

// Anything that is neither a plain member function nor a plain static one gets none
template<typename SIG, SIG inFunc> struct ScriptCallStub
{
   enum { usable = 0, isStatic = 0 };
};

template<typename C, typename R, typename... A, R (C::*inFunc)(A...)>
struct ScriptCallStub<R (C::*)(A...), inFunc>
{
   enum { usable = ScriptCallReturn<R>::usable && ScriptCallAll<A...>::ok, isStatic = 0 };

   template<int... I> static inline R call(CppiaCtx *ctx, ScriptCallSeq<I...>)
   {
      return (((C *)ctx->getThis())->*inFunc)(
         ScriptCallArgOf< typename ScriptCallAt<I, A...>::Type >::get(ctx, ScriptCallAt<I, A...>::pos)... );
   }
   static inline R call(CppiaCtx *ctx) { return call(ctx, typename ScriptCallCount<sizeof...(A)>::Seq()); }
   static void CPPIA_CALL run(CppiaCtx *ctx) { ScriptCallReturn<R>::template run<ScriptCallStub>(ctx); }
   static const char signature[];
};
template<typename C, typename R, typename... A, R (C::*inFunc)(A...)>
const char ScriptCallStub<R (C::*)(A...), inFunc>::signature[] =
   { (char)ScriptCallReturn<R>::code, (char)ScriptCallArgOf<A>::code..., 0 };

template<typename R, typename... A, R (*inFunc)(A...)>
struct ScriptCallStub<R (*)(A...), inFunc>
{
   enum { usable = ScriptCallReturn<R>::usable && ScriptCallAll<A...>::ok, isStatic = 1 };

   template<int... I> static inline R call(CppiaCtx *ctx, ScriptCallSeq<I...>)
   {
      return inFunc(
         ScriptCallArgOf< typename ScriptCallAt<I, A...>::Type >::get(ctx, ScriptCallAt<I, A...>::pos)... );
   }
   static inline R call(CppiaCtx *ctx) { return call(ctx, typename ScriptCallCount<sizeof...(A)>::Seq()); }
   static void CPPIA_CALL run(CppiaCtx *ctx) { ScriptCallReturn<R>::template run<ScriptCallStub>(ctx); }
   static const char signature[];
};
template<typename R, typename... A, R (*inFunc)(A...)>
const char ScriptCallStub<R (*)(A...), inFunc>::signature[] =
   { (char)ScriptCallReturn<R>::code, (char)ScriptCallArgOf<A>::code..., 0 };


// --- Which classes want entries ---------------------------------------------------

template<typename T, typename U> struct ScriptCallSame { };
template<typename T> struct ScriptCallSame<T, T> { typedef int Yes; };

// A class of the program has a static __mClass; the runtime's own helpers do not
template<typename T> struct ScriptCallHasClass
{
   template<typename U> static char test( typename ScriptCallSame< decltype(&U::__mClass), hx::Class * >::Yes * );
   template<typename U> static long test(...);
   enum { value = sizeof(test<T>(0)) == sizeof(char) };
};

template<typename... T> struct ScriptCallVoid { typedef void Type; };

template<hx::ScriptFunction *A, hx::ScriptFunction *B> struct ScriptCallSameGlue { enum { value = 0 }; };
template<hx::ScriptFunction *A> struct ScriptCallSameGlue<A, A> { enum { value = 1 }; };

// The compiler declares __script_construct in every class it writes script functions
//  for. hx::Object has one, so every class has the name: a class with script functions
//  is one whose own is not the one its super class has.
template<typename T, typename = void> struct ScriptCallHasGlue { enum { value = 0 }; };
template<typename T>
struct ScriptCallHasGlue<T, typename ScriptCallVoid< decltype(&T::__script_construct), decltype(&T::super::__script_construct) >::Type >
{
   enum { value = !ScriptCallSameGlue< &T::__script_construct, &T::super::__script_construct >::value };
};

// What a function that cannot have an entry registers: nothing
struct ScriptCallNone
{
   constexpr ScriptCallNone(const char *) { }
};

// The signature is only looked at for a class that wants entries, so the classes that
//  have script functions already cost the compiler nothing here
template<bool inClassWanted, typename SIG, SIG inFunc> struct ScriptCallUsable { enum { value = 0 }; };
template<typename SIG, SIG inFunc> struct ScriptCallUsable<true, SIG, inFunc>
{
   enum { value = ScriptCallStub<SIG, inFunc>::usable };
};

template<typename C, typename SIG, SIG inFunc,
   bool inWanted = ScriptCallUsable< (ScriptCallHasClass<C>::value && !ScriptCallHasGlue<C>::value), SIG, inFunc >::value >
struct ScriptCallRegister
{
   constexpr ScriptCallRegister(const char *) { }
};

template<typename C, typename SIG, SIG inFunc>
struct ScriptCallRegister<C, SIG, inFunc, true>
{
   ScriptCallEntry entry;

   ScriptCallRegister(const char *inName)
      : entry(&C::__mClass, inName, &ScriptCallStub<SIG, inFunc>::run, ScriptCallStub<SIG, inFunc>::signature,
              ScriptCallStub<SIG, inFunc>::isStatic != 0) { }
};

} // end namespace hx

#endif
