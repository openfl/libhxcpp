#include <hxcpp.h>
#include "Cppia.h"

#if (HXCPP_API_LEVEL>=500)
#include <hx/thread/Thread.hpp>
#endif

namespace hx
{

template<> inline Array<unsigned char> &runValue(Array<unsigned char> &outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   outValue.mPtr = (Array_obj<unsigned char>*) expr->runObject(ctx);
   return outValue;
}

#ifdef CPPIA_JIT
// How a builtin's argument reaches its native call under the JIT: objects as
// pointers, ints by value and floats by the address of a temp
template<typename T>
struct BuiltinArg
{
   typedef hx::Object *Native;
   static const JitType jitType = jtPointer;
   static const ExprType exprType = etObject;
   static T get(hx::Object *inValue) { return Dynamic(inValue); }
};

template<>
struct BuiltinArg<int>
{
   typedef int Native;
   static const JitType jitType = jtInt;
   static const ExprType exprType = etInt;
   static int get(int inValue) { return inValue; }
};

template<>
struct BuiltinArg<double>
{
   typedef double *Native;
   static const JitType jitType = jtFloat;
   static const ExprType exprType = etFloat;
   static double get(double *inValue) { return JitLoad(inValue); }
};

template<>
struct BuiltinArg<float>
{
   typedef double *Native;
   static const JitType jitType = jtFloat;
   static const ExprType exprType = etFloat;
   static float get(double *inValue) { return JitLoad(inValue); }
};

template<>
struct BuiltinArg< Array<unsigned char> >
{
   typedef hx::Object *Native;
   static const JitType jitType = jtPointer;
   static const ExprType exprType = etObject;
   static Array<unsigned char> get(hx::Object *inValue)
   {
      Array<unsigned char> result;
      result.mPtr = (Array_obj<unsigned char>*)inValue;
      return result;
   }
};
#endif


template<typename ARG0, int (*FUNC)(ARG0 arg0)>
class IntBuiltin1 : public CppiaExpr
{
public:
   Expressions args;

   IntBuiltin1(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "IntBuiltin1"; }
   ExprType getType() HXCPP_OVERRIDE { return etInt; }

   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runInt(ctx); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE { return runInt(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runInt(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runInt(ctx)); }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      return FUNC(val0);
   }

   #ifdef CPPIA_JIT
   static int SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0)
   {
      TRY_NATIVE
      return FUNC(BuiltinArg<ARG0>::get(inArg0));
      CATCH_NATIVE
      return 0;
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      compiler->callNative( (void *)run, val0 );
      compiler->checkException();
      compiler->convertReturnReg(etInt, inDest, destType);
   }
   #endif
};


template<typename ARG0, typename ARG1, int (*FUNC)(ARG0 arg0, ARG1 arg1)>
class IntBuiltin2 : public CppiaExpr
{
public:
   Expressions args;

   IntBuiltin2(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "IntBuiltin2"; }
   ExprType getType() HXCPP_OVERRIDE { return etInt; }

   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runInt(ctx); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE { return runInt(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runInt(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runInt(ctx)); }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      ARG1 val1;
      runValue(val1, ctx, args[1]);
      BCR_CHECK;
      return FUNC(val0,val1);
   }

   #ifdef CPPIA_JIT
   static int SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0, typename BuiltinArg<ARG1>::Native inArg1)
   {
      TRY_NATIVE
      return FUNC(BuiltinArg<ARG0>::get(inArg0), BuiltinArg<ARG1>::get(inArg1));
      CATCH_NATIVE
      return 0;
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      JitTemp val1(compiler, BuiltinArg<ARG1>::jitType);
      args[1]->genCode(compiler, val1, BuiltinArg<ARG1>::exprType);

      compiler->callNative( (void *)run, val0, val1 );
      compiler->checkException();
      compiler->convertReturnReg(etInt, inDest, destType);
   }
   #endif
};



template<typename ARG0, void (*FUNC)(ARG0)>
class VoidBuiltin1 : public CppiaExpr
{
public:
   Expressions args;

   VoidBuiltin1(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "VoidBuiltin1"; }
   ExprType getType() HXCPP_OVERRIDE { return etVoid; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0;}
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return String(); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_VCHECK;
      FUNC(val0);
   }
};



template<typename ARG0, typename ARG1, void (*FUNC)(ARG0,ARG1)>
class VoidBuiltin2 : public CppiaExpr
{
public:
   Expressions args;

   VoidBuiltin2(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "VoidBuiltin2"; }
   ExprType getType() HXCPP_OVERRIDE { return etVoid; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0;}
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return String(); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_VCHECK;
      ARG1 val1;
      runValue(val1, ctx, args[1]);
      BCR_VCHECK;
      FUNC(val0,val1);
   }

   #ifdef CPPIA_JIT
   static void SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0, typename BuiltinArg<ARG1>::Native inArg1)
   {
      TRY_NATIVE
      FUNC(BuiltinArg<ARG0>::get(inArg0), BuiltinArg<ARG1>::get(inArg1));
      CATCH_NATIVE
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      JitTemp val1(compiler, BuiltinArg<ARG1>::jitType);
      args[1]->genCode(compiler, val1, BuiltinArg<ARG1>::exprType);

      compiler->callNative( (void *)run, val0, val1 );
      compiler->checkException();
   }
   #endif
};


template<typename ARG0, typename ARG1, typename ARG2, void (*FUNC)(ARG0,ARG1,ARG2)>
class VoidBuiltin3 : public CppiaExpr
{
public:
   Expressions args;

   VoidBuiltin3(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "VoidBuiltin3"; }
   ExprType getType() HXCPP_OVERRIDE { return etVoid; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0;}
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return 0; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { runVoid(ctx); return String(); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_VCHECK;
      ARG1 val1;
      runValue(val1, ctx, args[1]);
      BCR_VCHECK;
      ARG2 val2;
      runValue(val2, ctx, args[2]);
      BCR_VCHECK;
      FUNC(val0,val1,val2);
   }

   #ifdef CPPIA_JIT
   // Calls FUNC itself: the int setters share this class, and choosing a float
   // setter by the size of ARG2 stored their values as floats
   static void SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0, typename BuiltinArg<ARG1>::Native inArg1, typename BuiltinArg<ARG2>::Native inArg2)
   {
      TRY_NATIVE
      FUNC(BuiltinArg<ARG0>::get(inArg0), BuiltinArg<ARG1>::get(inArg1), BuiltinArg<ARG2>::get(inArg2));
      CATCH_NATIVE
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      JitTemp val1(compiler, BuiltinArg<ARG1>::jitType);
      args[1]->genCode(compiler, val1, BuiltinArg<ARG1>::exprType);

      JitTemp val2(compiler, BuiltinArg<ARG2>::jitType);
      args[2]->genCode(compiler, val2, BuiltinArg<ARG2>::exprType);

      compiler->callNative( (void *)run, val0, val1, val2 );
      compiler->checkException();
   }
   #endif

};


template<typename RET, RET (*FUNC)()>
class FloatBuiltin0 : public CppiaExpr
{
public:
   Expressions args;

   FloatBuiltin0(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "FloatBuiltin0"; }
   ExprType getType() HXCPP_OVERRIDE { return etFloat; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { return runFloat(ctx); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runFloat(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runFloat(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runFloat(ctx)); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      return  FUNC();
   }
   #ifdef CPPIA_JIT
   static void SLJIT_CALL run(double *outResult)
   {
      JitStore(outResult, FUNC());
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      if (destType==etFloat && isMemoryVal(inDest) )
      {
         compiler->callNative( (void *)run, inDest.as(jtFloat));
      }
      else
      {
         JitTemp temp(compiler,jtFloat);
         compiler->callNative( (void *)run, temp);
         compiler->convert(temp, etFloat, inDest, destType);
      }
   }
  #endif
};

template<typename ARG0, typename RET, RET (*FUNC)(ARG0)>
class FloatBuiltin1 : public CppiaExpr
{
public:
   Expressions args;

   FloatBuiltin1(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "FloatBuiltin1"; }
   ExprType getType() HXCPP_OVERRIDE { return etFloat; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { return runFloat(ctx); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runFloat(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runFloat(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runFloat(ctx)); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      return  FUNC(val0);
   }

   #ifdef CPPIA_JIT
   static void SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0, double *outResult)
   {
      TRY_NATIVE
      JitStore(outResult, (double)FUNC(BuiltinArg<ARG0>::get(inArg0)));
      CATCH_NATIVE
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      JitTemp result(compiler, jtFloat);
      compiler->callNative( (void *)run, val0, result );
      compiler->checkException();
      compiler->convert(result, etFloat, inDest, destType);
   }
   #endif
};



template<typename ARG0, typename ARG1, typename RET, RET (*FUNC)(ARG0,ARG1)>
class FloatBuiltin2 : public CppiaExpr
{
public:
   Expressions args;

   FloatBuiltin2(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "FloatBuiltin2"; }
   ExprType getType() HXCPP_OVERRIDE { return etFloat; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { return runFloat(ctx); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runFloat(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runFloat(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runFloat(ctx)); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      ARG1 val1;
      runValue(val1, ctx, args[1]);
      BCR_CHECK;
      return  FUNC(val0,val1);
   }

   #ifdef CPPIA_JIT
   static void SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0, typename BuiltinArg<ARG1>::Native inArg1, double *outResult)
   {
      TRY_NATIVE
      JitStore(outResult, (double)FUNC(BuiltinArg<ARG0>::get(inArg0), BuiltinArg<ARG1>::get(inArg1)));
      CATCH_NATIVE
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      JitTemp val1(compiler, BuiltinArg<ARG1>::jitType);
      args[1]->genCode(compiler, val1, BuiltinArg<ARG1>::exprType);

      JitTemp result(compiler, jtFloat);
      compiler->callNative( (void *)run, val0, val1, result );
      compiler->checkException();
      compiler->convert(result, etFloat, inDest, destType);
   }
   #endif
};


template<typename ARG0, typename ARG1, typename ARG2, typename RET, RET (*FUNC)(ARG0,ARG1,ARG2)>
class FloatBuiltin3 : public CppiaExpr
{
public:
   Expressions args;

   FloatBuiltin3(CppiaExpr *inSrc, Expressions &inArgs) : CppiaExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "FloatBuiltin3"; }
   ExprType getType() HXCPP_OVERRIDE { return etFloat; }

   int runInt(CppiaCtx *ctx) HXCPP_OVERRIDE { return runFloat(ctx); }
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE { runFloat(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE { return Dynamic(runFloat(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE { return String(runFloat(ctx)); }
   Float runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      ARG1 val1;
      runValue(val1, ctx, args[1]);
      BCR_CHECK;
      ARG2 val2;
      runValue(val2, ctx, args[2]);
      BCR_CHECK;
      return  FUNC(val0,val1,val2);
   }
};

#define MEMORY_INT(GETTER,SETTER) \
   if (function==HX_CSTRING( #GETTER ) ) \
   { \
      if (ioExpressions.size()==1) \
         return new IntBuiltin1<int, GETTER>(src,ioExpressions); \
      return new IntBuiltin2<Array<unsigned char>, int, GETTER>(src,ioExpressions); \
   } \
   if (function==HX_CSTRING( #SETTER ) ) \
   { \
      if (ioExpressions.size()==2) \
         return new VoidBuiltin2<int,int, SETTER>(src,ioExpressions); \
      return new VoidBuiltin3<Array<unsigned char>,int,int,SETTER>(src,ioExpressions); \
   }



template<typename RET, RET (*FUNC)()>
class ObjectBuiltin0 : public CppiaDynamicExpr
{
public:
   Expressions args;

   ObjectBuiltin0(CppiaExpr *inSrc, Expressions &inArgs) : CppiaDynamicExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "ObjectBuiltin0"; }
   ExprType getType() HXCPP_OVERRIDE { return etObject; }

   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      return Dynamic(FUNC()).mPtr;
   }
   #ifdef CPPIA_JIT
   static hx::Object * SLJIT_CALL run()
   {
      TRY_NATIVE
      return Dynamic(FUNC()).mPtr;
      CATCH_NATIVE
      return 0;
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      compiler->callNative( (void *)run );
      compiler->checkException();
      compiler->convertReturnReg(etObject, inDest, destType);
   }
   #endif
};



template<typename ARG0, typename RET, RET (*FUNC)(ARG0)>
class ObjectBuiltin1 : public CppiaDynamicExpr
{
public:
   Expressions args;

   ObjectBuiltin1(CppiaExpr *inSrc, Expressions &inArgs) : CppiaDynamicExpr(inSrc), args(inArgs) { }

   const char *getName() HXCPP_OVERRIDE { return "ObjectBuiltin1"; }
   ExprType getType() HXCPP_OVERRIDE { return etObject; }

   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE
   {
      ARG0 val0;
      runValue(val0, ctx, args[0]);
      BCR_CHECK;
      return  FUNC(val0).mPtr;
   }

   #ifdef CPPIA_JIT
   static hx::Object * SLJIT_CALL run(typename BuiltinArg<ARG0>::Native inArg0)
   {
      TRY_NATIVE
      return FUNC(BuiltinArg<ARG0>::get(inArg0)).mPtr;
      CATCH_NATIVE
      return 0;
   }

   void genCode(CppiaCompiler *compiler, const JitVal &inDest,ExprType destType) HXCPP_OVERRIDE
   {
      JitTemp val0(compiler, BuiltinArg<ARG0>::jitType);
      args[0]->genCode(compiler, val0, BuiltinArg<ARG0>::exprType);

      compiler->callNative( (void *)run, val0 );
      compiler->checkException();
      compiler->convertReturnReg(etObject, inDest, destType);
   }
   #endif
};



CppiaExpr *createGlobalBuiltin(CppiaExpr *src, String function, Expressions &ioExpressions )
{
   MEMORY_INT(__hxcpp_memory_get_byte, __hxcpp_memory_set_byte);
   MEMORY_INT(__hxcpp_memory_get_i32, __hxcpp_memory_set_i32);
   MEMORY_INT(__hxcpp_memory_get_ui32, __hxcpp_memory_set_ui32);
   MEMORY_INT(__hxcpp_memory_get_i16, __hxcpp_memory_set_i16);
   MEMORY_INT(__hxcpp_memory_get_ui16, __hxcpp_memory_set_ui16);

   if (function==HX_CSTRING("__hxcpp_memory_get_float") )
   {
      if (ioExpressions.size()==1)
         return new FloatBuiltin1<int,float,__hxcpp_memory_get_float>(src,ioExpressions);
      return new FloatBuiltin2<Array<unsigned char>,int,float,__hxcpp_memory_get_float>(src,ioExpressions);
   }
   if (function==HX_CSTRING("__hxcpp_memory_set_float") )
   {
      if (ioExpressions.size()==2)
         return new VoidBuiltin2<int,float,__hxcpp_memory_set_float>(src,ioExpressions);
      return new VoidBuiltin3<Array<unsigned char>,int,float,__hxcpp_memory_set_float>(src,ioExpressions);
   }

   if (function==HX_CSTRING("__hxcpp_memory_get_double") )
   {
      if (ioExpressions.size()==1)
         return new FloatBuiltin1<int,double,__hxcpp_memory_get_double>(src,ioExpressions);
      return new FloatBuiltin2<Array<unsigned char>,int,double,__hxcpp_memory_get_double>(src,ioExpressions);
   }
   if (function==HX_CSTRING("__hxcpp_memory_set_double") )
   {
      if (ioExpressions.size()==2)
         return new VoidBuiltin2<int,double,__hxcpp_memory_set_double>(src,ioExpressions);
      return new VoidBuiltin3<Array<unsigned char>,int,double,__hxcpp_memory_set_double>(src,ioExpressions);
   }
   if (function==HX_CSTRING("__time_stamp") )
   {
      if (ioExpressions.size()==0)
         return new FloatBuiltin0<double,__time_stamp>(src,ioExpressions);
   }
   if (function==HX_CSTRING("__time_stamp_ms") )
   {
      if (ioExpressions.size()==0)
         return new ObjectBuiltin0<cpp::Int64,__time_stamp_ms>(src,ioExpressions);
   }
   if (function==HX_CSTRING("__hxcpp_thread_create") )
   {
      if (ioExpressions.size()==1)
#if (HXCPP_API_LEVEL>=500)
          return new ObjectBuiltin1<hx::Callable<void(void)>, hx::thread::Thread, hx::thread::Thread_obj::create>(src, ioExpressions);
#else
          return new ObjectBuiltin1<Dynamic, Dynamic, __hxcpp_thread_create>(src, ioExpressions);
#endif
   }

   printf("Unknown function : %s(%d)\n", function.out_str(), (int)ioExpressions.size() );
   throw (HX_CSTRING("Unknown global:") + function).utf8_str();
   return 0;
}



} // end namespace hx
