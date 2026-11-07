#include <hxcpp.h>
#include "Cppia.h"
#include "CppiaStream.h"
#include <stdlib.h>
#include <atomic>
#include <mutex>
#include <new>
#include <algorithm>


namespace hx
{


static int sScriptId = 0;
static std::atomic<int> sLiveModules(0);


const char **sgNativeNameSlots = 0;
int sgNativeNameSlotCount = 0;

// Every module not yet freed, loaded or released
// Allocated once and never destroyed: a thread still running when the
// process exits could otherwise use it after its destructor ran (#371)
static std::mutex &sModuleLock = *new std::mutex();
static std::vector<CppiaModule *> &allModules()
{
   static std::vector<CppiaModule *> *modules = new std::vector<CppiaModule *>();
   return *modules;
}


// --- Module arena ----

static thread_local CppiaModule *sAllocModule = 0;

struct CppiaAllocScope
{
   CppiaModule *previous;
   CppiaAllocScope(CppiaModule *inModule) : previous(sAllocModule) { sAllocModule = inModule; }
   ~CppiaAllocScope() { sAllocModule = previous; }
};

struct CppiaBlock
{
   void (*destroy)(void *);
   // Zero for a block from malloc, made when no module was loading
   size_t size;
};
enum { CPPIA_BLOCK_HEADER = 16, CPPIA_ARENA_CHUNK = 64*1024 };

void *cppiaAlloc(size_t inSize, void (*inDestroy)(void *))
{
   size_t total = (CPPIA_BLOCK_HEADER + inSize + 15) & ~(size_t)15;
   CppiaBlock *block;
   if (sAllocModule)
   {
      block = (CppiaBlock *)sAllocModule->arenaAlloc(total);
      block->size = total;
   }
   else
   {
      block = (CppiaBlock *)malloc(total);
      if (!block)
         throw std::bad_alloc();
      block->size = 0;
   }
   block->destroy = inDestroy;
   return (char *)block + CPPIA_BLOCK_HEADER;
}

void cppiaFree(void *inPtr)
{
   if (!inPtr)
      return;
   CppiaBlock *block = (CppiaBlock *)((char *)inPtr - CPPIA_BLOCK_HEADER);
   if (block->size)
      block->destroy = 0;
   else
      free(block);
}

void *CppiaModule::arenaAlloc(size_t inSize)
{
   ArenaChunk chunk;
   if (inSize > CPPIA_ARENA_CHUNK/4)
   {
      // A chunk of its own, before the one still filling
      chunk.base = (char *)malloc(inSize);
      if (!chunk.base)
         throw std::bad_alloc();
      chunk.size = chunk.used = inSize;
      arena.insert(arena.empty() ? arena.end() : arena.end()-1, chunk);
      return chunk.base;
   }
   if (arena.empty() || arena.back().used + inSize > arena.back().size)
   {
      chunk.base = (char *)malloc(CPPIA_ARENA_CHUNK);
      if (!chunk.base)
         throw std::bad_alloc();
      chunk.size = CPPIA_ARENA_CHUNK;
      chunk.used = 0;
      arena.push_back(chunk);
   }
   ArenaChunk &last = arena.back();
   void *result = last.base + last.used;
   last.used += inSize;
   return result;
}

bool CppiaModule::ownsMemory(const void *inPtr) const
{
   const char *ptr = (const char *)inPtr;
   for(size_t i=0;i<arena.size();i++)
      if (ptr>=arena[i].base && ptr<arena[i].base + arena[i].used)
         return true;
   return false;
}



// --- CppiaModule ----

CppiaModule::CppiaModule()
{
   main = 0;
   layout = 0;
   creatingClass = 0;
   creatingFunction = 0;
   owner = 0;
   released = false;
   scriptId = ++sScriptId;
   sLiveModules++;
   strings = Array_obj<String>::__new(0,0);
   if (sgNativeNameSlotCount>0)
      for(int i=2;i<sgNativeNameSlotCount;i++)
         interfaceSlots[sgNativeNameSlots[i]] = i;

   std::lock_guard<std::mutex> lock(sModuleLock);
   allModules().push_back(this);
}

void CppiaModule::setDebug(CppiaExpr *outExpr, int inFileId, int inLine)
{
   #ifdef HXCPP_DEBUGGER
   if (inFileId)
      allFileIds.insert(inFileId);
   #endif
   outExpr->className = creatingClass;
   outExpr->functionName = creatingFunction;
   //outExpr->filename = cStrings[inFileId].c_str();
   outExpr->filename = strings[inFileId].utf8_str();
   outExpr->line = inLine;
}


// --- CppiaModule -------------------------

// Runs in a collection, so it must not allocate from the collector
CppiaModule::~CppiaModule()
{
   sLiveModules--;
   for(size_t c=0;c<arena.size();c++)
   {
      char *ptr = arena[c].base;
      char *end = ptr + arena[c].used;
      while(ptr<end)
      {
         CppiaBlock *block = (CppiaBlock *)ptr;
         if (block->destroy)
         {
            void (*destroy)(void *) = block->destroy;
            block->destroy = 0;
            destroy(ptr + CPPIA_BLOCK_HEADER);
         }
         ptr += block->size;
      }
   }
   for(size_t c=0;c<arena.size();c++)
      free(arena[c].base);
   for(size_t r=0;r<resources.size();r++)
      if (resources[r].mData)
         free((unsigned char *)resources[r].mData - 4);
}

void CppiaModule::link()
{
   DBGLOG("Resolve registered - super\n");
   HaxeNativeClass::link();
   
   DBGLOG("Resolve typeIds\n");
   for(int t=0;t<types.size();t++)
      types[t]->link(*this);

   DBGLOG("Resolve inherited atributes\n");
   for(int i=0;i<classes.size();i++)
   {
      classes[i]->linkTypes();
   }

   for(int i=0;i<classes.size();i++)
   {
      linkingClass = classes[i];
      classes[i]->link();
   }
   linkingClass = 0;

   if (main)
      main = (ScriptCallable *)main->link(*this);

   if (!linkMisses.empty())
   {
      std::string message = "Could not link ";
      for(size_t i=0;i<linkMisses.size();i++)
      {
         if (i)
            message += ", ";
         message += linkMisses[i];
      }
      hx::Throw( String::create(message.c_str(), (int)message.size()) );
   }
}

#ifdef CPPIA_JIT
void CppiaModule::compile()
{
   for(int i=0;i<classes.size();i++)
      classes[i]->compile();

   if (main)
      main->compile();
}

void CppiaModule::markLazy()
{
   for(int i=0;i<classes.size();i++)
      classes[i]->markLazy();

   if (main)
      main->lazy = true;
}
#endif

void addScriptableClass(String inName);
void addScriptableFile(String inName);

void CppiaModule::registerDebugger()
{
   #ifdef HXCPP_DEBUGGER
   for(int i=0;i<classes.size();i++)
   {
      String scriptable(classes[i]->name.c_str());
      addScriptableClass( scriptable.makePermanent().utf8_str() );
   }

   for(std::unordered_set<int>::const_iterator i = allFileIds.begin(); i!=allFileIds.end(); ++i)
      addScriptableFile(strings[*i]);

   // Lets a debugger set breakpoints in the files this script brought
   if (hx::g_onScriptLoadedFunction.mPtr)
      hx::g_onScriptLoadedFunction();

   #endif
}

CppiaClassInfo *CppiaModule::findClass( ::String inName)
{

   std::string stdName(inName.utf8_str());
   for(int i=0;i<classes.size();i++)
      if (classes[i]->name == stdName)
         return classes[i];
   return 0;
}


// The data the module holds goes now; its code goes once nothing reaches the module
void CppiaModule::release()
{
   for(int i=0;i<classes.size();i++)
      classes[i]->release();
   if (resources.size())
      UnregisterResources(&resources[0]);
}

void CppiaModule::mark(hx::MarkContext *__inCtx)
{
   DBGLOG(" --- MARK --- \n");
   HX_MARK_MEMBER(strings);
   for(int i=0;i<types.size();i++)
   {
      if (types[i]) /* May be partially constructed */
         types[i]->mark(__inCtx);
   }
   for(int i=0;i<markable.size();i++)
   {
      markable[i]->mark(__inCtx);
   }
   for(int i=0;i<classes.size();i++)
      if (classes[i])
      {
         classes[i]->mark(__inCtx);
      }
}

#ifdef HXCPP_VISIT_ALLOCS
void CppiaModule::visit(hx::VisitContext *__inCtx)
{
   HX_VISIT_MEMBER(strings);
   for(int i=0;i<types.size();i++)
      types[i]->visit(__inCtx);
   for(int i=0;i<markable.size();i++)
      markable[i]->visit(__inCtx);
   for(int i=0;i<classes.size();i++)
      if (classes[i])
         classes[i]->visit(__inCtx);
}
#endif


void cppiaClassInit(CppiaClassInfo *inClass, CppiaCtx *ctx, int inPhase);



void CppiaModule::boot(CppiaCtx *ctx)
{
   // boot (statics)
   for(int i=0;i<classes.size();i++)
      cppiaClassInit(classes[i],ctx,0);
   // run __init__
   for(int i=0;i<classes.size();i++)
      cppiaClassInit(classes[i],ctx,1);
}

int CppiaModule::getInterfaceSlot(const std::string &inName)
{
   InterfaceSlots::iterator it = interfaceSlots.find(inName);
   if (it==interfaceSlots.end())
   {
      int result = interfaceSlots.size()+1;
      interfaceSlots[inName] = result;
      return result;
   }
   return it->second;
}


int CppiaModule::findInterfaceSlot(const std::string &inName)
{
   InterfaceSlots::iterator it = interfaceSlots.find(inName);
   if (it==interfaceSlots.end())
      return -1;
   return it->second;
}




// Each thing is named once, at the first place the script uses it
void CppiaModule::linkMiss(CppiaExpr *inWhere, const std::string &inWhat)
{
   for(size_t i=0;i<linkMisses.size();i++)
      if (linkMisses[i].compare(0, inWhat.size(), inWhat)==0 &&
          (linkMisses[i].size()==inWhat.size() || linkMisses[i][inWhat.size()]==' '))
         return;

   std::string miss = inWhat;
   if (inWhere && inWhere->filename && inWhere->line>0 && strcmp(inWhere->filename,"?"))
      miss += " (" + std::string(inWhere->filename) + ":" + std::to_string(inWhere->line) + ")";
   linkMisses.push_back(miss);
}

void CppiaModule::where(CppiaExpr *e)
{
   if (linkingClass && layout)
      CPPIALOG(" in %s::%p\n", linkingClass->name.c_str(), layout);
   CPPIALOG("   %s at %s:%d %s\n", e->getName(), e->filename, e->line, e->functionName);
}



void ScriptableRegisterNameSlots(const char *inNames[], int inLength)
{
   sgNativeNameSlots = inNames;
   sgNativeNameSlotCount = inLength;
}


// --- CppiaConst -------------------------------------------

CppiaConst::CppiaConst() : type(cNull), ival(0), dval(0) { }

void CppiaConst::fromStream(CppiaStream &stream)
{
   std::string tok = stream.getToken();
   if (tok[0]=='i')
   {
      type = cInt;

      dval = ival = stream.getInt();
   }
   else if (tok=="true")
   {
      type = cInt;
      dval = ival = 1;
   }
   else if (tok=="false")
   {
      type = cInt;
      dval = ival = 0;
   }
   else if (tok[0]=='f')
   {
      type = cFloat;
      int strIndex = stream.getStringId();
      String val = stream.module->strings[strIndex];
      dval = atof(val.out_str());
      ival = dval;
   }
   else if (tok[0]=='s')
   {
      type = cString;
      ival = stream.getInt();
   }
   else if (tok=="NULL")
      type = cNull;
   else if (tok=="THIS")
      type = cThis;
   else if (tok=="SUPER")
      type = cSuper;
   else
      throw "unknown const value";
}




// --- CppiaLoadedModule -------------------------------------------





static Array<Dynamic> adoptInstances(CppiaModule *inNext, CppiaModule *inPrevious);

// Cppia Object - manage
class CppiaObject : public hx::CppiaLoadedModule_obj
{
public:
   CppiaModule *cppia;
   bool        booted;

   // A loaded module is a root until it is released
   CppiaObject(CppiaModule *inModule)
   {
      cppia = inModule;
      booted = false;
      cppia->owner = this;
      GCAddRoot(&cppia->owner);
   }
   void __Mark(hx::MarkContext *ctx) HXCPP_OVERRIDE { cppia->mark(ctx); }
#ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
   {
      cppia->visit(__inCtx);
      HX_VISIT_OBJECT(cppia->owner);
   }
#endif


   void boot() HXCPP_OVERRIDE
   {
      if (booted)
         return;

      booted = true;
      try
      {
         DBGLOG("--- Boot --------------------------------------------\n");
         CppiaCtx *ctx = CppiaCtx::getCurrent();
         cppia->boot(ctx);
      }
      catch(const char *errorString)
      {
         String error(errorString);
         hx::Throw(error);
      }
      catch(const std::exception &e)
      {
         hx::Throw( String(e.what()) );
      }
   }


   void run() HXCPP_OVERRIDE
   {
      if (!booted)
         boot();
      if (cppia->main)
      {
         try
         {
            //__hxcpp_enable(false);
            DBGLOG("--- Run --------------------------------------------\n");
            CppiaCtx *ctx = CppiaCtx::getCurrent();
            {
               // Main gets a frame of its own, which is given back when it returns
               unsigned char *pointer = ctx->pointer;
               ctx->push( (hx::Object *) 0 );
               AutoStack save(ctx,pointer);
               ctx->runVoid(cppia->main);
            }
            if (ctx->exception)
            {
               hx::Object *e = fromPendingException(ctx->exception);
               ctx->exception = 0;
               hx::Throw( Dynamic(e) );
            }
         }
         catch(const char *errorString)
         {
            String error(errorString);
            hx::Throw(error);
         }
         catch(const std::exception &e)
         {
            hx::Throw( String(e.what()) );
         }
      }
   }

   ::hx::Class resolveClass( ::String inName) HXCPP_OVERRIDE
   {
      CppiaClassInfo *info = cppia->findClass(inName);
      if (info)
         return info->getClass();
      return null();
   }

   // The module stops being a root; it is freed by the first collection that finds nothing
   // reaching this object
   void release() HXCPP_OVERRIDE
   {
      {
         std::lock_guard<std::mutex> lock(sModuleLock);
         if (cppia->released)
            return;
         cppia->released = true;
      }
      cppia->release();
      GCRemoveRoot(&cppia->owner);
   }

   // The std's Module extern does not declare release, so untyped calls
   // reach it as a field
   struct ReleaseFunc : public hx::LocalFunc
   {
      hx::Object *module;
      ReleaseFunc(hx::Object *inModule) : module(inModule) { }
      Dynamic __Run(const Array<Dynamic> &inArgs) HXCPP_OVERRIDE
      {
         static_cast<CppiaObject *>(module)->release();
         return null();
      }
      #if (HXCPP_API_LEVEL<500)
      Dynamic __run() HXCPP_OVERRIDE { return __Run(null()); }
      #endif
      int __ArgCount() const HXCPP_OVERRIDE { return 0; }
      void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(module); }
      #ifdef HXCPP_VISIT_ALLOCS
      void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_OBJECT(module); }
      #endif
   };

   // Moves the live instances of the previous module's classes onto this
   //  module's classes of the same name, where their fields are laid out the
   //  same, and reports each class that had any (libhxcpp #265)
   Array<Dynamic> adopt(Dynamic inPrevious)
   {
      CppiaObject *previous = dynamic_cast<CppiaObject *>(inPrevious.mPtr);
      if (!previous)
         hx::Throw(HX_CSTRING("adoptInstances takes a cppia module"));
      if (previous==this)
         hx::Throw(HX_CSTRING("adoptInstances takes another module"));
      if (previous->cppia->released)
         hx::Throw(HX_CSTRING("adoptInstances takes a module before its release"));
      // Moved instances run this module's code, which needs its statics
      boot();
      return adoptInstances(cppia, previous->cppia);
   }

   struct AdoptFunc : public hx::LocalFunc
   {
      hx::Object *module;
      AdoptFunc(hx::Object *inModule) : module(inModule) { }
      Dynamic __Run(const Array<Dynamic> &inArgs) HXCPP_OVERRIDE
      {
         return static_cast<CppiaObject *>(module)->adopt(inArgs->length ? inArgs[0] : Dynamic());
      }
      #if (HXCPP_API_LEVEL<500)
      Dynamic __run(const Dynamic &inPrevious) HXCPP_OVERRIDE { return static_cast<CppiaObject *>(module)->adopt(inPrevious); }
      #endif
      int __ArgCount() const HXCPP_OVERRIDE { return 1; }
      void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(module); }
      #ifdef HXCPP_VISIT_ALLOCS
      void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_OBJECT(module); }
      #endif
   };

   hx::Val __Field(const String &inName, hx::PropertyAccess inCallProp) HXCPP_OVERRIDE
   {
      if (inName==HX_CSTRING("release"))
         return Dynamic(new ReleaseFunc(this));
      if (inName==HX_CSTRING("adoptInstances"))
         return Dynamic(new AdoptFunc(this));
      return hx::CppiaLoadedModule_obj::__Field(inName, inCallProp);
   }

};


/*
 A loaded module is an ordinary GC object, so a generational collection stops traversing it once it
 is old. What it holds can still change after that: a literal boxes itself on first use, the JIT
 embeds that box in its code, a script static is assigned. None of those writes has a barrier, so a
 minor collection freed the young object while the old module still pointed at it.

 So every loaded module is marked on every collection, the way class statics are. A released module
 is too in a minor collection; a full one reaches it only through its CppiaObject, so it can be freed.
*/
void scriptMarkModules(hx::MarkContext *__inCtx, bool inGenerational)
{
   std::lock_guard<std::mutex> lock(sModuleLock);
   std::vector<CppiaModule *> &modules = allModules();
   for(size_t i=0;i<modules.size();i++)
      if (!modules[i]->released || inGenerational)
         modules[i]->mark(__inCtx);
}

#ifdef HXCPP_VISIT_ALLOCS
void scriptVisitModules(hx::VisitContext *__inCtx)
{
   std::lock_guard<std::mutex> lock(sModuleLock);
   std::vector<CppiaModule *> &modules = allModules();
   for(size_t i=0;i<modules.size();i++)
   {
      modules[i]->visit(__inCtx);
      HX_VISIT_OBJECT(modules[i]->owner);
   }
}
#endif


#ifdef HXCPP_STACK_TRACE
void GCForEachStackContext(void (*inFunc)(StackContext *, void *), void *inData);

static const char *copyPositionString(const char *inString)
{
   if (!inString)
      return 0;
   size_t len = strlen(inString) + 1;
   char *result = (char *)malloc(len);
   memcpy(result, inString, len);
   return result;
}

static std::string positionKey(const char *inString)
{
   return inString ? std::string(inString) : std::string();
}

// An exception stack can still show functions of a freed module; they get copies kept by content,
// so reloading the same code adds none
static const StackPosition *keepPosition(const StackPosition *inPosition)
{
   static std::map<std::string, StackPosition *> *kept = new std::map<std::string, StackPosition *>();
   std::string key = positionKey(inPosition->className) + '\n' + positionKey(inPosition->functionName) + '\n' +
                     positionKey(inPosition->fullName) + '\n' + positionKey(inPosition->fileName);
   StackPosition *&copy = (*kept)[key];
   if (!copy)
   {
      copy = new StackPosition(*inPosition);
      copy->className = copyPositionString(inPosition->className);
      copy->functionName = copyPositionString(inPosition->functionName);
      copy->fullName = copyPositionString(inPosition->fullName);
      copy->fileName = copyPositionString(inPosition->fileName);
      #ifdef HXCPP_STACK_SCRIPTABLE
      copy->scriptCallable = 0;
      #endif
   }
   return copy;
}

static void keepPositions(QuickVec<ExceptionStackFrame> &ioStack, std::vector<CppiaModule *> &inFreed)
{
   for(int i=0;i<ioStack.size();i++)
      for(size_t m=0;m<inFreed.size();m++)
         if (inFreed[m]->ownsMemory(ioStack[i].position))
         {
            ioStack[i].position = keepPosition(ioStack[i].position);
            break;
         }
}

static void keepContextPositions(StackContext *inCtx, void *inFreed)
{
   std::vector<CppiaModule *> &freed = *(std::vector<CppiaModule *> *)inFreed;
   keepPositions(inCtx->mExceptionStack, freed);
   keepPositions(inCtx->mOuterExceptionStack, freed);
}
#endif

// Called by each collection once marking and finalizers are done, with every thread stopped
void scriptFreeModules()
{
   // They remember function names by address
   #if !defined(HXCPP_PROFILER) && !defined(HXCPP_TELEMETRY)
   std::vector<CppiaModule *> freed;
   {
      std::lock_guard<std::mutex> lock(sModuleLock);
      std::vector<CppiaModule *> &modules = allModules();
      for(size_t i=0;i<modules.size(); )
      {
         if (modules[i]->released && !IsWeakRefValid(modules[i]->owner))
         {
            freed.push_back(modules[i]);
            modules.erase(modules.begin()+i);
         }
         else
            i++;
      }
   }
   if (freed.empty())
      return;

   #ifdef HXCPP_STACK_TRACE
   GCForEachStackContext(keepContextPositions, &freed);
   #endif

   for(size_t i=0;i<freed.size();i++)
      delete freed[i];
   #endif
}


int CppiaStream::getTypeId()
{
   int id = getInt();
   if (id<0 || id>=(int)module->types.size())
      throw "Bad type id";
   return id;
}

int CppiaStream::getStringId()
{
   int id = getInt();
   if (id<0 || id>=module->strings->length)
      throw "Bad string id";
   return id;
}

CppiaLoadedModule LoadCppia(const unsigned char *inData, int inDataLength)
{
   CppiaModule   *cppiaPtr = new CppiaModule();
   CppiaLoadedModule loadedModule = new CppiaObject(cppiaPtr);

   String error;
   {
   CppiaAllocScope allocScope(cppiaPtr);

   CppiaModule   &cppia = *cppiaPtr;
   CppiaStream stream(cppiaPtr,inData, inDataLength);

   try
   {
      std::string tok = stream.getAsciiToken();
      if (tok!="CPPIA" && tok!="CPPIB")
         throw "Bad magic";

      stream.setBinary(tok=="CPPIB");

      int stringCount = stream.checkCount(stream.getAsciiInt());
      for(int s=0;s<stringCount;s++)
         cppia.strings[s] = stream.readString();

      int typeCount = stream.checkCount(stream.getAsciiInt());
      cppia.types.resize(typeCount);
      DBGLOG("Type count : %d\n", typeCount);
      for(int t=0;t<typeCount;t++)
         cppia.types[t] = new TypeData(stream.readString());

      int classCount = stream.checkCount(stream.getAsciiInt());
      DBGLOG("Class count : %d\n", classCount);

      if (stream.binary)
      {
         int newLine = stream.getByte();
         if (newLine!='\n')
            throw "Missing new-line after class count";
      }

      cppia.classes.reserve(classCount);
      for(int c=0;c<classCount;c++)
      {
         // Listed while it loads, so the module marks its class object
         CppiaClassInfo *info = new CppiaClassInfo(cppia);
         cppia.classes.push_back(info);
         if (!info->load(stream))
            cppia.classes.pop_back();
      }

      tok = stream.getToken();
      if (tok=="MAIN")
      {
         DBGLOG("Main...\n");
         cppia.main = new ScriptCallable(createCppiaExpr(stream));
         cppia.main->className = "cppia";
         cppia.main->functionName = "__cppia_main";
         cppia.main->staticEntry = true;
      }
      else if (tok!="NOMAIN")
         throw "no main specified";

      tok = stream.getToken();
      if (tok=="RESOURCES")
      {
         int count = stream.checkCount(stream.getInt());
         std::vector<hx::Resource> &scriptResources = cppia.resources;
         scriptResources.resize(count+1);
         for(int r=0;r<count;r++)
         {
            tok = stream.getToken();
            if (tok!="RESO")
               throw "no reso tag";

            scriptResources[r].mName = cppia.strings[stream.getStringId()];
            scriptResources[r].mDataLength = stream.checkCount(stream.getInt());
         }
         if (!stream.binary)
            stream.skipChar();

         for(int r=0;r<count;r++)
         {
            int len = scriptResources[r].mDataLength;
            unsigned char *buffer = (unsigned char *)malloc(len+5);
            if (!buffer)
               throw "Out of memory";
            scriptResources[r].mData = buffer + 4;
            *(unsigned int *)buffer = HX_GC_CONST_ALLOC_BIT;
            buffer[len+5-1] = '\0';
            stream.readBytes(buffer+4, len);
            #ifdef HX_SMART_STRINGS_1
            unsigned char *p = (unsigned char *)buffer+4;
            unsigned char *end = p + len;
            while(!hasBig && p<end)
               if (*p++>127)
               {
                  *(unsigned int *)buffer |= HX_GC_STRING_CHAR16_T;
                  break;
               }
            #endif
         }
         scriptResources[count].mDataLength = 0;
         scriptResources[count].mData = 0;
         scriptResources[count].mName = String();
         
         RegisterResources(&scriptResources[0]);
      }
      else
         throw "no resources tag";


   }
   catch(const char *errorString)
   {
      error = HX_CSTRING("Error reading file ") + String(errorString) + 
                HX_CSTRING(", line ") + String(stream.line) + HX_CSTRING(", char ") + 
                   String(stream.pos);
   }
   catch(const std::exception &e)
   {
      error = HX_CSTRING("Error reading file ") + String(e.what());
   }
   catch(Dynamic e)
   {
      loadedModule->release();
      throw;
   }

   // A module built for one host, with its fingerprint as a resource, is refused
   // by any other before linking, which would only find the first difference
   if (!error.raw_ptr())
      for(size_t r=0;r<cppia.resources.size();r++)
         if (cppia.resources[r].mData && cppia.resources[r].mName==HX_CSTRING("__cppia_host"))
         {
            std::string wanted((const char *)cppia.resources[r].mData, cppia.resources[r].mDataLength);
            size_t first = wanted.find_first_not_of(" \t\r\n");
            size_t last = wanted.find_last_not_of(" \t\r\n");
            wanted = first==std::string::npos ? std::string() : wanted.substr(first, last-first+1);
            const std::string &host = HaxeNativeClass::hostFingerprint();
            if (wanted!=host)
            {
               std::string message = "This cppia module was built for host " + wanted + ", but this host is " + host;
               error = String::create(message.c_str(), (int)message.size());
            }
            break;
         }

   if (!error.raw_ptr())
      try
      {
         DBGLOG("Link...\n");
         cppia.link();
      }
      catch(const char *errorString)
      {
         error = String(errorString);
      }
      catch(Dynamic e)
      {
         loadedModule->release();
         throw;
      }

   #ifdef CPPIA_JIT
   bool jit = gEnableJit && CppiaCompiler::canRun();
   if (gEnableJit && !jit)
      CppiaCompiler::reportCannotRun();
   #else
   bool jit = false;
   #endif

   if (jit)
   {
      #ifdef CPPIA_JIT
      if (!error.raw_ptr())
         try
         {
            DBGLOG("Compile...\n");
            // Each function compiles when it is first called, unless every
            // one is to compile now
            if (gLazyJit)
               cppia.markLazy();
            else
               cppia.compile();
         }
         catch(const char *errorString)
         {
            error = String(errorString);
         }
         catch(Dynamic e)
         {
            loadedModule->release();
            throw;
         }
      #endif
   }
   }

   // The host never sees a module that failed to load, so it cannot release it
   if (error.raw_ptr())
   {
      loadedModule->release();
      hx::Throw(error);
   }

   cppiaPtr->registerDebugger();

   return loadedModule;
}


// --- Script formats ----

// A format a host registered with __scriptable_cppia_register_format. There are
// few, kept for the life of the program, so a node is never freed and its
// translator slot is a GC root from the start
struct CppiaFormat
{
   std::string magic;
   hx::Object *translate;
   CppiaFormat *next;
};

// Held only to read or change the list, so whoever holds it never allocates GC
// memory or reaches a safe point
static std::mutex &sFormatLock = *new std::mutex();
static CppiaFormat *sFormats = 0;

static void registerFormat(String inMagic, Dynamic inTranslate)
{
   const char *magic = inMagic.raw_ptr() ? inMagic.utf8_str() : 0;
   if (!magic || !*magic)
      hx::Throw(HX_CSTRING("A cppia format needs a magic"));
   std::string key(magic);

   std::lock_guard<std::mutex> lock(sFormatLock);
   for(CppiaFormat *f=sFormats; f; f=f->next)
      if (f->magic==key)
      {
         f->translate = inTranslate.mPtr;
         return;
      }
   if (!inTranslate.mPtr)
      return;
   CppiaFormat *format = new CppiaFormat();
   format->magic = key;
   format->translate = inTranslate.mPtr;
   GCAddRoot(&format->translate);
   format->next = sFormats;
   sFormats = format;
}

// The translator for a script in a registered format, or null. Cppia, text or
// binary, is never translated
static Dynamic findFormat(const unsigned char *inData, int inLength)
{
   if (inLength>=5 && (!memcmp(inData,"CPPIA",5) || !memcmp(inData,"CPPIB",5)))
      return null();
   std::lock_guard<std::mutex> lock(sFormatLock);
   for(CppiaFormat *f=sFormats; f; f=f->next)
      if (f->translate && f->magic.size()<=(size_t)inLength &&
            !memcmp(inData,f->magic.c_str(),f->magic.size()))
         return f->translate;
   return null();
}

// The bytes of cppia text held in a String
static const unsigned char *codeOf(const String &inCode, int &outLength)
{
   const unsigned char *code = (const unsigned char *)inCode.raw_ptr();
   outLength = inCode.length;
   #ifdef HX_SMART_STRINGS
   if (inCode.isUTF16Encoded())
   {
      code = (const unsigned char *)inCode.utf8_str();
      outLength = strlen( (const char *)code );
   }
   #endif
   return code;
}

// Loads what a format's translator gives for a script: cppia text as a String,
// or text or binary cppia as bytes. It is not translated again
static CppiaLoadedModule loadTranslated(Dynamic inTranslate, Array<unsigned char> inScript)
{
   Dynamic cppia = inTranslate(inScript);
   if (cppia.mPtr && cppia->__GetType()==vtString)
   {
      String text = cppia;
      int length = 0;
      const unsigned char *code = codeOf(text,length);
      return LoadCppia(code,length);
   }
   Array<unsigned char> data = cppia;
   if (!data.mPtr)
      hx::Throw(HX_CSTRING("A cppia format's translator returned neither a String nor bytes"));
   return LoadCppia((unsigned char *)data->GetBase(),data->length);
}





Array<String > gAllClasses;
Array<String > gAllFiles;

void addScriptableClass(String inName)
{
   #ifdef HXCPP_DEBUGGER
   if (!gAllClasses.mPtr)
   {
      gAllClasses = Array_obj<Dynamic>::__new();
      GCAddRoot( (hx::Object **)&gAllClasses.mPtr );
   }
   if (gAllClasses->indexOf(inName)<0)
      gAllClasses->push(inName);
   #endif
}


void addScriptableFile(String inName)
{
   #ifdef HXCPP_DEBUGGER
   if (!gAllFiles.mPtr)
   {
      gAllFiles = Array_obj<Dynamic>::__new();
      GCAddRoot( (hx::Object **)&gAllFiles.mPtr );
   }
   if (gAllFiles->indexOf(inName)<0)
      gAllFiles->push(inName);
   #endif
}








} // end namespace hx




#ifdef HXCPP_STACK_SCRIPTABLE

void __hxcpp_dbg_getScriptableVariables(hx::StackFrame *inFrame, ::Array<Dynamic> outNames)
{
   hx::ScriptCallable *callable = inFrame->position->scriptCallable;
   if (callable)
      callable->getScriptableVariables(inFrame->scriptStack, outNames);
}

bool __hxcpp_dbg_getScriptableValue(hx::StackFrame *inFrame, String inName, ::Dynamic &outValue)
{
   hx::ScriptCallable *callable = inFrame->position->scriptCallable;
   if (callable)
      return callable->getScriptableValue(inFrame->scriptStack, inName, outValue);
   return false;
}


bool __hxcpp_dbg_setScriptableValue(hx::StackFrame *inFrame, String inName, ::Dynamic inValue)
{
   hx::ScriptCallable *callable = inFrame->position->scriptCallable;
   if (callable)
      return callable->setScriptableValue(inFrame->scriptStack, inName, inValue);
   return false;
}

#endif





#ifdef HXCPP_DEBUGGER
void __hxcpp_dbg_getScriptableFiles( Array< ::String> ioPaths )
{
   Array<String> merge = hx::gAllFiles;
   if (merge.mPtr)
      for(int i=0;i< merge->length; i++)
      {
         if (ioPaths->indexOf( merge[i] ) < 0)
            ioPaths->push( merge[i] );
      }
}

void __hxcpp_dbg_getScriptableFilesFullPath( Array< ::String> ioPaths )
{
   __hxcpp_dbg_getScriptableFiles( ioPaths );
}

void __hxcpp_dbg_getScriptableClasses( Array< ::String> ioClasses )
{
   Array<String> merge = hx::gAllClasses;
   if (merge.mPtr)
      for(int i=0;i< merge->length; i++)
      {
         if (ioClasses->indexOf( merge[i] ) < 0)
            ioClasses->push( merge[i] );
      }
}

#endif



int __scriptable_cppia_live_modules()
{
   return hx::sLiveModules;
}


namespace hx
{
extern void (*gCensusVisitor)(hx::Object *inObj, size_t inBytes);

struct RetainedTally
{
   size_t count;
   size_t bytes;
   RetainedTally() : count(0), bytes(0) { }
};

// What one census found of each released module. Filled while the census
// collection marks, on one thread, so it allocates nothing from the collector
struct RetainedModule
{
   CppiaModule *module;
   std::map<CppiaClassInfo *, RetainedTally> instances;
   RetainedTally closures;
};
static std::vector<RetainedModule> *sRetained = 0;

static RetainedModule *retainedFor(CppiaModule *inModule)
{
   for(size_t i=0;i<sRetained->size();i++)
      if ((*sRetained)[i].module==inModule)
         return &(*sRetained)[i];
   return 0;
}

static void retainedVisit(hx::Object *inObj, size_t inBytes)
{
   // A module marks its own Class objects and enum values, so those are not
   // told apart from what the host holds
   if (getScriptClassInfo(inObj))
      return;
   if (ScriptCallable *function = getClosureFunction(inObj))
   {
      for(size_t i=0;i<sRetained->size();i++)
         if ((*sRetained)[i].module->ownsMemory(function))
         {
            (*sRetained)[i].closures.count++;
            (*sRetained)[i].closures.bytes += inBytes;
            break;
         }
      return;
   }
   hx::Class cls = inObj->__GetClass();
   CppiaClassInfo *info = getScriptClassInfo(cls.mPtr);
   if (info && !info->isEnum)
      if (RetainedModule *retained = retainedFor(&info->cppia))
      {
         RetainedTally &tally = retained->instances[info];
         tally.count++;
         tally.bytes += inBytes;
      }
}

static hx::Anon retainedRow(const char *inName, const RetainedTally &inTally)
{
   hx::Anon row = hx::Anon_obj::Create();
   if (inName)
      row->__SetField(HX_CSTRING("name"), String::create(inName), hx::paccDynamic);
   row->__SetField(HX_CSTRING("count"), (int)std::min(inTally.count, (size_t)0x7fffffff), hx::paccDynamic);
   row->__SetField(HX_CSTRING("bytes"), (Float)inTally.bytes, hx::paccDynamic);
   return row;
}

// The census visitor is one global, so its users take turns
static std::mutex *sCensusVisitorLock = new std::mutex();

static void censusWithVisitor(void (*inVisitor)(hx::Object *, size_t))
{
   {
      hx::AutoGCFreeZone wait;
      sCensusVisitorLock->lock();
   }
   gCensusVisitor = inVisitor;
   __hxcpp_gc_census();
   gCensusVisitor = 0;
   sCensusVisitorLock->unlock();
}


// --- Moving live instances onto a replaced class (libhxcpp #265) ---
//
// An instance of a script class finds everything about its class through its
// script vtable, whose slot -1 is the class: its methods, its interfaces, how
// the collector marks it, Type.getClass. So an instance moves to another class
// by having that one pointer replaced. It stays where it is, at its size, so
// the new class must lay out every field as the old one did.

// A previous module's class with live instances, and the class they move to,
// or why they cannot
struct Adoption
{
   CppiaClassInfo *to;
   std::string reason;
   size_t moved;
   size_t live;
   Adoption() : to(0), moved(0), live(0) { }
};
static std::map<CppiaClassInfo *, Adoption> *sAdoptions = 0;

static std::string typeName(TypeData *inType)
{
   return std::string(inType->name.utf8_str());
}

// The interfaces a class and its script super classes implement, by name
static std::vector<std::string> interfaceNames(CppiaClassInfo *inInfo)
{
   std::vector<std::string> names;
   for(CppiaClassInfo *info=inInfo; info; info=info->superType ? info->superType->cppiaClass : 0)
      for(size_t i=0;i<info->implements.size();i++)
         names.push_back(typeName(info->cppia.types[info->implements[i]]));
   std::sort(names.begin(), names.end());
   return names;
}

// The first difference between two lists of a class's storage, its fields or
//  its dynamic functions, which are linked with their super classes' first
static std::string slotDifference(const char *inWhat, CppiaClassInfo *inFrom, std::vector<CppiaVar *> &inOld,
                                  CppiaClassInfo *inTo, std::vector<CppiaVar *> &inNew)
{
   for(size_t i=0;i<inOld.size() || i<inNew.size();i++)
   {
      if (i>=inOld.size())
         return std::string(inWhat) + " " + inTo->cppia.identStr(inNew[i]->nameId) + " is not in the old class";
      std::string name = inFrom->cppia.identStr(inOld[i]->nameId);
      if (i>=inNew.size())
         return std::string(inWhat) + " " + name + " is not in the new class";
      if (name!=inTo->cppia.identStr(inNew[i]->nameId))
         return std::string(inWhat) + " " + name + " is not where it was";
      if (inOld[i]->storeType!=inNew[i]->storeType || inOld[i]->offset!=inNew[i]->offset)
         return std::string(inWhat) + " " + name + " changed its type";
   }
   return std::string();
}

// Why instances of inFrom cannot become instances of inTo, or empty when they can
static std::string incompatibility(CppiaClassInfo *inFrom, CppiaClassInfo *inTo)
{
   if (inFrom->haxeBase!=inTo->haxeBase)
      return "it extends another host class";
   TypeData *from = inFrom->superType;
   TypeData *to = inTo->superType;
   while(from || to)
   {
      if (!from || !to || typeName(from)!=typeName(to))
         return "its super classes are not the same";
      from = from->cppiaClass ? from->cppiaClass->superType : 0;
      to = to->cppiaClass ? to->cppiaClass->superType : 0;
   }
   if (interfaceNames(inFrom)!=interfaceNames(inTo))
      return "it implements other interfaces";
   std::string field = slotDifference("field", inFrom, inFrom->memberVars, inTo, inTo->memberVars);
   if (field.empty())
      field = slotDifference("dynamic function", inFrom, inFrom->dynamicFunctions, inTo, inTo->dynamicFunctions);
   if (!field.empty())
      return field;
   if (inFrom->classSize!=inTo->classSize || inFrom->extraData!=inTo->extraData ||
       inFrom->dynamicMapOffset!=inTo->dynamicMapOffset || inFrom->containsPointers!=inTo->containsPointers)
      return "its instances are laid out otherwise";
   return std::string();
}

// Called with each object the census marks, on one thread with every other stopped
static void adoptVisit(hx::Object *inObj, size_t)
{
   // A module marks its own Class objects
   if (getScriptClassInfo(inObj))
      return;
   void **vtable = inObj->__GetScriptVTable();
   if (!vtable)
      return;
   std::map<CppiaClassInfo *, Adoption>::iterator found = sAdoptions->find((CppiaClassInfo *)vtable[-1]);
   if (found==sAdoptions->end())
      return;
   Adoption &adoption = found->second;
   adoption.live++;
   if (!adoption.to)
      return;
   void ***slot = (void ***)((char *)inObj + found->first->getScriptVTableOffset());
   if (*slot!=vtable)
      return;
   *slot = adoption.to->vtable;
   adoption.moved++;
}

static Array<Dynamic> adoptInstances(CppiaModule *inNext, CppiaModule *inPrevious)
{
   std::map<CppiaClassInfo *, Adoption> adoptions;
   for(size_t i=0;i<inPrevious->classes.size();i++)
   {
      CppiaClassInfo *info = inPrevious->classes[i];
      if (info->isInterface || info->isEnum || !info->vtable)
         continue;
      Adoption &adoption = adoptions[info];
      CppiaClassInfo *to = inNext->findClass(String::create(info->name.c_str()));
      if (!to || to->isInterface || to->isEnum || !to->vtable)
         adoption.reason = "the new module has no class of that name";
      else
      {
         adoption.reason = incompatibility(info, to);
         if (adoption.reason.empty())
            adoption.to = to;
      }
   }

   sAdoptions = &adoptions;
   censusWithVisitor(adoptVisit);
   sAdoptions = 0;

   std::vector< std::pair<std::string, Adoption> > rows;
   for(std::map<CppiaClassInfo *, Adoption>::iterator i=adoptions.begin(); i!=adoptions.end(); ++i)
      if (i->second.live)
         rows.push_back(std::make_pair(i->first->name, i->second));
   std::sort(rows.begin(), rows.end(), [](const std::pair<std::string, Adoption> &a, const std::pair<std::string, Adoption> &b)
      { return a.first < b.first; });

   Array<Dynamic> result = Array_obj<Dynamic>::__new(0,(int)rows.size());
   for(size_t r=0;r<rows.size();r++)
   {
      hx::Anon row = hx::Anon_obj::Create();
      row->__SetField(HX_CSTRING("name"), String::create(rows[r].first.c_str()), hx::paccDynamic);
      row->__SetField(HX_CSTRING("moved"), (int)std::min(rows[r].second.moved, (size_t)0x7fffffff), hx::paccDynamic);
      row->__SetField(HX_CSTRING("reason"), rows[r].second.reason.empty() ? String() : String::create(rows[r].second.reason.c_str()), hx::paccDynamic);
      result->push(row);
   }
   return result;
}

} // end namespace hx

// Collects, then reports each released module that is still loaded: its
// number, the live instances of its classes by class, and the bound methods
// and closures of its functions
Array<Dynamic> __scriptable_cppia_retained()
{
   std::vector<hx::RetainedModule> retained;
   {
      std::lock_guard<std::mutex> lock(hx::sModuleLock);
      std::vector<hx::CppiaModule *> &modules = hx::allModules();
      for(size_t i=0;i<modules.size();i++)
         if (modules[i]->released)
         {
            hx::RetainedModule entry;
            entry.module = modules[i];
            retained.push_back(entry);
         }
   }

   hx::sRetained = &retained;
   hx::censusWithVisitor(hx::retainedVisit);
   hx::sRetained = 0;

   // Copied out under the lock, and turned into Haxe values after it: a
   // collection while allocating them marks the modules, which takes the lock
   struct Report
   {
      int module;
      std::vector< std::pair<std::string, hx::RetainedTally> > instances;
      hx::RetainedTally closures;
   };
   std::vector<Report> reports;
   {
      std::lock_guard<std::mutex> lock(hx::sModuleLock);
      std::vector<hx::CppiaModule *> &modules = hx::allModules();
      for(size_t r=0;r<retained.size();r++)
      {
         // The census collection frees the modules nothing reaches
         if (std::find(modules.begin(), modules.end(), retained[r].module)==modules.end())
            continue;
         Report report;
         report.module = retained[r].module->scriptId;
         for(std::map<hx::CppiaClassInfo *, hx::RetainedTally>::iterator i=retained[r].instances.begin(); i!=retained[r].instances.end(); ++i)
            report.instances.push_back(std::make_pair(i->first->name, i->second));
         report.closures = retained[r].closures;
         reports.push_back(report);
      }
   }

   Array<Dynamic> result = Array_obj<Dynamic>::__new(0,(int)reports.size());
   for(size_t r=0;r<reports.size();r++)
   {
      std::vector< std::pair<std::string, hx::RetainedTally> > &sorted = reports[r].instances;
      std::sort(sorted.begin(), sorted.end(), [](const std::pair<std::string, hx::RetainedTally> &a, const std::pair<std::string, hx::RetainedTally> &b)
         { return a.second.bytes > b.second.bytes; });
      Array<Dynamic> instances = Array_obj<Dynamic>::__new(0,(int)sorted.size());
      for(size_t i=0;i<sorted.size();i++)
         instances->push( hx::retainedRow(sorted[i].first.c_str(), sorted[i].second) );

      hx::Anon row = hx::Anon_obj::Create();
      row->__SetField(HX_CSTRING("module"), reports[r].module, hx::paccDynamic);
      row->__SetField(HX_CSTRING("instances"), instances, hx::paccDynamic);
      row->__SetField(HX_CSTRING("closures"), hx::retainedRow(0, reports[r].closures), hx::paccDynamic);
      result->push(row);
   }
   return result;
}

String __scriptable_host_fingerprint()
{
   return String::create(hx::HaxeNativeClass::hostFingerprint().c_str());
}

bool __scriptable_cppia_jit_active()
{
   #ifdef CPPIA_JIT
   return hx::gEnableJit && hx::CppiaCompiler::canRun();
   #else
   return false;
   #endif
}

::hx::CppiaLoadedModule __scriptable_cppia_from_string(String inCode)
{
   int length = 0;
   const unsigned char *code = hx::codeOf(inCode,length);

   Dynamic translate = hx::findFormat(code,length);
   if (translate.mPtr)
   {
      Array<unsigned char> script = Array_obj<unsigned char>::__new(length);
      if (length)
         memcpy(script->GetBase(),code,length);
      return hx::loadTranslated(translate,script);
   }

   return hx::LoadCppia(code,length);
}


::hx::CppiaLoadedModule __scriptable_cppia_from_data(Array<unsigned char> inCode)
{
   const unsigned char *code = (unsigned char *)inCode->GetBase();
   Dynamic translate = hx::findFormat(code,inCode->length);
   if (translate.mPtr)
      return hx::loadTranslated(translate,inCode);

   return hx::LoadCppia(code,inCode->length);
}


void __scriptable_cppia_register_format(String inMagic, Dynamic inTranslate)
{
   hx::registerFormat(inMagic,inTranslate);
}


void __scriptable_load_cppia(String inCode)
{
   ::hx::CppiaLoadedModule module = __scriptable_cppia_from_string(inCode);

   module->boot();
   module->run();
}

