#include <stdio.h>
#include <stdlib.h>

#ifdef HXCPP_TELEMETRY
extern void __hxt_new_hash(void* obj, int size);
#endif

namespace hx
{


// Mix integer keys so that keys with low-bit structure (e.g. pointers,
// block-allocated ids, aligned/shifted values) do not collide catastrophically
// when the bucket index is taken as hash&mask over a power-of-two bucket count.
// Identity hashing funnels such keys into a few buckets (measured 26-275x
// slower lookups for strided keys); this fold spreads them while leaving
// well-distributed keys (and the common dense small-id case) unaffected.
// Int maps start on the identity hash and only switch to this mix when they
// see collisions (see Hash::keyHash); Int64 and object keys always use it.
inline unsigned int HashMixInt(unsigned int h)
{
   // Fold high bits down so strided keys (zero low bits) spread across buckets.
   // This is the identity for keys < 2^16, preserving cache locality for the
   // common small/dense key case (ids 0..N) while breaking power-of-two
   // clustering for large/strided keys. The multiply keeps keys packed as
   // (x << 16) | y from folding to x ^ y.
   h ^= (h >> 16) * 0x9E3779B1u;
   return h;
}

inline unsigned int HashCalcHash(int inKey) { return HashMixInt((unsigned int)inKey); }
// The high half is multiplied in rather than xored: xor folds a packed (x, y)
// pair to x ^ y, so a 256 x 256 grid had 256 hashes, and equal halves all
// to 0. A key with a zero high half hashes as the same Int does.
inline unsigned int HashCalcHash(cpp::Int64 inKey)
{
   unsigned int low = (unsigned int)inKey;
   unsigned int high = (unsigned int)((cpp::UInt64)inKey >> 32);
   return HashMixInt(low ^ (high * 0x9E3779B1u));
}
inline unsigned int HashCalcHash(const String &inKey) { return inKey.hash(); }
inline unsigned int HashCalcHash(const Dynamic &inKey)
{
   return HashMixInt(__hxcpp_obj_hash(inKey));
}

inline void HashClear(int &ioValue) { }
inline void HashClear(Dynamic &ioValue) { ioValue=null(); }
inline void HashClear(String &ioValue) { ioValue=String(); }
inline void HashClear(Float &ioValue) {  }
inline void HashClear(cpp::Int64 &ioValue) { }

template<typename T>
struct NeedsMarking { enum { Yes = 0 }; };
template<> struct NeedsMarking<Dynamic> { enum { Yes = 1 }; };
template<> struct NeedsMarking<String> { enum { Yes = 1 }; };

// An int element has key = hash
template<typename VALUE>
struct TIntElement
{
   typedef int   Key;
   typedef VALUE Value;

   enum { IgnoreHash = 1 };
   enum { WeakKeys = 0 };
   enum { ManageKeys = 0 };

   typedef TIntElement<int>        IntValue;
   typedef TIntElement<Float>      FloatValue;
   typedef TIntElement<Dynamic>    DynamicValue;
   typedef TIntElement<String>     StringValue;
   typedef TIntElement<cpp::Int64> Int64Value;

public:
   inline void  setKey(int inKey, unsigned int )
   {
      key = inKey;
   }
   // The bucket depends on the map's hash mode, so Hash computes it with
   // keyHash(key); this is only the raw key.
   inline unsigned int getHash()   { return (unsigned int)key; }

   Value               value;
   Key                 key;
   TIntElement<VALUE>  *next;
};

//
template<typename VALUE>
struct TInt64Element
{
   typedef cpp::Int64 Key;
   typedef VALUE Value;

   enum { IgnoreHash = 0 };
   enum { WeakKeys = 0 };
   enum { ManageKeys = 1 };

   typedef TInt64Element<int>        IntValue;
   typedef TInt64Element<Float>      FloatValue;
   typedef TInt64Element<Dynamic>    DynamicValue;
   typedef TInt64Element<String>     StringValue;
   typedef TInt64Element<cpp::Int64> Int64Value;

public:
   inline void setKey(cpp::Int64 inKey, unsigned int inHash)
   {
      key = inKey;
      hash = inHash;
   }
   inline unsigned int getHash() { return hash; }

   // hash beside a 4-byte value fills what would be padding (see TStringElement)
   Key                  key;
   unsigned int         hash;
   Value                value;
   TInt64Element<VALUE> *next;
};

// An string element gets hash from string or calculates it
template<typename VALUE>
struct TStringElement
{
   typedef String Key;
   typedef VALUE  Value;

   enum { IgnoreHash = 0 };
   enum { WeakKeys = 0 };
   enum { ManageKeys = 1 };

   typedef TStringElement<int>        IntValue;
   typedef TStringElement<Float>      FloatValue;
   typedef TStringElement<Dynamic>    DynamicValue;
   typedef TStringElement<String>     StringValue;
   typedef TStringElement<cpp::Int64> Int64Value;


public:
   inline void  setKey(String inKey, unsigned int inHash)
   {
      key = inKey;
      hash = inHash;
   }
   inline unsigned int getHash() { return hash; }

   // With value first, an Int value and the hash each left 4 bytes of
   //  padding: 40 bytes on 64-bit where 32 hold the same fields
   Key                    key;
   unsigned int           hash;
   Value                  value;
   TStringElement<VALUE>  *next;
};


struct TWeakStringSet
{
   typedef null Value;
   typedef String Key;

   enum { IgnoreHash = 0 };
   enum { WeakKeys = 1 };
   enum { ManageKeys = 1 };

   typedef TWeakStringSet  IntValue;
   typedef TWeakStringSet  FloatValue;
   typedef TWeakStringSet  DynamicValue;
   typedef TWeakStringSet  StringValue;
   typedef TWeakStringSet  Int64Value;


public:
   inline void  setKey(String inKey, unsigned int inHash)
   {
      key = inKey;
      hash = inHash;
   }
   inline unsigned int getHash() { return hash; }

   Key                key;
   unsigned int       hash;
   Value              value;
   TWeakStringSet     *next;
};


struct TNonGcStringSet
{
   typedef null Value;
   typedef String Key;

   enum { IgnoreHash = 0 };
   enum { WeakKeys = 1 };
   enum { ManageKeys = 0 };

   typedef TNonGcStringSet  IntValue;
   typedef TNonGcStringSet  FloatValue;
   typedef TNonGcStringSet  DynamicValue;
   typedef TNonGcStringSet  StringValue;
   typedef TNonGcStringSet  Int64Value;


public:
   inline void  setKey(String inKey, unsigned int inHash)
   {
      key = inKey;
      hash = inHash;
   }
   inline unsigned int getHash() { return hash; }

   Key                key;
   unsigned int       hash;
   Value              value;
   TNonGcStringSet    *next;
};




// An Dyanamic element must use the GC code to get a hash
template<typename VALUE,bool WEAK>
struct TDynamicElement
{
   typedef Dynamic Key;
   typedef VALUE   Value;

   enum { IgnoreHash = 0 };
   enum { WeakKeys = WEAK };
   enum { ManageKeys = 1 };

   typedef TDynamicElement<int,WEAK>        IntValue;
   typedef TDynamicElement<Float,WEAK>      FloatValue;
   typedef TDynamicElement<Dynamic,WEAK>    DynamicValue;
   typedef TDynamicElement<String,WEAK>     StringValue;
   typedef TDynamicElement<cpp::Int64,WEAK> Int64Value;

public:
   inline void  setKey(Dynamic inKey, unsigned int inHash)
   {
      key = inKey;
      hash = inHash;
   }
   inline unsigned int getHash() { return hash; }

   // hash beside a 4-byte value fills what would be padding (see TStringElement)
   Key                          key;
   unsigned int                 hash;
   Value                        value;
   TDynamicElement<VALUE,WEAK>  *next;
};




enum HashStore
{
   hashInt,
   hashFloat,
   hashString,
   hashObject,
   hashNull,
   hashInt64
};
template<typename T> struct StoreOf{ enum {store=hashObject}; };
template<> struct StoreOf<int> { enum {store=hashInt}; };
template<> struct StoreOf< ::String> { enum {store=hashString}; };
template<> struct StoreOf<Float> { enum {store=hashFloat}; };
template<> struct StoreOf<null> { enum {store=hashNull}; };
template<> struct StoreOf<cpp::Int64> { enum {store=hashInt64}; };

namespace
{
inline void CopyValue(Dynamic &outValue, const Dynamic &inValue) { outValue = inValue; }
inline void CopyValue(String &outValue, const String &inValue) { outValue = inValue; }
inline void CopyValue(String &outValue, Float inValue) {  }
inline void CopyValue(String &outValue, const Dynamic &inValue) { outValue = inValue; }
inline void CopyValue(int &outValue, int inValue) { outValue = inValue; }
inline void CopyValue(int &outValue, cpp::Int64 inValue) { }
inline void CopyValue(int &outValue, Float inValue) { outValue = (int)inValue; }
inline void CopyValue(int &outValue, const Dynamic &inValue) { outValue = inValue; }
inline void CopyValue(int &outValue, const String &inValue) {  }
inline void CopyValue(Float &outValue, Float inValue) { outValue = inValue; }
inline void CopyValue(Float &outValue, const Dynamic &inValue) { outValue = inValue; }
inline void CopyValue(Float &outValue, const String &inValue) {  }
inline void CopyValue(cpp::Int64 &outValue, cpp::Int64 inValue) { outValue = inValue; }
inline void CopyValue(cpp::Int64 &outValue, int inValue) { outValue = inValue; }
inline void CopyValue(cpp::Int64 &outValue, Float inValue) { }
inline void CopyValue(cpp::Int64 &outValue, const Dynamic &inValue) { outValue = inValue; }
inline void CopyValue(cpp::Int64 &outValue, const String &inValue) {  }
inline void CopyValue(null &, const null &) {  }
template<typename T> inline void CopyValue(T &outValue, const null &) {  }
template<typename T> inline void CopyValue(null &, const T &) {  }
}


struct HashRoot;
// A weak hash entry whose object key was not marked when its value was looked
//  at: the collector marks the value as soon as it marks the key
void EphemeronWait(hx::Object *inKey, HashRoot *inHash, void *inElement);

struct HashRoot : public Object
{
   HashStore store;
   int       size;
   int       mask;
   int       bucketCount;
   #ifdef HXCPP_GC_GENERATIONAL
   // The young values logged for this map in the cycle (hx::YoungStore)
   unsigned int mYoungLogged;
   #endif

    HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdHash };

   virtual void updateAfterGc() = 0;
   virtual bool markWeakValues(MarkContext *__inCtx) = 0;
   // Marks the value of an element that markWeakValues passed to EphemeronWait
   virtual void markWeakElement(void *inElement, MarkContext *__inCtx) = 0;
   // _hx_gc_freeze copied the keys into new objects, which hash differently
   //  where the hash is the object's identity
   virtual void rehashKeys() = 0;

   inline int getSize() { return size; }
};

// How many maps this thread is formatting, one inside another.  A map that
//  held itself, directly or through other maps, recursed until the native
//  stack overflowed; past 5, as for arrays and anonymous objects, a map
//  formats as "...".  Per thread, and given back when a key or value's
//  toString throws
int &HashToStringDepth();

struct ToStringDepth
{
   bool tooDeep;
   ToStringDepth()
   {
      int &depth = HashToStringDepth();
      tooDeep = depth >= 5;
      if (!tooDeep)
         depth++;
   }
   ~ToStringDepth()
   {
      if (!tooDeep)
         HashToStringDepth()--;
   }
};

template<typename KEY>
struct HashBase : public HashRoot
{
   HashBase(int inStore)
   {
      store = (HashStore)inStore;
      size = 0;
      mask = 0;
      bucketCount = 0;
      #ifdef HXCPP_GC_GENERATIONAL
      mYoungLogged = 0;
      #endif
   }


   virtual bool query(KEY inKey,int &outValue) = 0;
   virtual bool query(KEY inKey,::String &outValue) = 0;
   virtual bool query(KEY inKey,Float &outValue) = 0;
   virtual bool query(KEY inKey,Dynamic &outValue) = 0;
   virtual bool query(KEY inKey,cpp::Int64 &outValue) = 0;

   virtual void set(KEY inKey, const int &inValue) = 0;
   virtual void set(KEY inKey, const ::String &inValue) = 0;
   virtual void set(KEY inKey, const Float &inValue) = 0;
   virtual void set(KEY inKey, const Dynamic &inValue) = 0;
   virtual void set(KEY inKey, const cpp::Int64 &inValue) = 0;

   virtual void clear() = 0;

   virtual HashBase<KEY> *convertStore(HashStore inStore) = 0;

   virtual bool remove(KEY inKey) = 0;
   virtual bool exists(KEY inKey) = 0;
   virtual Array<KEY> keys() = 0;
   virtual Dynamic values() = 0;

   virtual ::String toStringRaw() { return toString(); }
};

extern void RegisterWeakHash(HashBase<Dynamic> *);
extern void RegisterWeakHash(HashBase< ::String> *);

inline void RegisterWeakHash(HashBase<int> *) { };
inline void RegisterWeakHash(HashBase<cpp::Int64> *) { };


template<typename T>
struct ArrayValueOf{ typedef T Value; };
template<> struct ArrayValueOf<null> { typedef Dynamic Value; };

template<typename ELEMENT>
struct Hash : public HashBase< typename ELEMENT::Key >
{
   using HashRoot::size;
   using HashRoot::mask;
   using HashRoot::bucketCount;
   using HashRoot::getSize;

   typedef typename ELEMENT::Key   Key;
   typedef typename ELEMENT::Value Value;
   typedef typename ArrayValueOf<Value>::Value ArrayValue;

   typedef ELEMENT Element;
   enum { IgnoreHash = Element::IgnoreHash };


   ELEMENT **bucket;
   // A collection folded the buckets down in place, so the array is bigger
   //  than bucketCount needs until the next insert or remove reallocates it
   bool bucketsOversized;

   // Int keys start on the identity hash: it matches stock hxcpp and spreads
   // arithmetic keys (ids, i*odd, offsets) with no collisions at all. Keys
   // with low-bit structure (strides, aligned values) pile into a few buckets
   // under it, so the map switches to HashMixInt for good once it sees long
   // chains (#231). Other key types always use HashCalcHash.
   bool mixIntKeys;

   // Walks over the buckets in progress, which a collection must not fold
   //  under: one that allocates, as converting the map does, can let it run
   unsigned short walks;

   // An insert that walks this many entries switches to the mixed hash. With
   // a good hash and load <= 1 a chain this long practically never happens.
   enum { MIX_CHAIN_LIMIT = 16 };


   Hash() : HashBase<Key>( StoreOf<Value>::store )
   {
      bucket = 0;
      bucketsOversized = false;
      mixIntKeys = false;
      walks = 0;
      if (ELEMENT::WeakKeys && Element::ManageKeys)
         RegisterWeakHash(this);
   }

   inline unsigned int keyHash(int inKey)
   {
      return mixIntKeys ? HashMixInt((unsigned int)inKey) : (unsigned int)inKey;
   }
   template<typename K>
   inline unsigned int keyHash(const K &inKey) { return HashCalcHash(inKey); }

   inline unsigned int elementHash(Element &e)
   {
      return IgnoreHash ? keyHash(e.key) : e.getHash();
   }

   template<typename T>
   bool TIsWeakRefValid(T &) { return true; }
   bool TIsWeakRefValid(Dynamic &key) { return IsWeakRefValid(key.mPtr); }
   bool TIsWeakRefValid(String &key) { return IsWeakRefValid(key.raw_ptr()); }

   // String keys are left to the next pass over the table
   template<typename T>
   void TWaitForKey(T &, Element *) { }
   void TWaitForKey(Dynamic &key, Element *inElement) { EphemeronWait(key.mPtr, this, inElement); }

   template<typename T>
   bool TMarkWeakValue(T &, MarkContext *) { return false; }
   bool TMarkWeakValue(Dynamic &inValue, MarkContext *__inCtx)
   {
      hx::Object *value = inValue.mPtr;
      if (!value || (((unsigned int *)value)[-1] & hx::gPrevMarkIdMask))
         return false;

      HX_MARK_MEMBER(inValue);
      return true;
   }
   bool TMarkWeakValue(String &inValue, MarkContext *__inCtx)
   {
      const HX_CHAR *value = inValue.raw_ptr();
      if (!value || (((unsigned int *)value)[-1] & hx::gPrevMarkIdMask))
         return false;

      HX_MARK_MEMBER(inValue);
      return true;
   }

   bool markWeakValues(MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      // Int, Float, Int64 and null values reach nothing to mark, and the map
      //  keeps this storage until it converts to another Hash. The walk took
      //  longer than the rest of a minor collection (libhxcpp #333)
      enum { ManagedValues = StoreOf<Value>::store==hashString || StoreOf<Value>::store==hashObject };
      if (!Element::WeakKeys || !Element::ManageKeys || !ManagedValues)
         return false;

      bool marked = false;
      for(int b=0;b<bucketCount;b++)
      {
         Element *element = bucket[b];
         while(element)
         {
            if (TIsWeakRefValid(element->key))
               marked = TMarkWeakValue(element->value, __inCtx) || marked;
            else
               TWaitForKey(element->key, element);
            element = element->next;
         }
      }
      return marked;
   }

   void markWeakElement(void *inElement, MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      TMarkWeakValue(((Element *)inElement)->value, __inCtx);
   }


   void updateAfterGc() HXCPP_OVERRIDE
   {
      if (Element::WeakKeys && Element::ManageKeys)
      {
         for(int b=0;b<bucketCount;b++)
         {
            Element **headPtr = &bucket[b];
            while(*headPtr)
            {
               Element &el = **headPtr;
               if (!TIsWeakRefValid(el.key))
               {
                  *headPtr = el.next;
                  size--;
               }
               else
                  headPtr = &el.next;
            }
         }
         // Nothing left: let the bucket array go rather than keep, and scan,
         // the size it reached. Only references change here, as nothing may
         // be allocated during a collection; the next set allocates again
         if (size==0 && bucket)
         {
            bucket = 0;
            bucketCount = 0;
            mask = 0;
            bucketsOversized = false;
         }
         // Most keys gone: fold the buckets down, in place, to the count that
         // remove() would compact to, so the collections that follow scan the
         // buckets in use rather than the most there ever were. The array keeps
         // its size until the next insert or remove, which may allocate
         // (libhxcpp #204). Not during a walk: entries moved into buckets it
         // has passed would be skipped. A later collection folds instead
         else if (bucketCount>8 && size < (bucketCount>>2) && !walks)
         {
            int newCount = bucketCount;
            while(newCount>8 && size < (newCount>>2))
               newCount >>= 1;
            foldBuckets(newCount);
            bucketsOversized = true;
         }
      }
   }

   // Moves the elements of buckets [inNewCount,bucketCount) into the first
   // inNewCount, which becomes the count. Allocates nothing
   void foldBuckets(int inNewCount)
   {
      int newMask = inNewCount-1;
      for(int b=inNewCount; b<bucketCount; b++)
      {
         Element *head = bucket[b];
         if (head)
         {
            Element **lastPtr = &(head->next);
            while(*lastPtr)
               lastPtr = & (*lastPtr)->next;
            *lastPtr = bucket[b & newMask];
            bucket[b & newMask] = head;
            bucket[b] = 0;
         }
      }
      mask = newMask;
      bucketCount = inNewCount;
   }

   // An array a collection folded down gets the size its count needs
   void releaseOversizedBuckets()
   {
      if (bucketsOversized)
      {
         bucketsOversized = false;
         bucket = (Element **)InternalRealloc(bucketCount*sizeof(ELEMENT *),bucket, sizeof(ELEMENT *)*bucketCount );
         HX_OBJ_WB_GET(this, bucket);
      }
   }

   void rebucket(int inNewCount)
   {
#ifdef HXCPP_TELEMETRY
      bool is_new = bucket==0;
#endif
      mask = inNewCount-1;
      //printf("expand %d -> %d\n",bucketCount, inNewCount);
      bucket = (Element **)InternalRealloc(bucketCount*sizeof(ELEMENT *), bucket,inNewCount*sizeof(ELEMENT *));
      HX_OBJ_WB_GET(this, bucket);
      //for(int b=bucketCount;b<inNewCount;b++)
      //   bucket[b] = 0;

#ifdef HXCPP_TELEMETRY
      if (is_new) __hxt_new_hash(bucket, inNewCount*sizeof(ELEMENT *));
#endif

      redistribute(bucketCount);

      bucketCount = inNewCount;
   }

   // Moves every element in buckets [0,inLimit) to bucket elementHash&mask.
   // Elements only move to buckets that are either already processed or
   // will be processed later and then stay put, so this works both for
   // growth (inLimit = old count) and for an in-place rehash (full count).
   void redistribute(int inLimit)
   {
      for(int b=0;b<inLimit;b++)
      {
         Element **head = &bucket[b];
         while(*head)
         {
            Element &e = **head;
            int newBucket = elementHash(e)&mask;
            if ( newBucket != b )
            {
               *head = e.next;
               e.next = bucket[newBucket];
               bucket[newBucket] = &e;
            }
            else
               head = &e.next;
         }
      }
   }

   template<typename V,bool W>
   void rehashElement(TDynamicElement<V,W> &e) { e.hash = keyHash(e.key); }
   template<typename E>
   void rehashElement(E &) { }

   void rehashKeys() HXCPP_OVERRIDE
   {
      if (IgnoreHash || !bucket)
         return;
      for(int b=0;b<bucketCount;b++)
         for(Element *e=bucket[b]; e; e=e->next)
            rehashElement(*e);
      redistribute(bucketCount);
   }

   void switchToMixedHash()
   {
      HX_INCREMENTAL_MUTATION(this);
      mixIntKeys = true;
      if (bucketCount)
         redistribute(bucketCount);
   }

   // After growth a good hash leaves at most ~1.6 entries per used bucket.
   // More than 2 means the keys have structure the identity hash can't spread.
   void checkIntSpread()
   {
      if (!IgnoreHash || mixIntKeys || size<64)
         return;
      int used = 0;
      for(int b=0;b<bucketCount;b++)
         if (bucket[b])
            used++;
      if (size > used*2)
         switchToMixedHash();
   }

   void reserve(int inSize)
   {
      if (inSize<8)
          inSize = 8;

      expandBuckets(inSize);
   }

   void compact()
   {
      // printf("compact -> %d\n", bucketCount>>1);
      foldBuckets(bucketCount>>1);
      bucketsOversized = false;
      bucket = (Element **)InternalRealloc(bucketCount*sizeof(ELEMENT *),bucket, sizeof(ELEMENT *)*bucketCount );
      HX_OBJ_WB_GET(this, bucket);
   }

   bool remove(Key inKey) HXCPP_OVERRIDE
   {
      HX_INCREMENTAL_MUTATION(this);
      if (!bucket)
         return false;
      releaseOversizedBuckets();
      unsigned int hash = keyHash(inKey);
      Element **head = bucket + (hash&mask);
      while(*head)
      {
         Element &el = **head;
         if ( (IgnoreHash || el.getHash()==hash) && el.key==inKey)
         {
            *head = el.next;
            size--;
            // Shrink only once a quarter full. Growth doubles (or quadruples)
            // past size == bucketCount, so shrinking at half full meant a map
            // just past a growth step shrank after a few removes and grew again
            // when keys came back (#225). After compact() the load is <= 0.5.
            if (bucketCount>8 && size < (bucketCount>>2) )
               compact();
            return true;
         }
         head = &el.next;
      }
      return false;
   }

   ELEMENT *allocElement()
   {
      releaseOversizedBuckets();
      ELEMENT *result = (ELEMENT *)InternalNew( sizeof(ELEMENT), false );
      size++;
      expandBuckets(size);
      return result;
   }

   inline void expandBuckets(int inSize)
   {
      // Trades memory vs bucket occupancy - more memory is used for elements anyhow, so not too critical.
      // Buckets are powers of two, so this grows when size > bucketCount, giving an actual load factor
      // that ranges 0.5..1.0 (avg ~0.75). Measured ~16-26% faster map lookups vs the previous value of 1
      // (which allowed chains to average 2), for ~10-12% more total map memory (bucket array only).
      enum { LOG_ELEMS_PER_BUCKET = 0 };
      if ( inSize > (bucketCount<<LOG_ELEMS_PER_BUCKET) )
      {
         // Grow 2x while small (keeps the common small-map case memory-tight),
         // then 4x once large. Large maps are where rehashing dominates build
         // cost; quadrupling halves the number of expensive late rehashes, at
         // the price of a little more bucket-array slack on the few big maps.
         enum { QUAD_GROW_BUCKETS = 4096 };
         int newCount = bucketCount ? bucketCount : 2;
         while( inSize > (newCount<<LOG_ELEMS_PER_BUCKET) )
            newCount <<= (newCount < QUAD_GROW_BUCKETS ? 1 : 2);
         if (newCount!=bucketCount)
         {
            rebucket(newCount);
            checkIntSpread();
         }
      }
   }

   Element *find(int inHash, Key inKey)
   {
      if (!bucket) return 0;
      Element *head = bucket[inHash & mask];
      while(head)
      {
         if ( (IgnoreHash || head->getHash()==inHash) && head->key==inKey)
            return head;
         head = head->next;
      }
      return 0;
   }

   bool exists(Key inKey) HXCPP_OVERRIDE { return find( keyHash(inKey), inKey ); }


   HashBase<Key> *convertStore(HashStore inStore) HXCPP_OVERRIDE
   {
      switch(inStore)
      {
         case hashInt:
            return TConvertStore< typename ELEMENT::IntValue >();
         case hashFloat:
            return TConvertStore< typename ELEMENT::FloatValue >();
         case hashString:
            return TConvertStore< typename ELEMENT::StringValue >();
         case hashObject:
            return TConvertStore< typename ELEMENT::DynamicValue >();
         case hashInt64:
            return TConvertStore< typename ELEMENT::Int64Value >();
         case hashNull:
             ;
      }
      return 0;
   }


   template<typename OUT_VALUE>
   bool TQuery(Key inKey,OUT_VALUE &outValue)
   {
      Element *result = find( keyHash(inKey), inKey );
      if (!result)
         return false;
      CopyValue(outValue,result->value);
      return true;
   }

   bool query(Key inKey,int &outValue) HXCPP_OVERRIDE { return TQuery(inKey,outValue); }
   bool query(Key inKey,::String &outValue) HXCPP_OVERRIDE { return TQuery(inKey,outValue); }
   bool query(Key inKey,Float &outValue) HXCPP_OVERRIDE { return TQuery(inKey,outValue); }
   bool query(Key inKey,Dynamic &outValue) HXCPP_OVERRIDE { return TQuery(inKey,outValue); }
   bool query(Key inKey,cpp::Int64 &outValue) HXCPP_OVERRIDE { return TQuery(inKey, outValue); }


   Value get(Key inKey)
   {
      Element *result = find( keyHash(inKey), inKey );
      if (result)
         return result->value;
      return 0;
   }


   template<typename Finder>
   bool findEquivalentKey(Key &outKey, int inHash, const Finder &inFinder)
   {
      if (!bucket) return false;
      Element *head = bucket[inHash & mask];
      while(head)
      {
         if ( (IgnoreHash || head->getHash()==inHash) && inFinder==head->key)
         {
            outKey = head->key;
            return true;
         }
         head = head->next;
      }
      return false;
   }

   static inline bool IsNursery(const void *inPtr)
   {
      return inPtr && !hx::BarrierMarkByte(inPtr);
   }

   #ifdef HXCPP_GC_GENERATIONAL
   // The barrier for young values stored in this map while it is old. A
   //  large strong map logs them (hx::YoungStore) rather than be remembered
   //  whole. A weak map's values live only with their keys, which marking
   //  the map decides, so it is remembered whole
   void rememberYoung(const void *inA, const void *inB, const void *inC)
   {
      #ifdef HXCPP_GC_INCREMENTAL
      if (Element::WeakKeys) hx::IncrementalRemember(this,nullptr,HX_CTX_GET);
      else {
         if (inA) hx::IncrementalRemember(this,inA,HX_CTX_GET);
         if (inB) hx::IncrementalRemember(this,inB,HX_CTX_GET);
         if (inC) hx::IncrementalRemember(this,inC,HX_CTX_GET);
      }
      #else
      if (hx::BarrierMarkByte(this) == hx::gByteMarkID && (IsNursery(inA) || IsNursery(inB) || IsNursery(inC)) )
      {
         if (Element::WeakKeys)
         {
            hx::BarrierRemember(this);
            (HX_CTX_GET)->pushReferrer(this);
         }
         else
         {
            const void *values[] = { inA, inB, inC };
            hx::YoungStore(this, this->mYoungLogged, size, values, 3, HX_CTX_GET);
         }
      }
      #endif
   }
   #endif

   template<typename SET>
   void TSet(Key inKey, const SET &inValue)
   {
      unsigned int hash = keyHash(inKey);
      Element *el = 0;
      if (IgnoreHash && !mixIntKeys)
      {
         // Same search as find(), counting the chain on the way.
         int chain = 0;
         if (bucket)
            for(el = bucket[hash & mask]; el && !(el->key==inKey); el = el->next)
               chain++;
         if (!el && chain>=MIX_CHAIN_LIMIT)
            switchToMixedHash();
      }
      else
         el = find(hash,inKey);
      if (el)
      {
         CopyValue(el->value,inValue);
         #ifdef HXCPP_GC_GENERATIONAL
         if (hx::ContainsPointers<Value>())
            rememberYoung(hx::PointerOf(el->value), 0, 0);
         #endif
         return;
      }
      HX_INCREMENTAL_MUTATION(this);
      el = allocElement();
      // Growth in allocElement can switch an int map to the mixed hash.
      if (IgnoreHash)
         hash = keyHash(inKey);
      el->setKey(inKey,hash);
      CopyValue(el->value,inValue);
      el->next = bucket[hash&mask];
      bucket[hash&mask] = el;

      #ifdef HXCPP_GC_GENERATIONAL
      // The new element, and its key and value, may be nursery objects
      rememberYoung(el, hx::PointerOf(el->key),
                    hx::ContainsPointers<Value>() ? hx::PointerOf(el->value) : 0);
      #endif
   }

   void set(Key inKey, const int &inValue) HXCPP_OVERRIDE { TSet(inKey, inValue); }
   void set(Key inKey, const ::String &inValue) HXCPP_OVERRIDE { TSet(inKey, inValue); }
   void set(Key inKey, const Float &inValue) HXCPP_OVERRIDE { TSet(inKey, inValue); }
   void set(Key inKey, const Dynamic &inValue) HXCPP_OVERRIDE { TSet(inKey, inValue); }
   void set(Key inKey, const null &inValue)  { TSet(inKey, inValue);  }
   void set(Key inKey, const cpp::Int64 &inValue) HXCPP_OVERRIDE { TSet(inKey, inValue); }

   void clear() HXCPP_OVERRIDE
   {
      HX_INCREMENTAL_MUTATION(this);
      bucket = 0;
      bucketsOversized = false;
      size = 0;
      mask = 0;
      bucketCount = 0;
   }


   // A weak map holds its keys and values while a walk is on them. A
   //  collection the walk allocated for, as toString does, unlinked the
   //  entries whose keys died, and the walk went on from its entry's next,
   //  which still led to them, to keys already freed. The map is marked again
   //  where a collection might otherwise skip it: an old map in a minor
   //  collection, or one an incremental cycle has already scanned
   struct AutoWalk
   {
      Hash *hash;
      AutoWalk(Hash *inHash) : hash(inHash)
      {
         hash->walks++;
         if (Element::WeakKeys)
            HX_OBJ_WB_PESSIMISTIC_GET(hash);
      }
      ~AutoWalk() { hash->walks--; }
   };

   template<typename F>
   void iterate(F &inFunc)
   {
      AutoWalk walk(this);
      iterateBuckets(inFunc);
   }

   // Without counting a walk, for the collector: it folds only after marking,
   //  and mark threads can mark one map at once, so their ++ and -- would race
   template<typename F>
   void iterateBuckets(F &inFunc)
   {
      for(int b=0;b<bucketCount;b++)
      {
         Element *el = bucket[b];
         while(el)
         {
            inFunc(el);
            el = el->next;
         }
      }
   }

   // Convert
   template<typename NEW>
   struct Converter
   {
      NEW *result;

      Converter(NEW *inResult) : result(inResult) { }

      void operator()(typename Hash::Element *elem)
      {
         result->set(elem->key,elem->value);
      }
   };


   template<typename NEW_ELEM>
   HashBase<Key> *TConvertStore()
   {
      Hash<NEW_ELEM> *result = new Hash<NEW_ELEM>();

      result->reserve(getSize()*3/2);

      Converter< Hash<NEW_ELEM> > converter(result);

      iterate(converter);

      return result;
   }



   // Keys ...
   struct KeyBuilder
   {
      Array<Key> array;

      KeyBuilder(int inReserve = 0)
      {
         array = Array<Key>(0,inReserve);
      }
      void operator()(typename Hash::Element *elem)
      {
         array->push(elem->key);
      }
   };
   Array<Key> keys() HXCPP_OVERRIDE
   {
      KeyBuilder builder(getSize());
      iterate(builder);
      return builder.array;;
   }


   // Values...
   struct ValueBuilder
   {
      Array<ArrayValue> array;
      ValueBuilder(int inReserve = 0)
      {
         array = Array<ArrayValue>(0,inReserve);
      }

      void operator()(typename Hash::Element *elem)
      {
         array->push(elem->value);
      }
   };
   Dynamic values() HXCPP_OVERRIDE
   {
      ValueBuilder builder(getSize());
      iterate(builder);
      return builder.array;;
   }


   // Strings ...
   struct StringBuilder
   {
      Array<String> array;
      bool raw;

      StringBuilder(int inReserve = 0,bool inRaw=false)
      {
         raw = inRaw;
         array = Array<String>(0,inReserve*4+1);
         if (!raw) {
            #if (HXCPP_API_LEVEL >= 430)
            array->push(HX_CSTRING("["));
            #else
            array->push(HX_CSTRING("{ "));
            #endif
         }
      }
      void operator()(typename Hash::Element *elem)
      {
         if (array->length>1)
            array->push(HX_CSTRING(", "));
         array->push(String(elem->key));
         array->push(HX_CSTRING(" => "));
         array->push(String(elem->value));
      }
      ::String toString()
      {
         if (!raw) {
            #if (HXCPP_API_LEVEL >= 430)
            array->push(HX_CSTRING("]"));
            #else
            array->push(HX_CSTRING(" }"));
            #endif
         }
         return array->length==0 ? String() : array->join(HX_CSTRING(""));
      }
   };

   String toString() HXCPP_OVERRIDE
   {
      ToStringDepth depth;
      if (depth.tooDeep)
         return HX_CSTRING("...");
      StringBuilder builder(getSize());
      iterate(builder);
      return builder.toString();
   }


   String toStringRaw() HXCPP_OVERRIDE
   {
      ToStringDepth depth;
      if (depth.tooDeep)
         return HX_CSTRING("...");
      StringBuilder builder(getSize(),true);
      iterate(builder);
      return builder.toString();
   }


   // Mark ...
   // inStrong marks the keys and values of a weak map too: see AutoWalk
   struct HashMarker
   {
      hx::MarkContext *__inCtx;
      bool strong;
      HashMarker(hx::MarkContext *ctx, bool inStrong) : __inCtx(ctx), strong(!Hash::Element::WeakKeys || inStrong) { }
      void operator()(typename Hash::Element *inElem)
      {
         HX_MARK_ARRAY(inElem);
         if (strong)
         {
            HX_MARK_MEMBER(inElem->key);
            HX_MARK_MEMBER(inElem->value);
         }
      }
   };

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_MARK_ARRAY(bucket);

      #ifdef HXCPP_GC_INCREMENTAL
      if (size > 256 && hx::IncrementalRange(this,IncrementalScan,__inCtx)) return;
      #endif

      HashMarker marker(__inCtx, walks!=0);
      iterateBuckets(marker);
   }

   #ifdef HXCPP_GC_INCREMENTAL
   static bool IncrementalScan(hx::Object *inOwner, size_t &ioBucket, size_t &ioItem, hx::MarkContext *__inCtx) {
      Hash *hash = static_cast<Hash *>(inOwner);
      HashMarker marker(__inCtx, hash->walks!=0);
      Element *element = reinterpret_cast<Element *>(ioItem);
      unsigned int work = 0;
      while (ioBucket < static_cast<size_t>(hash->bucketCount) && work < 256) {
         if (!element) element = hash->bucket[ioBucket];
         if (!element) { ioBucket++; work++; continue; }
         while (element && work < 256) {
            marker(element);
            element = element->next;
            work++;
         }
         if (!element) ioBucket++;
      }
      ioItem = reinterpret_cast<size_t>(element);
      return ioBucket >= static_cast<size_t>(hash->bucketCount);
   }
   #endif

#ifdef HXCPP_VISIT_ALLOCS

   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
   {
      //printf(" visit hash %p\n", this);
      HX_VISIT_ARRAY(bucket);
      for(int b=0;b<bucketCount;b++)
      {
         HX_VISIT_ARRAY(bucket[b]);
         Element *el = bucket[b];
         while(el)
         {
            HX_VISIT_MEMBER(el->key);
            HX_VISIT_MEMBER(el->value);
            HX_VISIT_ARRAY(el->next);
            el = el->next;
         }
      }
   }
#endif
};


} // end namespace hx

