#include <hxcpp.h>
#include <hx/Scriptable.h>
#include "Cppia.h"
#include <algorithm>
#include <mutex>


namespace hx
{


typedef std::map<std::string, HaxeNativeClass *> ScriptRegisteredMap;
static ScriptRegisteredMap *sScriptRegistered = 0;

typedef std::map<std::string, HaxeNativeInterface *> HaxeNativeIntefaceMap;
static HaxeNativeIntefaceMap *sScriptRegisteredInterface = 0;






// -- HaxeNativeClass ---

HaxeNativeClass::HaxeNativeClass(const std::string &inName, int inDataOffset, ScriptNamedFunction *inFunctions, hx::ScriptableClassFactory inFactory, ScriptNamedFunction inConstruct)
{
   name = inName;
   mDataOffset = inDataOffset;
   functions = inFunctions;
   factory = inFactory;
   construct = inConstruct;
   haxeSuper = 0;
}

#ifdef NATIVE_CLASS_OVERRIDES_MARKED
void HaxeNativeClass::addVtableEntries( std::vector<std::string> &outVtable)
{
   if (haxeSuper)
      haxeSuper->addVtableEntries(outVtable);

   if (functions)
      for(ScriptNamedFunction *func = functions; func->name; func++)
         if (!func->isStatic && !func->isOverride)
            outVtable.push_back( func->name );
}
#else
void HaxeNativeClass::addVtableEntries(std::vector<std::string>& outVtable) {
   std::unordered_set<std::string> methodsSet;
   addVtableEntries(outVtable, methodsSet);
}

void HaxeNativeClass::addVtableEntries( std::vector<std::string> &outVtable,  std::unordered_set<std::string>& outMethodsSet)
{
   if (haxeSuper)
      haxeSuper->addVtableEntries(outVtable, outMethodsSet);

   if (functions)
      for (ScriptNamedFunction* func = functions; func->name; func++)
         if (!func->isStatic && outMethodsSet.find(func->name) == outMethodsSet.end()) {
            outVtable.push_back(func->name);
            outMethodsSet.emplace(func->name);
         }
}
#endif

void HaxeNativeClass::dump()
{
   printf("HaxeNativeClass %s\n", name.c_str());

   if (functions)
      for(ScriptNamedFunction *f=functions;f->name;f++)
         printf("  %s func %s\n", f->isStatic ? "static" : "virtual", f->name );
   if (haxeSuper)
   {
      printf("super:\n");
      haxeSuper->dump();
   }
}

ScriptNamedFunction HaxeNativeClass::findFunction(const std::string &inName)
{
   if (functions)
      for(ScriptNamedFunction *f=functions;f->name;f++)
         if (inName == f->name && !f->isStatic)
            return *f;
   if (haxeSuper)
      return haxeSuper->findFunction(inName);

   return ScriptNamedFunction(0,0,0,0,0);
}


ScriptNamedFunction HaxeNativeClass::findStaticFunction(String inName)
{
   if (functions)
      for(ScriptNamedFunction *f=functions;f->name;f++)
         if ( !strcmp(inName.utf8_str(),f->name) && f->isStatic)
            return *f;

   return ScriptNamedFunction(0,0,0,0,0);
}



// The maps are filled as the host starts, and only read after, by any thread
//  loading or running a script: a lookup with operator[] inserted the names it
//  missed, under threads walking or searching the same map (libhxcpp #466)
HaxeNativeClass *HaxeNativeClass::findClass(const std::string &inName)
{
   if (!sScriptRegistered)
      return 0;
   ScriptRegisteredMap::iterator it = sScriptRegistered->find(inName);
   return it==sScriptRegistered->end() ? 0 : it->second;
}

HaxeNativeClass *HaxeNativeClass::hxObject()
{
   return findClass("hx.Object");
}

void HaxeNativeClass::link()
{
   // Each load links, and the super classes it sets are shared by every module.
   //  Linking allocates, so a thread that waits for another's link waits in a
   //  GC-free zone: outside one, a collection the linker reached waited for it
   static std::mutex linkLock;
   if (!linkLock.try_lock())
   {
      hx::AutoGCFreeZone zone;
      linkLock.lock();
   }
   std::lock_guard<std::mutex> lock(linkLock, std::adopt_lock);
   HaxeNativeClass *hxObj = hxObject();
   for(ScriptRegisteredMap::iterator i = sScriptRegistered->begin(); i!=sScriptRegistered->end();++i)
   {
      if (!i->second)
         continue;

      DBGLOG("== %s ==\n", i->first.c_str());
      if (i->second == hxObj)
      {
         DBGLOG(" super =0\n");
         continue;
      }
      hx::Class cls = hx::Class_obj::Resolve( String(i->first.c_str() ) );
      if (cls.mPtr)
      {
         hx::Class superClass = cls->GetSuper();
         if (superClass.mPtr)
         {
            HaxeNativeClass *superRef = findClass(superClass.mPtr->mName.utf8_str());
            if (superRef)
            {
               DBGLOG("registered %s\n",superClass.mPtr->mName.utf8_str());
               i->second->haxeSuper = superRef;
            }
         }
      }

      DBGLOG("haxe super = %p\n", i->second->haxeSuper);
      if (!i->second->haxeSuper)
      {
         DBGLOG("using hx.Object\n");
         i->second->haxeSuper = hxObj;
      }
   }

}


// What the host gives scripts to link against, as text: each registered class
// with its super class, functions and fields, and each interface with its
// functions. Lists are sorted, so the same host source always gives the same
// text. Field offsets are left out: they change with the word size, not with
// what a script can use.
static std::string hostExports()
{
   std::string out;
   if (sScriptRegistered)
      for(ScriptRegisteredMap::iterator i = sScriptRegistered->begin(); i!=sScriptRegistered->end();++i)
      {
         // Lookups of a missing name leave empty entries
         if (!i->second)
            continue;
         HaxeNativeClass *native = i->second;
         out += "class " + native->name;
         hx::Class cls = hx::Class_obj::Resolve( String(native->name.c_str()) );
         if (cls.mPtr && cls->GetSuper().mPtr)
            out += " extends " + std::string(cls->GetSuper()->mName.utf8_str());
         out += "\n";

         std::vector<std::string> members;
         if (native->construct.signature)
            members.push_back(std::string("new ") + native->construct.signature);
         if (native->functions)
            for(ScriptNamedFunction *f=native->functions;f->name;f++)
               members.push_back(std::string(f->isStatic ? "static function " : "function ") + f->name + " " + (f->signature ? f->signature : ""));
         if (cls.mPtr)
         {
            if (cls->mMemberStorageInfo)
               for(const hx::StorageInfo *s=cls->mMemberStorageInfo; s->name.raw_ptr(); s++)
                  members.push_back("var " + std::string(s->name.utf8_str()) + " " + std::to_string((int)s->type));
            if (cls->mStaticStorageInfo)
               for(const hx::StaticInfo *s=cls->mStaticStorageInfo; s->name.raw_ptr(); s++)
                  members.push_back("static var " + std::string(s->name.utf8_str()) + " " + std::to_string((int)s->type));
         }
         std::sort(members.begin(), members.end());
         for(size_t m=0;m<members.size();m++)
            out += "  " + members[m] + "\n";
      }

   if (sScriptRegisteredInterface)
      for(HaxeNativeIntefaceMap::iterator i = sScriptRegisteredInterface->begin(); i!=sScriptRegisteredInterface->end();++i)
      {
         if (!i->second)
            continue;
         out += "interface " + i->second->name + "\n";
         std::vector<std::string> members;
         if (i->second->functions)
            for(ScriptNamedFunction *f=i->second->functions;f->name;f++)
               members.push_back(std::string("function ") + f->name + " " + (f->signature ? f->signature : ""));
         std::sort(members.begin(), members.end());
         for(size_t m=0;m<members.size();m++)
            out += "  " + members[m] + "\n";
      }
   return out;
}

// A 64-bit FNV-1a hash of hostExports, as 16 hex digits. Worked out on first
// use, after every class has registered at boot
const std::string &HaxeNativeClass::hostFingerprint()
{
   static const std::string fingerprint = []()
   {
      std::string exports = hostExports();
      unsigned long long hash = 14695981039346656037ULL;
      for(size_t i=0;i<exports.size();i++)
      {
         hash ^= (unsigned char)exports[i];
         hash *= 1099511628211ULL;
      }
      char buf[17];
      snprintf(buf, sizeof(buf), "%016llx", hash);
      return std::string(buf);
   }();
   return fingerprint;
}


// -- HaxeNativeInterface ---


ScriptFunction HaxeNativeInterface::findFunction(const std::string &inName)
{
   if (functions)
      for(ScriptNamedFunction *f=functions;f->name;f++)
         if (inName == f->name)
            return *f;
   //if (haxeSuper)
   //   return haxeSuper->findFunction(inName);

   return ScriptFunction(0,0);
}



void ScriptableRegisterClass( String inName, int inDataOffset, ScriptNamedFunction *inFunctions, hx::ScriptableClassFactory inFactory, hx::ScriptFunction inConstruct)
{
   DBGLOG("ScriptableRegisterClass %s\n", inName.out_str());
   if (!sScriptRegistered)
      sScriptRegistered = new ScriptRegisteredMap();
   HaxeNativeClass *registered = new HaxeNativeClass(inName.utf8_str(),inDataOffset, inFunctions,inFactory, inConstruct);
   (*sScriptRegistered)[inName.utf8_str()] = registered;
   //printf("Registering %s -> %p\n",inName.out_str(),(*sScriptRegistered)[inName.utf8_str()]);
}


HaxeNativeInterface::HaxeNativeInterface(const std::string &inName,
                                         ScriptNamedFunction *inFunctions,
                                         void *inScriptTable )
{
   name = inName;
   functions = inFunctions;
   scriptTable = inScriptTable;
}

void ScriptableRegisterInterface( String inName,
                                  ScriptNamedFunction *inFunctions,
                                  void *inScriptTable)
{
   DBGLOG("ScriptableInterfaceFactory %s\n",inName.out_str());
   if (!sScriptRegisteredInterface)
      sScriptRegisteredInterface = new HaxeNativeIntefaceMap();

   HaxeNativeInterface *registered = new HaxeNativeInterface(inName.utf8_str(), inFunctions, inScriptTable);
   (*sScriptRegisteredInterface)[inName.utf8_str()] = registered;
}


HaxeNativeInterface *HaxeNativeInterface::findInterface(const std::string &inName)
{
   if (!sScriptRegisteredInterface)
      return 0;
   HaxeNativeIntefaceMap::iterator it = sScriptRegisteredInterface->find(inName);
   return it==sScriptRegisteredInterface->end() ? 0 : it->second;
}




} // end namespace hx
