#ifndef HX_CPPIA_INCLUDED_H
#define HX_CPPIA_INCLUDED_H

#include <hx/Scriptable.h>
#include <hx/GC.h>
#include <unordered_set>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <set>

#ifdef HX_ANDROID
  #include <android/log.h>
  #define CPPIALOG(...) __android_log_print(ANDROID_LOG_WARN, "cppia", __VA_ARGS__)
#else
  #define CPPIALOG printf
#endif



namespace hx
{
enum ExprType
{
  etVoid,
  etNull,
  etObject,
  etString,
  etFloat,
  etInt,
};


} // end namespace hx


#ifdef CPPIA_JIT
#include "CppiaCompiler.h"
#endif

namespace hx
{

class CppiaModule;

#define DBGLOG(...) { }
//#define DBGLOG printf

struct TypeData;
class CppiaClassInfo;
struct CppiaExpr;
struct CppiaStream;
struct CppiaStackVar;
struct StackLayout;

// The jit passes some values to native helpers by address, and the address can
//  be a frame slot or a jit temporary, which can be unaligned (see
//  StackContext::getSlot), so helpers read and write through these
template<typename T> inline T JitLoad(const T *inPtr) { return StackContext::getSlot<T>(inPtr); }
// Assigns as `*outPtr = inValue` would, with the same conversion
template<typename T, typename V> inline void JitStore(T *outPtr, const V &inValue)
{
   T value;
   value = inValue;
   StackContext::setSlot(outPtr, value);
}

// One argument of a block the jit passes to a native helper, copied out
template<typename A> inline A JitArg(const A *inArgs, int inIndex)
{
   return StackContext::getSlot<A>((const char *)inArgs + inIndex*sizeof(A));
}

struct ScriptCallable;
class  ScriptRegistered;
class  HaxeNativeClass;
class  HaxeNativeInterface;

enum CppiaOp
{
   #define CPPIA_OP(ident, name, val) ident = val,
   #include "CppiaOps.inc"
   #undef CPPIA_OP
};


enum ArrayType
{
   arrNotArray = 0,
   arrBool,
   arrInt,
   arrFloat,
   arrFloat32,
   arrUnsignedChar,
   arrString,
   arrObject,
   arrAny,
};

enum CrementOp
{
   coNone = -1,
   coPreInc,
   coPostInc,
   coPreDec,
   coPostDec,
};

enum AssignOp
{
   aoNone = -1,
   aoSet,
   aoAdd,
   aoMult,
   aoDiv,
   aoSub,
   aoAnd,
   aoOr,
   aoXOr,
   aoShl,
   aoShr,
   aoUShr,
   aoMod
};


enum VarLocation
{
   locObj,
   locThis,
   locStack,
   locAbsolute,
};

enum ArrayFunc
{
   afConcat,
   afCopy,
   afInsert,
   afIterator,
   afKeyValueIterator,
   afJoin,
   afPop,
   afPush,
   afContains,
   afRemove,
   afReverse,
   afShift,
   afSlice,
   afSplice,
   afSort,
   afToString,
   afUnshift,
   afMap,
   afFilter,
   afIndexOf,
   afLastIndexOf,
   af__get,
   af__set,
   af__crement,
   af__SetSizeExact,
   afBlit,
   afResize,
};


extern const char *gArrayFuncNames[];
extern int gArrayArgCount[];

typedef std::map<int,CppiaStackVar *> CppiaStackVarMap;

extern String sInvalidArgCount;

// Native objects a module creates while it loads come from its arena and go when it is freed.
// Deleting one runs its destructor; its memory stays with the module.
void *cppiaAlloc(size_t inSize, void (*inDestroy)(void *));
void cppiaFree(void *inPtr);

template<typename T>
struct CppiaOwned
{
   static void destroy(void *inPtr) { static_cast<T *>(inPtr)->~T(); }
   static void *operator new(size_t inSize) { return cppiaAlloc(inSize, destroy); }
   static void operator delete(void *inPtr) { cppiaFree(inPtr); }
};

struct CppiaExpr : public CppiaOwned<CppiaExpr>
{
   int line;
   const char *filename;
   const char *className;
   const char *functionName;
   int haxeTypeId;


   CppiaExpr() : line(0), filename(0), className(0), functionName(0)
   {
   }
   CppiaExpr(const CppiaExpr *inSrc) : line(0), filename(0), className(0), functionName(0)
   {
      if (inSrc)
      {
         line = inSrc->line;
         filename = inSrc->filename;
         className = inSrc->className;
         functionName = inSrc->functionName;
         haxeTypeId = inSrc->haxeTypeId;
      }
   }

   virtual ~CppiaExpr() {}

   virtual const char *getName() { return "CppiaExpr"; }
   virtual CppiaExpr   *link(CppiaModule &data)   { return this; }
   virtual void mark(hx::MarkContext *ctx) { };
   virtual void visit(hx::VisitContext *ctx) { };
   virtual bool isBoolInt() { return false; }
   // The line the debugger first sees when this runs as a function body
   virtual int firstLine() { return line; }

   // Constant-folding query - lets containers (eg, switch) build dispatch
   // tables at link time when every alternative is a literal int
   virtual bool getConstantInt(int &outValue) { return false; }


   virtual ExprType    getType() { return etObject; }

   virtual int         runInt(CppiaCtx *ctx)    { return runFloat(ctx); }
   virtual Float       runFloat(CppiaCtx *ctx) { return 0.0; }
   virtual ::String    runString(CppiaCtx *ctx)
   {
      hx::Object *result = runObject(ctx);
      if (!result)
         return null();
      return result->toString();
   }
   virtual hx::Object *runObject(CppiaCtx *ctx) { return Dynamic(runFloat(ctx)).mPtr; }
   virtual void        runVoid(CppiaCtx *ctx)   { runObject(ctx); }

   virtual CppiaExpr   *makeSetter(AssignOp op,CppiaExpr *inValue) { return 0; }
   virtual CppiaExpr   *makeCrement(CrementOp inOp) { return 0; }


   #ifdef CPPIA_JIT
   virtual void genCode(CppiaCompiler *compiler,const JitVal &inDest=JitVal(),ExprType type=etNull);
   // TODO - array of JumpIds
   virtual JumpId genCompare(CppiaCompiler *compiler,bool inReverse, LabelId inLabel=0);
   #endif
};

typedef std::vector<CppiaExpr *> Expressions;


struct CppiaDynamicExpr : public CppiaExpr
{
   inline CppiaDynamicExpr(const CppiaExpr *inSrc=0) : CppiaExpr(inSrc) {}
   const char *getName() HXCPP_OVERRIDE = 0;
   int         runInt(CppiaCtx *ctx) HXCPP_OVERRIDE;
   Float       runFloat(CppiaCtx *ctx) HXCPP_OVERRIDE;
   ::String    runString(CppiaCtx *ctx) HXCPP_OVERRIDE;
   void        runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE;
   hx::Object *runObject(CppiaCtx *ctx)  HXCPP_OVERRIDE= 0;
};


struct ArrayBuiltinBase : public CppiaExpr
{
   CppiaExpr *thisExpr;
   Expressions args;

   ArrayBuiltinBase(CppiaExpr *inSrc, CppiaExpr *inThisExpr, Expressions &ioExpressions);
   const char *getName() HXCPP_OVERRIDE;
   CppiaExpr *link(CppiaModule &inData) HXCPP_OVERRIDE;
};

CppiaExpr *createArrayAnyBuiltin(CppiaExpr *src,
                              CppiaExpr *inThisExpr, String field,
                              Expressions &ioExpressions );


struct CppiaConst
{
   enum Type { cInt, cFloat, cString, cBool, cNull, cThis, cSuper };

   Type type;
   int  ival;
   Float  dval;

   CppiaConst();

   void fromStream(CppiaStream &stream);
};



struct ScriptCallable : public CppiaDynamicExpr
{
   int returnTypeId;
   ExprType returnType;
   bool isBoolReturn;
   int argCount;
   int stackSize;
   bool hasDefaults;

   std::vector<CppiaStackVar> args;
   std::vector<bool>          hasDefault;
   std::vector<CppiaConst>    initVals;
   CppiaExpr *body;
   CppiaModule *data;
   // Static functions and main store their module in the frame's this slot, so running code keeps it
   bool staticEntry;
   #ifdef CPPIA_JIT
   CppiaFunc compiled;
   bool ownsCompiled;
   // Compiled when it is first called rather than when its module loads
   bool lazy;
   #endif

   #ifdef HXCPP_STACK_SCRIPTABLE
   CppiaStackVarMap varMap;
   #endif

   hx::StackPosition          position;

   std::vector<CppiaStackVar *> captureVars;
   int                          captureSize;


   ScriptCallable(CppiaStream &stream);
   ScriptCallable(CppiaExpr *inBody);
   ScriptCallable(CppiaModule &inModule,ScriptNamedFunction *inFunction);
   ~ScriptCallable();

   CppiaExpr *link(CppiaModule &inModule) HXCPP_OVERRIDE;

   #ifdef HXCPP_STACK_SCRIPTABLE
   void getScriptableVariables(unsigned char *inFrame, Array<Dynamic> outNames);
   bool getScriptableValue(unsigned char *inFrame, String inName, ::Dynamic &outValue);
   bool setScriptableValue(unsigned char *inFrame, String inName, ::Dynamic inValue);
   #endif

   static int Hash(int value, const char *inString);
   ExprType getReturnType() { return returnType; }
   ExprType getType() HXCPP_OVERRIDE { return etObject; }


   #ifdef CPPIA_JIT
   void compile();
   void compileLazy();
   void genDefaults(CppiaCompiler *compiler);
   #ifdef HXCPP_DEBUGGER
   void genZeroLocals(CppiaCompiler *compiler);
   #endif
   void genModuleAnchor(CppiaCompiler *compiler);
   #ifdef HXCPP_STACK_TRACE
   void genPushFrame(CppiaCompiler *compiler, int frameLocal);
   #endif
   void genArgs(CppiaCompiler *compiler, CppiaExpr *inThis, Expressions &inArgs, const JitVal &inThisVal);
   void genCode(CppiaCompiler *compiler,const JitVal &inDest=JitVal(),ExprType type=etNull) HXCPP_OVERRIDE;
   #endif


   void pushArgs(CppiaCtx *ctx, hx::Object *inThis, Expressions &inArgs);
   void pushArgsDynamic(CppiaCtx *ctx, hx::Object *inThis, Array<Dynamic> &inArgs);

   // Return the closure
   hx::Object *runObject(CppiaCtx *ctx) HXCPP_OVERRIDE;

   const char *getName() HXCPP_OVERRIDE;
   String runString(CppiaCtx *ctx) HXCPP_OVERRIDE;
   void runVoid(CppiaCtx *ctx) HXCPP_OVERRIDE;


   // Run the actual function
   void runFunction(CppiaCtx *ctx);
   void runFunctionClosure(CppiaCtx *ctx);
   void addStackVarsSpace(CppiaCtx *ctx);
   bool pushDefault(CppiaCtx *ctx,int arg);
   void addExtraDefaults(CppiaCtx *ctx,int inHave);
};



CppiaExpr *createCppiaExpr(CppiaStream &inStream);
CppiaExpr *createStaticAccess(CppiaExpr *inSrc, ExprType inType, void *inPtr);
CppiaExpr *createStaticAccess(CppiaExpr *inSrc, FieldStorage inType, void *inPtr);
hx::Object * CPPIA_CALL createClosure(CppiaCtx *ctx, ScriptCallable *inFunction);
hx::Object *createMemberClosure(hx::Object *, ScriptCallable *inFunction);
ScriptCallable *getClosureFunction(hx::Object *inObj);
CppiaClassInfo *getScriptClassInfo(hx::Object *inClass);
hx::Object *createEnumClosure(struct CppiaEnumConstructor &inContructor);

hx::Object *DynamicToArrayType(hx::Object *obj, ArrayType arrayType);


struct TypeData : public CppiaOwned<TypeData>
{
   String              name;
   hx::Class           haxeClass;
   CppiaClassInfo      *cppiaClass;
   ExprType            expressionType;
   HaxeNativeClass     *haxeBase;
   HaxeNativeInterface *interfaceBase;
   bool                linked;
   bool                isInterface;
   bool                isDynamic;
   bool                isFloat;
   ArrayType           arrayType;

   TypeData(String inData);

   bool isClassOf(Dynamic inInstance);
   void mark(hx::MarkContext *__inCtx);
   void visit(hx::VisitContext *__inCtx);
   void link(CppiaModule &inData);
};



class CppiaModule
{
public:
   Array< String >                 strings;
   //std::vector< std::string >      cStrings;
   std::vector< TypeData * >       types;
   std::vector< CppiaClassInfo * > classes;
   std::vector< CppiaExpr * >      markable;
   std::unordered_set<int>           allFileIds;
   typedef std::map< std::string, int > InterfaceSlots;
   InterfaceSlots                  interfaceSlots;

   StackLayout                     *layout;
   CppiaClassInfo                  *linkingClass;
   const char                      *creatingClass;
   const char                      *creatingFunction;
   int                             scriptId;

   // What the script uses that the host does not have, found while linking.
   //  Linking goes on past each one, so the load fails once, naming them all
   std::vector< std::string >      linkMisses;

   ScriptCallable                  *main;
   std::vector< hx::Resource >     resources;

   // The module's CppiaObject: what keeps it alive once released
   hx::Object                      *owner;
   bool                            released;

   struct ArenaChunk { char *base; size_t size; size_t used; };
   std::vector<ArenaChunk>         arena;

   CppiaModule();
   ~CppiaModule();

   void *arenaAlloc(size_t inSize);
   bool ownsMemory(const void *inPtr) const;
   void release();

   void link();
   void compile();
   void markLazy();
   void setDebug(CppiaExpr *outExpr, int inFileId, int inLine);
   void boot(CppiaCtx *ctx);
   void where(CppiaExpr *e);
   void linkMiss(CppiaExpr *inWhere, const std::string &inWhat);
   void mark(hx::MarkContext *ctx);
   void visit(hx::VisitContext *ctx);
   int  getInterfaceSlot(const std::string &inName);
   int  findInterfaceSlot(const std::string &inName);
   CppiaClassInfo *findClass( ::String inName );
   void registerDebugger();

   inline const char *identStr(int inId) { return strings[inId].raw_ptr(); }
   inline const char *typeStr(int inId) { return types[inId]->name.c_str(); }
};


struct StackLayout
{
   std::map<int,CppiaStackVar *> varMap;
   std::vector<CppiaStackVar *>  captureVars;
   StackLayout                   *parent;
   ExprType                      returnType;
   int                           captureSize;
   int                           size;

   StackLayout(StackLayout *inParent);

   void dump(Array<String> &inStrings, std::string inIndent);
   CppiaStackVar *findVar(int inId);
};


struct ArgInfo
{
   int  nameId;
   bool optional;
   int  typeId;
};


struct CppiaFunction : public CppiaOwned<CppiaFunction>
{
   CppiaModule          &cppia;
   int                  nameId;
   bool                 isStatic;
   bool                 isDynamic;
   int                  returnType;
   int                  argCount;
   int                  vtableSlot;
   bool                 linked;
   std::string          name;
   std::vector<ArgInfo> args;
   ScriptCallable       *funExpr;
   // The body as it came. A class's __init__ is an expression of any kind,
   //  and an abstract function's a bare RETURN: neither is a ScriptCallable,
   //  and neither has a funExpr
   CppiaExpr            *body;

   CppiaFunction(CppiaModule *inCppia,bool inIsStatic,bool inIsDynamic);

   inline void setVTableSlot(int inSlot) { vtableSlot = inSlot; }
   inline String getName() { return cppia.strings[nameId]; }
   void load(CppiaStream &stream,bool inExpectBody);
   void link( );
   void compile();
   void markLazy();
};



struct CppiaStackVar : public CppiaOwned<CppiaStackVar>
{
   int      nameId;
   int      id;
   bool     capture;
   int      typeId;
   int      stackPos;
   int      fromStackPos;
   int      capturePos;
   TypeData *type;
   FieldStorage storeType;
   ExprType expressionType;
   CppiaModule *module;

   int      defaultStackPos;
   ExprType argType;

   CppiaStackVar();
   CppiaStackVar(CppiaStackVar *inVar,int &ioSize, int &ioCaptureSize);

   void fromStream(CppiaStream &stream);
   void set(CppiaCtx *inCtx,Dynamic inValue);
   void setInFrame(unsigned char *inFrame,Dynamic inValue);
   Dynamic getInFrame(const unsigned char *inFrame);
   void markClosure(char *inBase, hx::MarkContext *__inCtx);
   void visitClosure(char *inBase, hx::VisitContext *__inCtx);
   void link(CppiaModule &inModule, bool hasDefault=false);
   void linkDefault();
   void setDefault(CppiaCtx *inCxt, const CppiaConst &inDefault);

   #ifdef CPPIA_JIT
   void genDefault(CppiaCompiler *compiler, const CppiaConst &inDefault);
   #endif

};



int getStackVarNameId(int inVarId);

hx::Object *ObjectToInterface(hx::Object *inObject, TypeData *toType);

void LinkExpressions(Expressions &ioExpressions, CppiaModule &data);

CppiaExpr *createEnumField(CppiaStream &stream,bool inWithArgs);

int TypeAlignment(ExprType type);
void AlignOffset(ExprType type, int &ioOffset);

struct CppiaVar : public CppiaOwned<CppiaVar>
{
   enum Access { accNormal, accNo, accResolve, accCall, accRequire, accCallNative } ;

   TypeData         *type;
   bool             isStatic;
   bool             isVirtual;
   Access           readAccess;
   Access           writeAccess;
   int              nameId;
   int              typeId;
   int              offset;
   String           name;
   FieldStorage     storeType;
   CppiaFunction    *dynamicFunction;
   ExprType         exprType;

   CppiaExpr        *init;

   Dynamic          objVal;
   union {
      int              boolVal;
      int              byteVal;
      int              intVal;
      Float            floatVal;
   };
   String           stringVal;

   void             *valPointer;


   CppiaVar(bool inIsStatic);
   CppiaVar(CppiaFunction *inDynamicFunction);

   void clear();
   void load(CppiaStream &stream);
   inline ExprType getType() { return exprType; }
   void linkVarTypes(CppiaModule &cppia);
   Dynamic getStaticValue();
   Dynamic setStaticValue(Dynamic inValue);
   CppiaExpr *createAccess(CppiaExpr *inSrc);
   void linkVarTypes(CppiaModule &cppia, int &ioOffset);
   int fieldAlignment(CppiaModule &cppia);
   void createDynamic(hx::Object *inBase);
   Dynamic getValue(hx::Object *inThis);
   Dynamic setValue(hx::Object *inThis, Dynamic inValue);
   void link(CppiaModule &inModule);
   static Access getAccess(CppiaStream &stream);
   void runInit(CppiaCtx *ctx);
   bool hasPointer();

   inline void mark(hx::Object *inThis,hx::MarkContext *__inCtx)
   {
      switch(storeType)
      {
         case fsString: HX_MARK_MEMBER(*(String *)((char *)inThis + offset)); break;
         case fsObject: HX_MARK_MEMBER(*(Dynamic*)((char *)inThis + offset)); break;
         default:;
      }
   }
   inline void visit(hx::Object *inThis,hx::VisitContext *__inCtx)
   {
      switch(storeType)
      {
         case fsString: HX_VISIT_MEMBER(*(String *)((char *)inThis + offset)); break;
         case fsObject: HX_VISIT_MEMBER(*(Dynamic *)((char *)inThis + offset)); break;
         default:;
      }
   }


   void mark(hx::MarkContext *__inCtx);
   void visit(hx::VisitContext *__inCtx);
};



#if (HXCPP_API_LEVEL >= 500)
# define NATIVE_CLASS_OVERRIDES_MARKED
#endif

class HaxeNativeClass
{
public:
   std::string  name;
   hx::ScriptableClassFactory factory;
   hx::ScriptNamedFunction  construct;
   ScriptNamedFunction *functions;
   HaxeNativeClass *haxeSuper;
   int mDataOffset;

   HaxeNativeClass(const std::string &inName, int inDataOffset, ScriptNamedFunction *inFunctions, hx::ScriptableClassFactory inFactory, ScriptNamedFunction inConstruct);

   void addVtableEntries( std::vector<std::string> &outVtable);
   void dump();
   ScriptNamedFunction findFunction(const std::string &inName);
   ScriptNamedFunction findStaticFunction(String inName);

   static HaxeNativeClass *findClass(const std::string &inName);
   static HaxeNativeClass *hxObject();
   static void link();
   static const std::string &hostFingerprint();
#ifndef NATIVE_CLASS_OVERRIDES_MARKED
private:
   void addVtableEntries( std::vector<std::string> &outVtable, std::unordered_set<std::string> &outMethodsSet);
#endif
};

class HaxeNativeInterface
{
public:
   std::string  name;
   ScriptNamedFunction *functions;

   void *scriptTable;

   HaxeNativeInterface(const std::string &inName, ScriptNamedFunction *inFunctions,void *inScriptTable);

   ScriptFunction findFunction(const std::string &inName);

   static HaxeNativeInterface *findInterface(const std::string &inName);
};

typedef std::vector<CppiaFunction *> Functions;
typedef std::map<std::string, ScriptCallable *> FunctionMap;

class CppiaClass;

void runFunExpr(CppiaCtx *ctx, ScriptCallable *inFunExpr, hx::Object *inThis, Expressions &inArgs );
hx::Object *runFunExprDynamic(CppiaCtx *ctx, ScriptCallable *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs );
void runFunExprDynamicVoid(CppiaCtx *ctx, ScriptCallable *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs );

hx::Class_obj *createCppiaClass(CppiaClassInfo *);


class CppiaClassInfo : public CppiaOwned<CppiaClassInfo>
{
public:

   CppiaModule &cppia;
   std::vector<int> implements;
   bool      isInterface;
   bool      isLinked;
   bool      isEnum;
   int       typeId;
   TypeData  *type;
   int       superId;
   TypeData *superType;
   int       classSize;
   int       extraData;
   bool      containsPointers;
   int       dynamicMapOffset;
   int       interfaceSlotSize;
   void      **vtable;
   std::string name;
   std::map<int, void *> interfaceScriptTables;
   std::vector<ScriptNamedFunction *> nativeInterfaceFunctions;
   // Fill host interface slots whose signature differs from the script function
   std::vector<ScriptCallable *> interfaceAdapters;
   std::set<String> nativeProperties;
   hx::Class     mClass;

   HaxeNativeClass *haxeBase;
   void            *haxeBaseVTable;

   Functions memberFunctions;
   FunctionMap memberGetters;
   FunctionMap memberSetters;
   std::vector<CppiaVar *> memberVars;
   std::vector<CppiaVar *> dynamicFunctions;

   // One-probe member lookup built at link time - getField/setField
   // previously scanned functions, dynamic functions and vars linearly
   // with a string compare per entry on every dynamic access.  Keys are
   // the module's permanent strings, so they are GC-safe to hold here.
   enum MemberFieldKind { mfFunction, mfDynamicFunction, mfVar };
   struct MemberFieldEntry { int kind; void *ptr; };
   struct MemberNameHash { size_t operator()(const String &inS) const { return inS.hash(); } };
   struct MemberNameEq { bool operator()(const String &inA, const String &inB) const { return inA==inB; } };
   typedef std::unordered_map<String,MemberFieldEntry,MemberNameHash,MemberNameEq> MemberFieldLookup;
   MemberFieldLookup memberFieldLookup;

   Functions staticFunctions;
   FunctionMap staticGetters;
   FunctionMap staticSetters;
   std::vector<CppiaVar *> staticVars;
   std::vector<CppiaVar *> staticDynamicFunctions;

   std::vector<CppiaEnumConstructor *> enumConstructors;

   CppiaFunction  *newFunc;
   CppiaExpr      *initExpr;
   CppiaExpr      *enumMeta;


public:

   CppiaClassInfo(CppiaModule &inCppia);
   bool load(CppiaStream &inStream);
   hx::Class *getSuperClass();
   inline hx::Class getClass() { return mClass; }

   hx::Object *createInstance(CppiaCtx *ctx,Expressions &inArgs, bool inCallNew = true);
   hx::Object *createInstance(CppiaCtx *ctx,Array<Dynamic> &inArgs);

   void init(CppiaCtx *ctx, int inPhase);
   void addMemberFunction(Functions &ioCombined, CppiaFunction *inNewFunc);
   inline void createDynamicFunctions(hx::Object *inThis)
   {
      for(int d=0;d<dynamicFunctions.size();d++)
         dynamicFunctions[d]->createDynamic(inThis);
   }


   #ifdef CPPIA_JIT
   void compile();
   void markLazy();
   #endif

   void link();
   void linkTypes();


   inline bool isNativeProperty(const String &inString);
   int __GetType();
   int getEnumIndex(String inName);
   bool implementsInterface(CppiaClassInfo *inInterface);
   ScriptCallable *findFunction(bool inStatic,int inId);
   ScriptCallable *findInterfaceFunction(const std::string &inName);
   ScriptCallable *findFunction(bool inStatic, const String &inName);
   ScriptCallable *findFunction(FunctionMap &inMap,const String &inName);
   inline ScriptCallable *findMemberGetter(const String &inName)
      { return findFunction(memberGetters,inName); }
   inline ScriptCallable *findMemberSetter(const String &inName)
      { return findFunction(memberSetters,inName); }
   inline ScriptCallable *findStaticGetter(const String &inName)
      { return findFunction(staticGetters,inName); }
   inline ScriptCallable *findStaticSetter(const String &inName)
      { return findFunction(staticSetters,inName); }
   CppiaEnumConstructor *findEnum(int inFieldId);
   ExprType findFunctionType(CppiaModule &inModule, int inName);
   CppiaFunction *findVTableFunction(int inId);
   int findFunctionSlot(int inName);
   CppiaVar *findVar(bool inStatic,int inId);

   void *getHaxeBaseVTable();
   int getScriptVTableOffset();

   Dynamic getStaticValue(const String &inName,hx::PropertyAccess  inCallProp);
   bool hasStaticValue(const String &inName);
   Dynamic setStaticValue(const String &inName,const Dynamic &inValue ,hx::PropertyAccess  inCallProp);
   void GetInstanceFields(hx::Object *inObject, Array<String> &ioFields);
   Array<String> GetClassFields();

   bool getField(hx::Object *inThis, String inName, hx::PropertyAccess  inCallProp, Dynamic &outValue);
   bool setField(hx::Object *inThis, String inName, Dynamic inValue, hx::PropertyAccess  inCallProp, Dynamic &outValue);

   void dumpVars(const char *inMessage, std::vector<CppiaVar *> &vars);
   void dumpFunctions(const char *inMessage, std::vector<CppiaFunction *> &funcs);
   void dump();

   void mark(hx::MarkContext *__inCtx);
   void markInstance(hx::Object *inThis, hx::MarkContext *__inCtx);
   void release();

   #ifdef HXCPP_VISIT_ALLOCS
   void visit(hx::VisitContext *__inCtx);
   void visitInstance(hx::Object *inThis, hx::VisitContext *__inCtx);
   #endif

   inline Dynamic &getFieldMap(hx::Object *inThis)
   {
      return *(Dynamic *)( (char *)inThis + dynamicMapOffset );
   }



};





#ifdef HXCPP_STACK_LINE
   #ifdef HXCPP_DEBUGGER
      #define CPPIA_STACK_LINE(expr) \
          ctx->getCurrentStackFrame()->lineNumber = expr->line; \
          if (hx::gShouldCallHandleBreakpoints.load(std::memory_order_relaxed)) \
              __hxcpp_on_line_changed(ctx);
   #else
      #define CPPIA_STACK_LINE(expr) ctx->getCurrentStackFrame()->lineNumber = expr->line;
   #endif
#else
   #define CPPIA_STACK_LINE(expr)
#endif

#ifdef HXCPP_STACK_TRACE
   struct CppiaStackFrame
   {
      hx::StackContext *ctx;
      hx::StackFrame *frame;
      // The script stack only keeps 4-byte alignment
      alignas(hx::StackFrame) unsigned char storage[sizeof(hx::StackFrame)];
      CppiaStackFrame(hx::StackContext *inCtx, hx::StackPosition *inPosition)
      {
         ctx = inCtx;
         frame = (hx::StackFrame *)storage;
         frame->position = inPosition;
         #ifdef HXCPP_STACK_SCRIPTABLE
         frame->scriptStack = ctx->pointer;
         #endif
         #ifdef HXCPP_DEBUGGER
         frame->variables = 0;
         frame->catchables = 0;
         #endif
         inCtx->pushFrame(frame);
      }
      ~CppiaStackFrame()
      {
         ctx->popFrame(frame);
      }
   };

   #define CPPIA_STACK_FRAME(expr) hx::CppiaStackFrame __frame(ctx,&expr->position);
#else
   #define CPPIA_STACK_FRAME(expr)
#endif


#ifdef HXCPP_CHECK_POINTER
   #ifdef HXCPP_GC_CHECK_POINTER
      #define CPPIA_CHECK(obj) if (!obj) NullReference("Object", false);  GCCheckPointer(obj);
   #else
      #define CPPIA_CHECK(obj) if (!obj) NullReference("Object", false);
   #endif
#else
   #define CPPIA_CHECK(obj) { }
#endif

#ifdef HXCPP_CHECK_POINTER
   #define CPPIA_CHECK_FUNC(obj) if (!obj) Dynamic::ThrowBadFunctionError(); GCCheckPointer(obj);
#else
   #define CPPIA_CHECK_FUNC(obj) if (!obj) Dynamic::ThrowBadFunctionError();
#endif



struct BCRReturn
{
   inline BCRReturn() {}
   inline operator hx::Object *() const {  return 0; }
   inline operator bool() const {  return false; }
   inline operator int() const {  return 0; }
   inline operator unsigned char() const {  return 0; }
   inline operator char() const {  return 0; }
   inline operator short() const {  return 0; }
   inline operator unsigned short() const {  return 0; }
   inline operator float() const {  return 0; }
   inline operator double() const {  return 0; }
   inline operator Dynamic() const {  return null(); }
   inline operator String() const {  return String(); }
};


// Compiled code keeps a pending exception in StackContext::exception, where 0
//  means there is none, so a thrown null waits there as gThrownNull. A catch
//  that takes it, or a return to code that throws it again, gets null back
extern hx::Object *const gThrownNull;
inline hx::Object *toPendingException(hx::Object *inThrown) { return inThrown ? inThrown : gThrownNull; }
inline hx::Object *fromPendingException(hx::Object *inPending) { return inPending == gThrownNull ? 0 : inPending; }


#define BCR_CHECK if (ctx->breakContReturn || ctx->exception) return BCRReturn();
#define BCR_CHECK_RET(x) if (ctx->breakContReturn || ctx->exception) return x;
#define BCR_VCHECK if (ctx->breakContReturn || ctx->exception) return;




CppiaExpr *createArrayBuiltin(CppiaExpr *inSrc, ArrayType inType, CppiaExpr *inThisExpr,
                              String inField, Expressions &ioExpressions );
CppiaExpr *createStringBuiltin(CppiaExpr *src, CppiaExpr *inThisExpr, String field, Expressions &ioExpressions );

CppiaExpr *createGlobalBuiltin(CppiaExpr *src, String function, Expressions &ioExpressions );

template<typename T>
inline T &runValue(T& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   expr->runVoid(ctx);
   return null();
}

#if (HXCPP_API_LEVEL>=500)
template<typename... TArgs>
inline hx::Callable<void(TArgs...)>& runValue(hx::Callable<void(TArgs...)>& outValue, CppiaCtx* ctx, CppiaExpr* expr)
{
   // Wraps the function the expression gives; a null Callable here left
   // __hxcpp_thread_create without a function to run
   return outValue = hx::Callable<void(TArgs...)>(Dynamic(expr->runObject(ctx)));
}
#endif

template<> inline int &runValue(int& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runInt(ctx);
}


template<> inline unsigned char &runValue(unsigned char& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runInt(ctx);
}

template<> inline bool &runValue(bool& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runInt(ctx);
}


template<> inline double &runValue(double& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runFloat(ctx);
}


template<> inline float &runValue(float& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runFloat(ctx);
}

template<> inline String &runValue(String& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runString(ctx);
}

template<> inline hx::Object * &runValue(hx::Object *& outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runObject(ctx);
}

template<> inline Dynamic &runValue(Dynamic & outValue, CppiaCtx *ctx, CppiaExpr *expr)
{
   return outValue = expr->runObject(ctx);
}




inline static int ValToInt( bool v ) { return v; }
inline static int ValToInt( int v ) { return v; }
inline static int ValToInt( const Float &v ) { return v; }
inline static int ValToInt( const String &v ) { return 0; }
inline static int ValToInt( hx::Object *v ) { return v ? v->__ToInt() : 0; }
inline static int ValToInt( const Dynamic &v ) { return v.mPtr ? v->__ToInt() : 0; }

inline static Float ValToFloat( const bool &v ) { return v; }
inline static Float ValToFloat( const int &v ) { return v; }
inline static Float ValToFloat( const unsigned char &v ) { return v; }
inline static Float ValToFloat( const Float &v ) { return v; }
inline static Float ValToFloat( const String &v ) { return 0; }
inline static Float ValToFloat( hx::Object *v ) { return v ? v->__ToDouble() : 0; }
inline static Float ValToFloat( const Dynamic &v ) { return v.mPtr ? v->__ToDouble() : 0.0; }

inline static String ValToString( const bool &v ) { return v; }
inline static String ValToString( const int &v ) { return String(v); }
inline static String ValToString( const unsigned char &v ) { return String(v); }
inline static String ValToString( const Float &v ) { return String(v); }
inline static String ValToString( const String &v ) { return v; }
inline static String ValToString( hx::Object *v ) { return v ? ((hx::Object *)v)->toString() : String(); }
inline static String ValToString( const Dynamic &v ) { return v.mPtr ? v.mPtr->toString() : String(); }

template<typename T>
struct ExprTypeOf { enum { value = etObject }; };
template<> struct ExprTypeOf<int> { enum { value = etInt }; };
template<> struct ExprTypeOf<unsigned char> { enum { value = etInt }; };
template<> struct ExprTypeOf<bool> { enum { value = etInt }; };
template<> struct ExprTypeOf<Float> { enum { value = etFloat }; };
template<> struct ExprTypeOf<float> { enum { value = etFloat }; };
template<> struct ExprTypeOf<String> { enum { value = etString }; };


#ifdef CPPIA_JIT
void genNullReferenceExceptionCheck(CppiaCompiler *compiler, const JitVal &reg);

// Loads the object a field holds when the code runs. A moving collection
// updates the field, while an address written into the code would go stale.
inline void genLoadHeldObject(CppiaCompiler *compiler, const JitVal &inDest, hx::Object **inHolder)
{
   compiler->move( sJitTemp0, JitVal( (void *)inHolder ) );
   compiler->move( inDest, sJitTemp0.star(jtPointer,0) );
}
#endif

template<typename T>
struct ExprTypeIsBool { enum { value = false }; };
template<> struct ExprTypeIsBool<bool> { enum { value = true }; };


struct NoCrement
{
   enum { OP = coNone };
   template<typename T>
   static inline T run(const T&inVal) { return inVal; }
};

struct CrementPreInc
{
   enum { OP = hx::coPreInc };
   template<typename T>
   static T run(T&inVal) { return ++inVal; }
   static hx::Object *run(hx::Object *&inVal) { return inVal = Dynamic(Dynamic(inVal) + 1).mPtr; }
   static bool run(bool &inVal) { return inVal; }
   static String run(String &inVal) { return inVal; }
};

struct CrementPostInc
{
   enum { OP = hx::coPostInc };
   template<typename T>
   static T run(T& inVal) { return inVal++; }
   static hx::Object *run(hx::Object *&inVal)
   {
      hx::Object *result(inVal);
      inVal = Dynamic(Dynamic(inVal) + 1).mPtr;
      return result;
   }
   static bool run(bool &inVal) { return inVal; }
   static String run(String &inVal) { return inVal; }
};

struct CrementPreDec
{
   enum { OP = hx::coPreDec };
   template<typename T>
   static T run(T&inVal) { return --inVal; }
   static bool run(bool &inVal) { return inVal; }
   static String run(String &inVal) { return inVal; }
   static hx::Object *run(hx::Object *&inVal) { return inVal = Dynamic(Dynamic(inVal) - 1).mPtr; }
};

struct CrementPostDec
{
   enum { OP = hx::coPostDec };
   template<typename T>
   static T run(T& inVal) { return inVal--; }
   static bool run(bool &inVal) { return inVal; }
   static String run(String &inVal) { return inVal; }
   static hx::Object *run(hx::Object *&inVal)
   {
      hx::Object *result(inVal);
      inVal = Dynamic(Dynamic(inVal) - 1).mPtr;
      return result;
   }

};



struct AssignSet
{
   enum { op = aoSet };

   template<typename T>
   static T run(T& ioVal, CppiaCtx *ctx, CppiaExpr *value )
   {
      T val;
      runValue(val, ctx, value );
      BCR_CHECK;
      return ioVal = val;
   }
   template<typename T, typename V> static void apply( T &ioVal, V &val) {  }
};

template<typename T> inline void AssignDouble(T &value, double d) { value = d; }
inline void AssignDouble(bool &value, double d) { }
inline void AssignDouble(String &value, double d) { }

template<typename T> inline void AssignInt(T &value, int i) { value = i; }
inline void AssignInt(bool &value, int i) { }
inline void AssignInt(String &value, int i) { }


template<typename T> inline void AssignString(T &value, const String &s) {  }
inline void AssignString(String &value, const String &s) { value = s; }
inline void AssignString(Dynamic &value, const String &s) { value = s; }

struct AssignAdd
{
   enum { op = aoAdd };

   template<typename T>
   static T run(T &ioVal, CppiaCtx *ctx, CppiaExpr *value)
   {
      T left = ioVal;
      T rhs;
      runValue(rhs,ctx,value);
      BCR_CHECK;
      ioVal = left + rhs;
      return ioVal;
   }
   static bool run(bool &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static hx::Object *run(hx::Object * &ioVal, CppiaCtx *ctx, CppiaExpr *value)
   {
      Dynamic left = ioVal;
      Dynamic rhs(value->runObject(ctx));
      BCR_CHECK;
      ioVal = ( left + rhs).mPtr;
      return ioVal;
   }
   static hx::Object *run(Dynamic &ioVal, CppiaCtx *ctx, CppiaExpr *value)
   {
      Dynamic left = ioVal;
      Dynamic rhs(value->runObject(ctx));
      BCR_CHECK;
      ioVal = left + rhs;
      return ioVal.mPtr;
   }
   template<typename T> static
   void apply( T &ioVal, const double &val) { AssignDouble( ioVal, ValToFloat(ioVal) + val ); }

   template<typename T> static
   void apply( T &ioVal, const int    &val) { AssignInt( ioVal, ValToInt(ioVal) + val ); }

   template<typename T> static
   void apply( T &ioVal, const String &val) { AssignString( ioVal, ValToString(ioVal) + val ); }

   template<typename T> static
   void apply( T &ioVal, Dynamic &val) { ioVal = Dynamic(ioVal) + val; }
};


#define DECL_STRUCT_ASSIGN(NAME,OPEQ,OP,ao) \
struct NAME \
{ \
   enum op { op = ao }; \
   template<typename T> \
   inline static T &run(T &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      T left = ioVal; \
      Float f = value->runFloat(ctx); \
      BCR_CHECK_RET(ioVal); \
      ioVal  = left OP f; \
      return ioVal; \
   } \
   inline static int &run(int &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      int left = ioVal; \
      int i = value->runInt(ctx); \
      BCR_CHECK_RET(ioVal); \
      ioVal  = left OP i; \
      return ioVal; \
   } \
   static bool run(bool &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static String run(String &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static hx::Object *run(hx::Object * &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      Dynamic left = ioVal; \
      Float f =  value->runFloat(ctx); \
      BCR_CHECK_RET(ioVal); \
      ioVal = Dynamic(left OP f).mPtr; \
      return ioVal; \
   } \
   static Dynamic &run(Dynamic &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      Dynamic left = ioVal; \
      Float f = value->runFloat(ctx); \
      BCR_CHECK_RET(ioVal); \
      ioVal = left OP f; \
      return ioVal; \
   } \
   template<typename T> static \
   void apply( T &ioVal, double val) { AssignDouble(ioVal, ValToFloat(ioVal) OP val); } \
   template<typename T> static \
   void apply( T &ioVal, String &val) { } \
   template<typename T> static \
   void apply( T &ioVal, int &val) { } \
   template<typename T> static \
   void apply( T &ioVal, Dynamic &val) { } \
};

DECL_STRUCT_ASSIGN(AssignMult,*=,*,aoMult)
DECL_STRUCT_ASSIGN(AssignSub,-=,-,aoSub)
DECL_STRUCT_ASSIGN(AssignDiv,/=,/,aoDiv)

#define DECL_STRUCT_ASSIGN_FUNC(NAME,OP_FUNC,RUN_FUNC,TMP,ao) \
struct NAME \
{ \
   enum op { op = ao }; \
   template<typename T> \
   static T run(T &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      T left = ioVal; \
      TMP t = value->RUN_FUNC(ctx); \
      BCR_CHECK; \
      ioVal = OP_FUNC(left, t); \
      return ioVal; \
   } \
   static bool run(bool &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static String run(String &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static hx::Object *run(hx::Object * &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      Dynamic left = ioVal; \
      TMP t = value->RUN_FUNC(ctx); \
      BCR_CHECK; \
      ioVal = Dynamic( OP_FUNC(left,t )).mPtr; \
      return ioVal; \
   } \
   template<typename T> static \
   void apply( T &ioVal, int &i) { AssignInt(ioVal, hx::UShr(ValToInt(ioVal),i)); } \
   template<typename T> static \
   void apply( T &ioVal, double &d) { AssignDouble(ioVal, hx::DoubleMod(ValToFloat(ioVal),d)); } \
   template<typename T> static \
   void apply( T &ioVal, String &) { } \
   template<typename T> static \
   void apply( T &ioVal, Dynamic &) { } \
};

DECL_STRUCT_ASSIGN_FUNC(AssignMod,hx::DoubleMod,runFloat,Float,aoMod)
DECL_STRUCT_ASSIGN_FUNC(AssignUShr,hx::UShr,runInt,int,aoUShr)



#define DECL_STRUCT_ASSIGN_CAST(NAME,CAST,OP,RUN_FUNC,ao) \
struct NAME \
{ \
   enum op { op = ao }; \
   template<typename T> \
   static T run(T &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      T left = ioVal; \
      CAST t = value->RUN_FUNC(ctx); \
      BCR_CHECK; \
      ioVal = (CAST)left OP t; \
      return ioVal; \
   } \
   static bool run(bool &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static String run(String &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) { value->runVoid(ctx); return ioVal; } \
   static hx::Object *run(hx::Object * &ioVal, hx::CppiaCtx *ctx, hx::CppiaExpr *value) \
   { \
      Dynamic left = ioVal; \
      CAST t = value->RUN_FUNC(ctx); \
      BCR_CHECK; \
      ioVal = Dynamic((CAST)left OP t).mPtr; \
      return ioVal; \
   } \
   template<typename T> static \
   void apply( T &ioVal, int &bits) { AssignInt(ioVal, ValToInt(ioVal)  OP bits); } \
   template<typename T> static \
   void apply( T &ioVal, double &) { } \
   template<typename T> static \
   void apply( T &ioVal, String &) { } \
   template<typename T> static \
   void apply( T &ioVal, Dynamic &) { } \
};

DECL_STRUCT_ASSIGN_CAST(AssignAnd,int,&,runInt,aoAnd)
DECL_STRUCT_ASSIGN_CAST(AssignOr,int,|,runInt,aoOr)
DECL_STRUCT_ASSIGN_CAST(AssignXOr,int,^,runInt,aoXOr)

DECL_STRUCT_ASSIGN_CAST(AssignShl,int,<<,runInt,aoShl)
DECL_STRUCT_ASSIGN_CAST(AssignShr,int,>>,runInt,aoShr)


int runContextConvertInt(CppiaCtx *ctx, ExprType inType, void *inFunc);
Float runContextConvertFloat(CppiaCtx *ctx, ExprType inType, void *inFunc);
String runContextConvertString(CppiaCtx *ctx, ExprType inType, void *inFunc);
hx::Object *runContextConvertObject(CppiaCtx *ctx, ExprType inType, void *inFunc);




} // end namespace hx



#endif
