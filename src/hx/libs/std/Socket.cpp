#if !defined(HX_WINRT) && !defined(EPPC)
#include <hxcpp.h>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <hx/OS.h>

#include <string.h>
#include <stddef.h>


#ifdef NEKO_WINDOWS

#ifdef __GNUC__
   // Mingw / gcc on windows
   #ifndef _WIN32_WINNT
   #define _WIN32_WINNT 0x0501
   #endif
   #include <winsock2.h>
   #include <ws2tcpip.h>
#else
   // Windows...
   #include <winsock2.h>
   #include <In6addr.h>
   #include <Ws2tcpip.h>
#endif

#define DYNAMIC_INET_FUNCS 1
typedef INT (WSAAPI *inet_pton_func)( INT Family, PCSTR pszAddrString, PVOID pAddrBuf);
typedef PCSTR (WSAAPI *inet_ntop_func)(INT  Family, PVOID pAddr, PSTR pStringBuf, size_t StringBufSize);

// A WS2_32 function looked up on first use, from any thread. The answer is
// published only once complete, and a thread that finds none yet looks it
// up too: a flag set before the lookup let another thread take a lookup
// still in progress for a missing function, and format ::1 as null.
struct DynamicInetFunc
{
   const char *name;
   std::atomic<void *> func;
   std::atomic<bool> tried;

   void *get()
   {
      if (!tried.load(std::memory_order_acquire))
      {
         void *found = 0;
         HMODULE module = LoadLibraryA("WS2_32.dll");
         if (module)
            found = (void *)GetProcAddress(module,name);
         func.store(found, std::memory_order_relaxed);
         tried.store(true, std::memory_order_release);
      }
      return func.load(std::memory_order_relaxed);
   }
};

// fd_array does not begin at sizeof(u_int). On 64-bit Windows SOCKET is
// eight bytes, so fd_set carries four bytes of padding after fd_count and
// the array starts at offset 8. Sizing the copy as 4 + n*8 truncated the
// first handle by half and left the rest as whatever malloc returned, so
// select() saw a handle that was not a socket and failed with WSAENOTSOCK.
#   define FDSIZE(n)   (offsetof(fd_set, fd_array) + (n) * sizeof(SOCKET))
#   define SHUT_WR      SD_SEND
#   define SHUT_RD      SD_RECEIVE
#   define SHUT_RDWR   SD_BOTH
   static bool init_done = false;
   static WSADATA init_data;
typedef int SocketLen;
// Windows 7 SP1 and later; older SDK and MinGW headers lack the name
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#   define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif
#else
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#   include <netinet/in.h>
#  include <netinet/tcp.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <netdb.h>
#   include <fcntl.h>
#   include <errno.h>
#   include <stdlib.h>
#   include <stdio.h>
#   include <poll.h>
#   include <vector>
   typedef int SOCKET;
#   define closesocket close
#   define SOCKET_ERROR (-1)
#   define INVALID_SOCKET (-1)
typedef socklen_t SocketLen;
// Poll keeps its registrations in the kernel where there is a way to
#   if defined(NEKO_LINUX) && !defined(__EMSCRIPTEN__)
#      include <sys/epoll.h>
#      define HX_POLL_QUEUE
#      define HX_POLL_EPOLL
#   elif defined(NEKO_MAC) || defined(NEKO_BSD)
#      include <sys/event.h>
#      define HX_POLL_QUEUE
#      define HX_POLL_KQUEUE
#   endif
#endif

#if (defined(NEKO_WINDOWS) || defined(NEKO_MAC)) && !defined(MSG_NOSIGNAL)
#   define MSG_NOSIGNAL 0
#endif



namespace
{

static int socketType = 0;

// The stream sockets holding bytes read ahead, so that select and poll look
// for them only while there are some
static std::atomic<int> sReadAheadHeld(0);
// Counts socket_close calls, so that a poll that keeps its registrations
// looks for a closed socket only after one was closed
static std::atomic<unsigned int> sSocketsClosed(0);
static std::atomic<unsigned int> sNextSocketSerial(0);

static const int SOCKET_READ_AHEAD = 4096;

// The bytes a stream socket received past those asked for. A byte at a time,
// socket_recv_char called recv for each one in its own GC-free zone, 0.6 us
// a byte; it takes up to SOCKET_READ_AHEAD bytes the kernel already has
// instead, and every other read takes these first. recv with a larger
// buffer returns as soon as one byte is there, so blocking, timeouts and
// nonblocking reads are as they were. select and poll report a socket that
// holds bytes as readable, and a socket handed to TLS reads ahead no more.
//
// One read at a time holds readers, from before it looks at the bytes held
// until after its recv, so two threads reading the same stream each see
// their bytes in order: one whose recv returned later than another thread
// took bytes ahead could otherwise have gone on to take bytes from before
// its own. close and shutdown never wait for it, since a read blocked in
// recv can be waiting for exactly that.
#ifndef HXCPP_SINGLE_THREADED_APP
#ifdef NEKO_WINDOWS
// std::mutex on MSVC goes through the C++ runtime and tracks its owner, a
// quarter of the time a byte took
struct ReadersMutex
{
   SRWLOCK lock_;
   ReadersMutex() { InitializeSRWLock(&lock_); }
   bool try_lock() { return TryAcquireSRWLockExclusive(&lock_) != 0; }
   void lock() { AcquireSRWLockExclusive(&lock_); }
   void unlock() { ReleaseSRWLockExclusive(&lock_); }
};
#else
typedef std::mutex ReadersMutex;
#endif
#endif

struct ReadAhead
{
   #ifndef HXCPP_SINGLE_THREADED_APP
   ReadersMutex readers;
   #endif
   unsigned char *bytes;
   int pos;
   int end;
   // Read without the lock: bytes are held, for select and poll
   std::atomic<bool> held;
   // No more reading ahead, after the socket was handed to TLS
   std::atomic<bool> stopped;
   // What is held is thrown away, after close, or a shutdown for reading
   // that drops what the kernel holds too
   std::atomic<bool> discarded;
   // Held bytes thrown away still count as readable, as Windows reports bytes
   // it will no longer give after a shutdown for reading
   std::atomic<bool> stillReadable;

   ReadAhead() : bytes(0), pos(0), end(0), held(false), stopped(false), discarded(false), stillReadable(false) { }
   ~ReadAhead()
   {
      setHeld(false);
      free(bytes);
   }

   void setHeld(bool inHeld)
   {
      if (inHeld)
      {
         if (!held.exchange(true))
            sReadAheadHeld++;
      }
      else if (held.exchange(false))
         sReadAheadHeld--;
   }

   // Called by the reader that holds the lock
   int unread()
   {
      if (discarded)
      {
         pos = end = 0;
         if (!stillReadable)
            setHeld(false);
      }
      return end - pos;
   }

   int take(unsigned char *outBytes, int inLen)
   {
      int n = end - pos;
      if (n > inLen)
         n = inLen;
      memcpy(outBytes, bytes + pos, n);
      pos += n;
      if (pos == end)
         setHeld(false);
      return n;
   }

   // Without the lock: a reader clears what it holds when it next looks,
   // and held is clear either way, whichever comes first
   void discard(bool inStillReadable = false)
   {
      stopped = true;
      stillReadable = inStillReadable;
      discarded = true;
      if (!inStillReadable)
         setHeld(false);
   }
};

// The lock a read takes. Waiting for it is in a GC-free zone, so that a
// collection never waits on a read blocked in recv.
struct ReadAheadLock
{
   ReadAhead *ahead;
   ReadAheadLock(ReadAhead *inAhead) : ahead(inAhead)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (ahead && !ahead->readers.try_lock())
      {
         hx::EnterGCFreeZone();
         ahead->readers.lock();
         hx::ExitGCFreeZone();
      }
      #endif
   }
   ~ReadAheadLock()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (ahead)
         ahead->readers.unlock();
      #endif
   }
};

static void forgetHanded(ReadAhead *inAhead);

// The vtable of the wrappers made here
static std::atomic<void *> sWrapperVtable(0);

// In an unnamed namespace: libraries that make socket handles of their own,
// as CrossByte's accept does, declare a SocketWrapper too, and two classes of
// one name would share a vtable
namespace {
struct SocketWrapper : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSocket };

   SOCKET socket;
   bool stream;
   // Tells a socket from one that has the descriptor it had, for a poll.
   //  Never 0, which stands for a library's handle, that has none
   unsigned int serial;
   // Made by the first read of a stream socket, and freed with the wrapper,
   // so that a read never finds it gone
   std::atomic<ReadAhead *> ahead;

   int __GetType() const HXCPP_OVERRIDE { return socketType; }

   ReadAhead *currentAhead()
   {
      return ahead.load(std::memory_order_acquire);
   }

   ReadAhead *readAhead()
   {
      ReadAhead *a = ahead.load(std::memory_order_acquire);
      if (!a && stream)
      {
         ReadAhead *made = new ReadAhead();
         if (ahead.compare_exchange_strong(a, made))
            a = made;
         else
            delete made;
      }
      return a;
   }

   bool holdsBytes()
   {
      ReadAhead *a = currentAhead();
      return a && a->held.load(std::memory_order_relaxed);
   }


   // A socket that is dropped without close still releases its descriptor
   static void finalize(Dynamic inObj)
   {
      SocketWrapper *wrap = (SocketWrapper *)inObj.mPtr;
      if (wrap->socket != INVALID_SOCKET)
      {
         closesocket(wrap->socket);
         wrap->socket = INVALID_SOCKET;
      }
      ReadAhead *ahead = wrap->ahead.exchange(0);
      if (ahead)
      {
         forgetHanded(ahead);
         delete ahead;
      }
   }

   static SocketWrapper *create(SOCKET inSocket, bool inStream)
   {
      SocketWrapper *wrap = new SocketWrapper();
      wrap->socket = inSocket;
      wrap->stream = inStream;
      do
         wrap->serial = ++sNextSocketSerial;
      while (wrap->serial == 0);
      wrap->ahead = 0;
      sWrapperVtable.store(*(void **)wrap, std::memory_order_relaxed);
      _hx_set_finalizer(wrap, finalize);
      return wrap;
   }
};
}


// The object of a handle of the socket kind, or null for a sys.net.Socket
//  without one: a wrapper made here, or a library's in stock hxcpp's layout
static hx::Object *val_handle(Dynamic inValue)
{
   if (inValue.mPtr)
   {
      int type = inValue->__GetType();
      if (type==vtClass)
      {
         inValue = inValue->__Field( HX_CSTRING("__s"), hx::paccNever );
         if (inValue.mPtr==0)
            return 0;
         type = inValue->__GetType();
      }

      if (type==socketType)
         return inValue.mPtr;
   }

   hx::Throw(HX_CSTRING("Invalid socket handle"));
   return 0;
}

// Where both layouts keep the descriptor: right after the object header. A
//  library's handle is not a SocketWrapper, so it is not reached as one
static SOCKET &handleSocket(hx::Object *inHandle)
{
   const size_t offset = (sizeof(hx::Object) + alignof(SOCKET) - 1) / alignof(SOCKET) * alignof(SOCKET);
   return *reinterpret_cast<SOCKET *>(reinterpret_cast<char *>(inHandle) + offset);
}

static void checkWrapperLayout(SocketWrapper *inWrap)
{
   if (&handleSocket(inWrap)!=&inWrap->socket)
      hx::Throw(HX_CSTRING("SocketWrapper keeps its descriptor where handleSocket does not look"));
}

// The wrapper, for a handle made here, with its read-ahead and poll serial
static SocketWrapper *ownWrapper(hx::Object *inHandle)
{
   if (!inHandle || *reinterpret_cast<void **>(inHandle)!=sWrapperVtable.load(std::memory_order_relaxed))
      return 0;
   return static_cast<SocketWrapper *>(inHandle);
}


SOCKET val_sock(Dynamic inValue)
{
   hx::Object *handle = val_handle(inValue);
   // Not 0 - on posix that is stdin and operations would silently
   // target the console instead of failing
   return handle ? handleSocket(handle) : INVALID_SOCKET;
}


void reset_sock(Dynamic inValue)
{
   if (inValue.mPtr)
   {
      int type = inValue->__GetType();
      if (type==vtClass)
      {
         inValue = inValue->__Field( HX_CSTRING("__s"), hx::paccNever );
         if (inValue.mPtr==0)
            return;
         type = inValue->__GetType();
      }

      if (type==socketType) {
         handleSocket(inValue.mPtr) = INVALID_SOCKET;
         return;
      }
   }

   hx::Throw(HX_CSTRING("Invalid socket handle"));
}


/**
   <doc>
   <h1>Socket</h1>
   <p>
   TCP and UDP sockets
   </p>
   </doc>
**/

static void block_error()
{

#ifdef NEKO_WINDOWS
   int err = WSAGetLastError();
   // call ExitGCFreeZone after WSAGetLastError, WSAGetLastError is just an alias for GetLastError and
   // calling ExitGCFreeZone will on some cases clear the lastError code.
   hx::ExitGCFreeZone();
   if( err == WSAEWOULDBLOCK || err == WSAEALREADY )

#else
   hx::ExitGCFreeZone();
   if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY )
#endif
      hx::Throw(HX_CSTRING("Blocking"));
    else {
       hx::Throw(HX_CSTRING("EOF"));
    }
}

}

namespace
{
// Sockets handed to TLS holding bytes read ahead, until TLS has read them:
// it reads from the descriptor
struct HandedSocket
{
   SOCKET socket;
   ReadAhead *ahead;
};
static std::mutex sHandedLock;
static std::vector<HandedSocket> sHanded;
static std::atomic<int> sHandedCount(0);

static void forgetHanded(ReadAhead *inAhead)
{
   if (!sHandedCount.load())
      return;
   std::lock_guard<std::mutex> lock(sHandedLock);
   for(size_t i=0;i<sHanded.size();i++)
      if (sHanded[i].ahead==inAhead)
      {
         sHanded.erase(sHanded.begin()+i);
         sHandedCount--;
         return;
      }
}
}

// For SSL.cpp: the native socket in a handle, or a throw if it is not one.
// TLS reads from the descriptor, so the socket reads ahead no more, and the
// bytes it holds, which TLS would have read from the kernel, go to TLS first.
size_t _hx_std_socket_handle(Dynamic inSocket)
{
   hx::Object *handle = val_handle(inSocket);
   if (!handle)
      return (size_t)INVALID_SOCKET;
   SocketWrapper *wrap = ownWrapper(handle);
   ReadAhead *ahead = wrap ? wrap->readAhead() : 0;
   if (ahead)
   {
      ahead->stopped = true;
      ReadAheadLock lock(ahead);
      if (ahead->unread() && wrap->socket!=INVALID_SOCKET)
      {
         std::lock_guard<std::mutex> handed(sHandedLock);
         bool listed = false;
         for(size_t i=0;i<sHanded.size();i++)
            listed = listed || sHanded[i].ahead==ahead;
         if (!listed)
         {
            HandedSocket h = { wrap->socket, ahead };
            sHanded.push_back(h);
            sHandedCount++;
         }
      }
   }
   return (size_t)handleSocket(handle);
}

// For SSL.cpp: copies to outBytes what a socket handed to TLS still holds of
// the bytes read ahead, and returns how many, or 0 when it holds none
int _hx_std_socket_take_handed(size_t inSocket, unsigned char *outBytes, int inLen)
{
   if (!sHandedCount.load() || inLen<=0)
      return 0;
   ReadAhead *ahead = 0;
   {
      std::lock_guard<std::mutex> lock(sHandedLock);
      for(size_t i=0;i<sHanded.size();i++)
         if (sHanded[i].socket==(SOCKET)inSocket)
            ahead = sHanded[i].ahead;
   }
   if (!ahead)
      return 0;
   int got = 0;
   {
      ReadAheadLock lock(ahead);
      if (ahead->unread())
         got = ahead->take(outBytes, inLen);
      if (ahead->unread())
         return got;
   }
   forgetHanded(ahead);
   return got;
}

/**
   socket_init : void -> void
   <doc>
   Initialize the socket API. Must be called at least once per process
   before using any socket or host function.
   </doc>
**/
void _hx_std_socket_init()
{
#ifdef NEKO_WINDOWS
   if( !init_done ) {
      WSAStartup(MAKEWORD(2,0),&init_data);
      init_done = true;
   }
#endif
}


/**
   socket_new : udp:bool -> 'socket
   <doc>Create a new socket, TCP or UDP</doc>
**/
Dynamic _hx_std_socket_new( bool udp, bool ipv6 )
{
   if (!socketType)
      socketType = hxcpp_alloc_kind();

   SOCKET s;
   int family = ipv6 ? AF_INET6 : AF_INET;
   int type = udp ? SOCK_DGRAM : SOCK_STREAM;

   #ifdef NEKO_WINDOWS
      // Not inheritable, as posix sockets are close-on-exec below.  A
      // Winsock handle is inheritable by default, so every process started
      // while it was open (sys.io.Process, Sys.command) got a copy, and a
      // connection this process closed stayed open until those exited.
      // WSASocket with the overlapped flag is what socket() creates
      s = WSASocketW(family,type,0,NULL,0,WSA_FLAG_OVERLAPPED|WSA_FLAG_NO_HANDLE_INHERIT);
      if( s == INVALID_SOCKET && WSAGetLastError() == WSAEINVAL )
      {
         // Before Windows 7 SP1 the flag is refused - clear it afterwards
         s = socket(family,type,0);
         if( s != INVALID_SOCKET )
            SetHandleInformation((HANDLE)s,HANDLE_FLAG_INHERIT,0);
      }
   #elif defined(HX_LINUX) && defined(SOCK_CLOEXEC)
      // Close-on-exec from the start - set afterwards, a process another
      // thread started in between inherited the socket anyway
      s = socket(family,type|SOCK_CLOEXEC,0);
   #else
      s = socket(family,type,0);
   #endif

   if( s == INVALID_SOCKET )
      return null();

   #ifdef NEKO_MAC
      int set = 1;
      setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,(void *)&set, sizeof(int));
   #endif

   #if defined(NEKO_POSIX) && !(defined(HX_LINUX) && defined(SOCK_CLOEXEC))
      // we don't want sockets to be inherited in case of exec
      int old = fcntl(s,F_GETFD,0);
      if( old >= 0 ) fcntl(s,F_SETFD,old|FD_CLOEXEC);
   #endif

   SocketWrapper *wrap = SocketWrapper::create(s,!udp);
   checkWrapperLayout(wrap);
   return wrap;
}

/**
   socket_close : 'socket -> void
   <doc>Close a socket. Any subsequent operation on this socket will fail</doc>
**/
void _hx_std_socket_close( Dynamic handle )
{
   hx::Object *object = val_handle(handle);
   SocketWrapper *wrap = ownWrapper(object);
   SOCKET s = object ? handleSocket(object) : INVALID_SOCKET;
   if( s != INVALID_SOCKET )
   {
      // No EINTR retry: posix close() releases the descriptor even when
      // interrupted, so retrying can close a descriptor another thread
      // just received.  A lingering close (SO_LINGER) can block, so let
      // the GC keep running.
      hx::EnterGCFreeZone();
      closesocket(s);
      hx::ExitGCFreeZone();
      sSocketsClosed++;
   }
   reset_sock(handle);
   ReadAhead *ahead = wrap ? wrap->currentAhead() : 0;
   if (ahead)
   {
      ahead->discard();
      forgetHanded(ahead);
   }
}

/**
   socket_send_char : 'socket -> int -> void
   <doc>Send a character over a connected socket. Must be in the range 0..255</doc>
**/
void _hx_std_socket_send_char( Dynamic o, int c )
{
   SOCKET sock = val_sock(o);
   if( c < 0 || c > 255 )
      return;

   unsigned char cc = (unsigned char)c;

   hx::EnterGCFreeZone();
   POSIX_LABEL(send_char_again);
   if( send(sock,(const char *)&cc,1,MSG_NOSIGNAL) == SOCKET_ERROR )
   {
      HANDLE_EINTR(send_char_again);
      block_error();
   }
   hx::ExitGCFreeZone();
}

/**
   socket_send : 'socket -> buf:string -> pos:int -> len:int -> int
   <doc>Send up to [len] bytes from [buf] starting at [pos] over a connected socket.
   Return the number of bytes sent.</doc>
**/
int _hx_std_socket_send( Dynamic o, Array<unsigned char> buf, int p, int l )
{
   SOCKET sock = val_sock(o);
   int dlen = buf->length;
   // l > dlen - p, not p + l > dlen: the addition can overflow and skip the check
   if( p < 0 || l < 0 || p > dlen || l > dlen - p )
      return 0;

   // volatile: the allocation start must stay visible in this frame so the
   // conservative scan pins buf while send() reads it from inside the zone
   const char * volatile base = (const char *)buf->GetBase();
   hx::EnterGCFreeZone();
   POSIX_LABEL(send_again);
   dlen = send(sock, base + p , l, MSG_NOSIGNAL);
   if( dlen == SOCKET_ERROR )
   {
      HANDLE_EINTR(send_again);
      block_error();
   }
   hx::ExitGCFreeZone();
   return dlen;
}

/**
   socket_recv : 'socket -> buf:string -> pos:int -> len:int -> int
   <doc>Read up to [len] bytes from [buf] starting at [pos] from a connected socket.
   Return the number of bytes readed.</doc>
**/
int _hx_std_socket_recv( Dynamic o, Array<unsigned char> buf, int p, int l )
{
   hx::Object *handle = val_handle(o);
   SocketWrapper *wrap = ownWrapper(handle);
   SOCKET sock = handle ? handleSocket(handle) : INVALID_SOCKET;
   int dlen = buf->length;
   if( p < 0 || l < 0 || p > dlen || l > dlen - p )
      return 0;

   // An empty read returns at once when the kernel has bytes, and Windows
   // waits for some otherwise, so one that holds bytes returns at once
   if (l == 0 && wrap && sock != INVALID_SOCKET && wrap->holdsBytes())
      return 0;
   ReadAhead *ahead = wrap && l > 0 ? wrap->readAhead() : 0;
   ReadAheadLock lock(ahead);
   if (ahead && ahead->unread() && sock != INVALID_SOCKET)
      return ahead->take((unsigned char *)buf->GetBase() + p, l);

   char * volatile base = (char *)buf->GetBase();
   hx::EnterGCFreeZone();
   POSIX_LABEL(recv_again);
   dlen = recv(sock, base + p, l, MSG_NOSIGNAL);
   if( dlen == SOCKET_ERROR )
   {
      #ifdef NEKO_WINDOWS
      // A datagram larger than the buffer fills the buffer but reports
      // WSAEMSGSIZE - posix silently truncates instead.  Match posix
      // rather than discarding the data as a bogus "EOF"
      if( WSAGetLastError() == WSAEMSGSIZE )
      {
         hx::ExitGCFreeZone();
         return l;
      }
      #endif
      HANDLE_EINTR(recv_again);
      block_error();
   }
   hx::ExitGCFreeZone();
   return dlen;
}

/**
   socket_recv_char : 'socket -> int
   <doc>Read a single char from a connected socket.</doc>
**/
int _hx_std_socket_recv_char( Dynamic o )
{
   hx::Object *handle = val_handle(o);
   SocketWrapper *wrap = ownWrapper(handle);
   SOCKET sock = handle ? handleSocket(handle) : INVALID_SOCKET;

   ReadAhead *ahead = wrap ? wrap->readAhead() : 0;
   ReadAheadLock lock(ahead);
   if (ahead && sock != INVALID_SOCKET)
   {
      if (ahead->unread())
      {
         int cc = ahead->bytes[ahead->pos++];
         if (ahead->pos == ahead->end)
            ahead->setHeld(false);
         return cc;
      }
      if (!ahead->stopped.load() && !ahead->bytes)
         ahead->bytes = (unsigned char *)malloc(SOCKET_READ_AHEAD);
      if (!ahead->stopped.load() && ahead->bytes)
      {
         unsigned char *bytes = ahead->bytes;
         hx::EnterGCFreeZone();
         POSIX_LABEL(recv_ahead_again);
         int ret = recv(sock,(char *)bytes,SOCKET_READ_AHEAD,MSG_NOSIGNAL);
         if( ret == SOCKET_ERROR )
         {
            HANDLE_EINTR(recv_ahead_again);
            block_error();
         }
         hx::ExitGCFreeZone();
         if( ret == 0 )
            hx::Throw(HX_CSTRING("Connection closed"));
         if (ret > 1)
         {
            ahead->pos = 1;
            ahead->end = ret;
            ahead->setHeld(true);
            // A close or shutdown while recv waited throws away what it got
            ahead->unread();
         }
         return bytes[0];
      }
   }

   hx::EnterGCFreeZone();
   POSIX_LABEL(recv_char_again);
   unsigned char cc = 0;
   int ret = recv(sock,(char *)&cc,1,MSG_NOSIGNAL);
   if( ret == SOCKET_ERROR )
   {
      HANDLE_EINTR(recv_char_again);
      block_error();
   }
   hx::ExitGCFreeZone();
   if( ret == 0 )
      hx::Throw(HX_CSTRING("Connection closed"));
   return cc;
}


/**
   socket_write : 'socket -> string -> void
   <doc>Send the whole content of a string over a connected socket.</doc>
**/
void _hx_std_socket_write( Dynamic o, Array<unsigned char> buf )
{
   SOCKET sock = val_sock(o);
   int datalen = buf->length;
   const char * volatile cdata = (const char *)buf->GetBase();
   int pos = 0;

   hx::EnterGCFreeZone();
   while( datalen > 0 )
   {
      POSIX_LABEL(write_again);
      int slen = send(sock,cdata + pos,datalen,MSG_NOSIGNAL);
      if( slen == SOCKET_ERROR ) {
         HANDLE_EINTR(write_again);
         block_error();
      }
      pos += slen;
      datalen -= slen;
   }
   hx::ExitGCFreeZone();
}


/**
   socket_read : 'socket -> string
   <doc>Read the whole content of a the data available from a socket until the connection close.
   If the socket hasn't been close by the other side, the function might block.
   </doc>
**/
Array<unsigned char> _hx_std_socket_read( Dynamic o )
{
   hx::Object *handle = val_handle(o);
   SocketWrapper *wrap = ownWrapper(handle);
   SOCKET sock = handle ? handleSocket(handle) : INVALID_SOCKET;
   Array<unsigned char> result = Array_obj<unsigned char>::__new();
   ReadAhead *ahead = wrap ? wrap->readAhead() : 0;
   ReadAheadLock lock(ahead);
   if (ahead && sock != INVALID_SOCKET && ahead->unread())
   {
      int held = ahead->unread();
      result->EnsureAppendSize(held);
      ahead->take((unsigned char *)result->GetBase(), held);
   }
   // Read in large chunks: a 256-byte buffer forced one recv() syscall (and
   // array append) per 256 bytes when slurping a stream to EOF. 16KB cuts that
   // ~64x while staying small enough for any thread stack and exception-safe
   // (no heap buffer to leak if block_error throws).
   char buf[16384];

   hx::EnterGCFreeZone();
   while( true )
   {
      POSIX_LABEL(read_again);
      int len = recv(sock,buf,sizeof(buf),MSG_NOSIGNAL);
      if( len == SOCKET_ERROR ) {
         HANDLE_EINTR(read_again);
         block_error();
      }
      if( len == 0 )
         break;

      hx::ExitGCFreeZone();
      int olen = result->length;
      result->EnsureAppendSize(olen+len);
      ::memcpy(result->GetBase()+olen, buf, len);
      hx::EnterGCFreeZone();
   }

   hx::ExitGCFreeZone();
   return result;
}

#if !(defined(NEKO_WINDOWS) || defined(NEKO_MAC) || defined(BLACKBERRY) || defined(__EMSCRIPTEN__) || defined(NEKO_BSD))
// Scratch space for gethostbyname_r and gethostbyaddr_r, which give ERANGE
// when the answer does not fit. A fixed 1 KiB took that for a missing host,
// so a host with many aliases or addresses was unknown. The lookup is tried
// again with twice the space, up to 1 MiB. Called in a GC-free zone.
struct ResolverScratch
{
   union { char local[1024]; void *align; };
   char *data;
   size_t size;

   ResolverScratch() : data(local), size(sizeof(local)) { }
   ~ResolverScratch() { if (data!=local) free(data); }

   bool grow()
   {
      if (size >= 1024*1024)
         return false;
      char *bigger = (char *)malloc(size*2);
      if (!bigger)
         return false;
      if (data!=local)
         free(data);
      data = bigger;
      size *= 2;
      return true;
   }
};
#endif

/**
   host_resolve : string -> 'int32
   <doc>Resolve the given host string into an IP address.</doc>
**/
int _hx_std_host_resolve( String host )
{
   // inet_addr's error value INADDR_NONE is also the valid limited
   // broadcast address, which would otherwise fall into DNS and throw
   if( host == HX_CSTRING("255.255.255.255") )
      return (int)0xFFFFFFFF;

   unsigned int ip;

   hx::EnterGCFreeZone();
   hx::strbuf buf;
   ip = inet_addr(host.utf8_str(&buf));
   if( ip == INADDR_NONE )
   {
      struct hostent *h = 0;
      hx::strbuf hostBuf;

#   if defined(NEKO_WINDOWS) || defined(NEKO_MAC) || defined(BLACKBERRY) || defined(__EMSCRIPTEN__) || defined(NEKO_BSD)
      h = gethostbyname(host.utf8_str(&hostBuf));
#   else
      struct hostent hbase;
      ResolverScratch scratch;
      int errcode;
      const char *name = host.utf8_str(&hostBuf);
      while( gethostbyname_r(name,&hbase,scratch.data,scratch.size,&h,&errcode)==ERANGE && scratch.grow() )
         h = 0;
#   endif
      if( !h ) {
         hx::ExitGCFreeZone();
         return hx::Throw( HX_CSTRING("Unknown host:") + host );
      }
      ip = *((unsigned int*)h->h_addr);
   }
   hx::ExitGCFreeZone();
   return ip;
}

#ifdef DYNAMIC_INET_FUNCS
static DynamicInetFunc dynamic_inet_pton = { "inet_pton" };
#endif

Array<unsigned char> _hx_std_host_resolve_ipv6( String host, bool )
{
   in6_addr ipv6;

   hx::strbuf hostBuf;
   const char *hostStr = host.utf8_str(&hostBuf);
   #ifdef DYNAMIC_INET_FUNCS
   inet_pton_func pton = (inet_pton_func)dynamic_inet_pton.get();
   int ok = pton ? pton(AF_INET6, hostStr, (void *)&ipv6) : 0;
   #else
   int ok = inet_pton(AF_INET6, hostStr, (void *)&ipv6);
   #endif

   if (!ok)
   {
      addrinfo hints;

      memset(&hints, 0, sizeof(struct addrinfo));
      hints.ai_family = AF_INET6;  //  IPv6
      hints.ai_socktype = 0;  // any - SOCK_STREAM or SOCK_DGRAM
      hints.ai_flags = AI_PASSIVE;  // For wildcard IP address
      hints.ai_protocol = 0;        // Any protocol
      hints.ai_canonname = 0;
      hints.ai_addr = 0;
      hints.ai_next = 0;

      addrinfo *result = 0;
      hx::EnterGCFreeZone();
      int err =  getaddrinfo( hostStr, 0, &hints, &result);
      hx::ExitGCFreeZone();
      if (err==0)
      {
         for(addrinfo * rp = result; rp; rp = rp->ai_next)
         {
            if (rp->ai_family==AF_INET6)
            {
               sockaddr_in6 *s6 = (sockaddr_in6 *)rp->ai_addr;
               ipv6 = s6->sin6_addr;
               ok = true;
               break;
            }
            else
            {
               freeaddrinfo(result);
               hx::Throw( HX_CSTRING("Unkown ai_family") );
            }
         }
         freeaddrinfo(result);
      }
      else
      {
         hx::Throw( host + HX_CSTRING(":") + String(gai_strerror(err)) );
      }
   }

   if (!ok)
      return null();

   return Array_obj<unsigned char>::fromData( (unsigned char *)&ipv6, 16 );
}



/**
   host_to_string : 'int32 -> string
   <doc>Return a string representation of the IP address.</doc>
**/
String _hx_std_host_to_string( int ip )
{
   struct in_addr i;
   *(int*)&i = ip;
   #ifdef NEKO_WINDOWS
   // Winsock keeps inet_ntoa's answer per thread
   return String( inet_ntoa(i) );
   #else
   // Not inet_ntoa: on macOS and the BSDs its buffer is one static for the
   // whole process, which it first fills with "[inet_ntoa error]" and then
   // formats over, so a thread formatting at the same time as another read
   // that text back for 127.0.0.1
   char buf[INET_ADDRSTRLEN];
   if (!inet_ntop(AF_INET, &i, buf, sizeof(buf)))
      return String();
   return String( buf );
   #endif
}


#ifdef DYNAMIC_INET_FUNCS
static DynamicInetFunc dynamic_inet_ntop = { "inet_ntop" };
#endif


String _hx_std_host_to_string_ipv6( Array<unsigned char> ip )
{
   char buf[100];
   #ifdef DYNAMIC_INET_FUNCS
   inet_ntop_func ntop = (inet_ntop_func)dynamic_inet_ntop.get();
   if (!ntop)
      return String();
   return String( ntop(AF_INET6, &ip[0], buf, 100) );
   #else
   return String( inet_ntop(AF_INET6, &ip[0], buf, 100) );
   #endif
}

/**
   host_reverse : 'int32 -> string
   <doc>Reverse the DNS of the given IP address.</doc>
**/
String _hx_std_host_reverse( int host )
{
   struct hostent *h = 0;
   unsigned int ip = host;
   hx::EnterGCFreeZone();
   #if defined(NEKO_WINDOWS) || defined(NEKO_MAC) || defined(ANDROID) || defined(BLACKBERRY) || defined(__EMSCRIPTEN__) || defined(NEKO_BSD)
   h = gethostbyaddr((char *)&ip,4,AF_INET);
   #else
   struct hostent htmp;
   int errcode;
   ResolverScratch scratch;
   while( gethostbyaddr_r((char *)&ip,4,AF_INET,&htmp,scratch.data,scratch.size,&h,&errcode)==ERANGE && scratch.grow() )
      h = 0;
   #endif
   hx::ExitGCFreeZone();
   if( !h )
      return String();
   return String( h->h_name );
}

String _hx_std_host_reverse_ipv6( Array<unsigned char> host )
{
   if (!host.mPtr || host->length!=16)
      return String();

   // Out of the array first: it may move once the zone is entered
   unsigned char addr[16];
   memcpy(addr, host->GetBase(), 16);
   struct hostent *h = 0;
   hx::EnterGCFreeZone();
   #if defined(NEKO_WINDOWS) || defined(NEKO_MAC) || defined(ANDROID) || defined(BLACKBERRY) || defined(__EMSCRIPTEN__) || defined(NEKO_BSD)
   h = gethostbyaddr((char *)addr,16,AF_INET6);
   #else
   struct hostent htmp;
   int errcode;
   ResolverScratch scratch;
   while( gethostbyaddr_r((char *)addr,16,AF_INET6,&htmp,scratch.data,scratch.size,&h,&errcode)==ERANGE && scratch.grow() )
      h = 0;
   #endif
   hx::ExitGCFreeZone();
   if( !h )
      return String();
   return String( h->h_name );
}


/**
   host_local : void -> string
   <doc>Return the local host name.</doc>
**/
String _hx_std_host_local()
{
   char buf[256];
   hx::EnterGCFreeZone();
   if( gethostname(buf,256) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      return String();
   }
   hx::ExitGCFreeZone();
   return String(buf);
}

/**
   socket_connect : 'socket -> host:'int32 -> port:int -> void
   <doc>Connect the socket the given [host] and [port]</doc>
**/
void _hx_std_socket_connect( Dynamic o, int host, int port )
{
   struct sockaddr_in addr;
   memset(&addr,0,sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   *(int*)&addr.sin_addr.s_addr = host;

   // val_sock does a dynamic field lookup and can throw - neither is
   // allowed inside the free zone
   SOCKET sock = val_sock(o);
   hx::EnterGCFreeZone();
   if( connect(sock,(struct sockaddr*)&addr,sizeof(addr)) == SOCKET_ERROR )
   {
      #ifdef NEKO_POSIX
      // A signal interrupted a blocking connect: the connection continues
      // asynchronously (posix), so report "in progress", not EOF
      if( errno == EINTR )
      {
         hx::ExitGCFreeZone();
         hx::Throw(HX_CSTRING("Blocking"));
      }
      #endif
      // This will throw a "Blocking" exception if the "error" was because
      // it's a non-blocking socket with connection in progress, otherwise
      // it will do nothing.
      //
      // - now it always throws
      block_error();
   }
   hx::ExitGCFreeZone();
}


/**
   socket_connect - to ipv6 host
**/
void _hx_std_socket_connect_ipv6( Dynamic o, Array<unsigned char> host, int port )
{
   struct sockaddr_in6 addr;
   memset(&addr,0,sizeof(addr));
   addr.sin6_family = AF_INET6;
   addr.sin6_port = htons(port);
   memcpy(&addr.sin6_addr,&host[0],16);

   SOCKET sock = val_sock(o);
   hx::EnterGCFreeZone();
   if( connect(sock,(struct sockaddr*)&addr,sizeof(addr)) != 0 )
   {
      #ifdef NEKO_POSIX
      if( errno == EINTR )
      {
         hx::ExitGCFreeZone();
         hx::Throw(HX_CSTRING("Blocking"));
      }
      #endif
      // This will throw a "Blocking" exception if the "error" was because
      // it's a non-blocking socket with connection in progress, otherwise
      // it will do nothing.
      //
      // - now it always throws
      block_error();
   }
   hx::ExitGCFreeZone();
}

/**
   socket_listen : 'socket -> int -> void
   <doc>Listen for a number of connections</doc>
**/
void _hx_std_socket_listen( Dynamic o, int n )
{
   SOCKET sock = val_sock(o);
   hx::EnterGCFreeZone();
   if( listen(sock,n) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("Listen failed"));
   }
   hx::ExitGCFreeZone();
}

static fd_set *make_socket_array( Array<Dynamic> a, fd_set *tmp, SOCKET *n )
{
   FD_ZERO(tmp);
   if( !a.mPtr )
      return tmp;

   int len = a->length;
   if( len > FD_SETSIZE )
      hx::Throw(HX_CSTRING("Too many sockets in select"));

   for(int i=0;i<len;i++)
   {
      // make sure it is a socket...
      SOCKET sock = val_sock( a[i] );
      if( sock == INVALID_SOCKET )
         hx::Throw(HX_CSTRING("Closed socket in select"));
      #ifndef NEKO_WINDOWS
      // On posix fd_set is a fixed-size bitmap and FD_SET past it smashes
      // the stack.  (On Windows it is a counted array, where the length
      // check above is the right one.)
      if( sock >= FD_SETSIZE )
         hx::Throw(HX_CSTRING("Socket descriptor too large for select (use poll)"));
      #endif
      if( sock > *n )
         *n = sock;
      FD_SET(sock,tmp);
   }
   return tmp;
}

// Whether a socket to be read holds bytes read ahead, which select would not
// see: those are ready, and select only adds whatever else is ready now
static bool holds_read_ahead( Array<Dynamic> a )
{
   if( !a.mPtr || !sReadAheadHeld.load() )
      return false;
   for(int i=0;i<a->length;i++)
   {
      SocketWrapper *wrap = ownWrapper(val_handle(a[i]));
      if( wrap && wrap->socket != INVALID_SOCKET && wrap->holdsBytes() )
         return true;
   }
   return false;
}

static void add_read_ahead( Array<Dynamic> a, fd_set *tmp )
{
   for(int i=0;i<a->length;i++)
   {
      SocketWrapper *wrap = ownWrapper(val_handle(a[i]));
      if( wrap && wrap->socket != INVALID_SOCKET && wrap->holdsBytes() && !FD_ISSET(wrap->socket,tmp) )
         FD_SET(wrap->socket,tmp);
   }
}

static Array<Dynamic> make_array_result( Array<Dynamic> a, fd_set *tmp )
{
   if (!tmp || !a.mPtr)
      return null();

   int len = a->length;
   Array<Dynamic> r = Array_obj<Dynamic>::__new();
   for(int i=0;i<len;i++)
   {
      Dynamic s = a[i];
      if( FD_ISSET(val_sock(s),tmp) )
         r->push(s);
   }
   return r;
}

static void make_array_result_inplace(Array<Dynamic> a, fd_set *tmp)
{
    if (!a.mPtr)
      return;

    if (tmp == NULL)
    {
       a->__SetSize(0);
       return;
    }

    int len = a->length;
    int destPos = 0;
    for(int i = 0; i < len; i++)
    {
       Dynamic s = a[i];
       if (FD_ISSET(val_sock(s), tmp)) {
           a[destPos++] = s;
       }
    }

    a->__SetSize(destPos);
}

static struct timeval *init_timeval( double f, struct timeval *t ) {
   if (f<0)
      return 0;
   t->tv_usec = (int)(  (f - (int)f ) * 1000000 );
   t->tv_sec = (int)f;
   return t;
}

// Not only NEKO_POSIX: poll_events uses it on every non-Windows system,
// Emscripten included
#ifndef NEKO_WINDOWS
static double monotonicSeconds()
{
   return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

#ifdef NEKO_POSIX

// A finite wait that a signal interrupted goes on for what is left of it,
// not for its whole length again
static void remainingTimeval(double inDeadline, struct timeval *t)
{
   double left = inDeadline - monotonicSeconds();
   if (left < 0)
      left = 0;
   t->tv_sec = (int)left;
   t->tv_usec = (int)((left - (int)left) * 1000000);
}
#endif

#ifdef __EMSCRIPTEN__
// Emscripten's select reads and writes only the first 64 descriptors of each
// set, so a socket above that kept the bit it was given and came back as
// ready. Its poll takes any descriptor, so select runs through that instead,
// with the same readiness: POLLIN for reading, POLLOUT for writing and
// POLLPRI for the others. A closed descriptor fails with EBADF, as in its
// select.
static int selectSockets(int n, fd_set *ra, fd_set *wa, fd_set *ea, struct timeval *tt)
{
   std::vector<struct pollfd> fds;
   for(int fd=0;fd<n;fd++)
   {
      short events = (FD_ISSET(fd,ra) ? POLLIN : 0) | (FD_ISSET(fd,wa) ? POLLOUT : 0) | (FD_ISSET(fd,ea) ? POLLPRI : 0);
      if (events)
      {
         struct pollfd p;
         p.fd = fd;
         p.events = events;
         p.revents = 0;
         fds.push_back(p);
      }
   }

   int ms = -1;
   if (tt)
   {
      double wait = tt->tv_sec * 1000.0 + tt->tv_usec / 1000.0;
      ms = wait > 0x7fffffff ? 0x7fffffff : (int)wait;
   }
   if (poll(fds.data(), fds.size(), ms) < 0)
      return SOCKET_ERROR;

   FD_ZERO(ra);
   FD_ZERO(wa);
   FD_ZERO(ea);
   int ready = 0;
   for(size_t i=0;i<fds.size();i++)
   {
      const struct pollfd &p = fds[i];
      if (p.revents & POLLNVAL)
      {
         errno = EBADF;
         return SOCKET_ERROR;
      }
      // poll reports POLLERR and POLLHUP unasked, but select did not use them
      if ((p.events & POLLIN) && (p.revents & POLLIN))
      {
         FD_SET(p.fd,ra);
         ready++;
      }
      if ((p.events & POLLOUT) && (p.revents & POLLOUT))
      {
         FD_SET(p.fd,wa);
         ready++;
      }
      if ((p.events & POLLPRI) && (p.revents & POLLPRI))
      {
         FD_SET(p.fd,ea);
         ready++;
      }
   }
   return ready;
}
#else
static int selectSockets(int n, fd_set *ra, fd_set *wa, fd_set *ea, struct timeval *tt)
{
   return select(n,ra,wa,ea,tt);
}
#endif

/**
   socket_select : read : 'socket array -> write : 'socket array -> others : 'socket array -> timeout:number? -> 'socket array array
   <doc>Perform the [select] operation. Timeout is in seconds or [null] if infinite</doc>
**/
Array<Dynamic> _hx_std_socket_select( Array<Dynamic> rs, Array<Dynamic> ws, Array<Dynamic> es, Dynamic timeout )
{
   SOCKET n = 0;
   fd_set rx, wx, ex;
   fd_set *ra, *wa, *ea;
   double deadline = -1;

   POSIX_LABEL(select_again);
   ra = make_socket_array(rs,&rx,&n);
   wa = make_socket_array(ws,&wx,&n);
   ea = make_socket_array(es,&ex,&n);

   struct timeval tval;
   struct timeval *tt = 0;
   if( timeout.mPtr )
      tt = init_timeval(timeout,&tval);
   #ifdef NEKO_POSIX
   if (tt)
   {
      if (deadline < 0)
         deadline = monotonicSeconds() + tval.tv_sec + tval.tv_usec * 1e-6;
      else
         remainingTimeval(deadline, &tval);
   }
   #endif
   bool held = holds_read_ahead(rs);
   if (held)
   {
      tval.tv_sec = 0;
      tval.tv_usec = 0;
      tt = &tval;
   }

   hx::EnterGCFreeZone();
   if( selectSockets((int)(n+1),ra,wa,ea,tt) == SOCKET_ERROR )
   {
      // Sample the error before leaving the zone - the zone exit can
      // clobber errno/GetLastError
      #ifdef NEKO_WINDOWS
      int err = WSAGetLastError();
      hx::ExitGCFreeZone();
      #else
      int err = errno;
      hx::ExitGCFreeZone();
      // select_again only exists where POSIX_LABEL defines it (NEKO_POSIX);
      // elsewhere, emscripten among them, there is no retry to jump to.
      #ifdef NEKO_POSIX
      if( err == EINTR )
         goto select_again;
      #endif
      #endif
      hx::Throw( HX_CSTRING("Select error ") + String(err) );
   }
   hx::ExitGCFreeZone();
   if (held)
      add_read_ahead(rs,ra);

   Array<Dynamic> r = Array_obj<Dynamic>::__new(3,3);
   r->__unsafe_set(0, make_array_result(rs,ra));
   r->__unsafe_set(1, make_array_result(ws,wa));
   r->__unsafe_set(2, make_array_result(es,ea));
   return r;
}

/**
   socket_select : read : 'socket array -> write : 'socket array -> others : 'socket array -> timeout:number?
   <doc>Perform the [select] operation. Timeout is in seconds or [null] if infinite</doc>
**/
void _hx_std_socket_fast_select( Array<Dynamic> rs, Array<Dynamic> ws, Array<Dynamic> es, Dynamic timeout )
{
   SOCKET n = 0;
   fd_set rx, wx, ex;
   fd_set *ra, *wa, *ea;
   double deadline = -1;

   POSIX_LABEL(select_again);
   ra = make_socket_array(rs,&rx,&n);
   wa = make_socket_array(ws,&wx,&n);
   ea = make_socket_array(es,&ex,&n);

   struct timeval tval;
   struct timeval *tt = 0;
   if( timeout.mPtr )
      tt = init_timeval(timeout,&tval);
   #ifdef NEKO_POSIX
   if (tt)
   {
      if (deadline < 0)
         deadline = monotonicSeconds() + tval.tv_sec + tval.tv_usec * 1e-6;
      else
         remainingTimeval(deadline, &tval);
   }
   #endif
   bool held = holds_read_ahead(rs);
   if (held)
   {
      tval.tv_sec = 0;
      tval.tv_usec = 0;
      tt = &tval;
   }

   hx::EnterGCFreeZone();
   if( selectSockets((int)(n+1),ra,wa,ea,tt) == SOCKET_ERROR )
   {
      // Sample the error before leaving the zone - the zone exit can
      // clobber errno/GetLastError
      #ifdef NEKO_WINDOWS
      int err = WSAGetLastError();
      hx::ExitGCFreeZone();
      #else
      int err = errno;
      hx::ExitGCFreeZone();
      // select_again only exists where POSIX_LABEL defines it (NEKO_POSIX);
      // elsewhere, emscripten among them, there is no retry to jump to.
      #ifdef NEKO_POSIX
      if( err == EINTR )
         goto select_again;
      #endif
      #endif
      hx::Throw( HX_CSTRING("Select error ") + String(err) );
   }

   hx::ExitGCFreeZone();
   if (held)
      add_read_ahead(rs,ra);
   make_array_result_inplace(rs, ra);
   make_array_result_inplace(ws, wa);
   make_array_result_inplace(es, ea);
}

#ifndef NEKO_WINDOWS
/**
   SO_REUSEADDR is for a listener taking back a port its predecessor left
   in TIME_WAIT, and only a stream socket has one. On a datagram socket
   Linux reads it otherwise: two sockets that both set it may share a port,
   so binding to port 0 can hand out one already in use -- 18 of 1,000
   sockets were given one, 306 of 4,000 -- and one of the two then receives
   the other's datagrams. Any local process that set it could bind a UDP
   server's port as well.
**/
static bool isStreamSocket(SOCKET sock)
{
   int type = SOCK_STREAM;
   SocketLen length = sizeof(type);
   if (getsockopt(sock,SOL_SOCKET,SO_TYPE,(char*)&type,&length) != 0)
      return true;
   return type == SOCK_STREAM;
}
#endif

/**
   socket_bind : 'socket -> host : 'int -> port:int -> void
   <doc>Bind the socket for server usage on the given host and port</doc>
**/
void _hx_std_socket_bind( Dynamic o, int host, int port )
{
   SOCKET sock = val_sock(o);

   int opt = 1;
   struct sockaddr_in addr;
   memset(&addr,0,sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   *(int*)&addr.sin_addr.s_addr = host;
   #ifndef NEKO_WINDOWS
   if (isStreamSocket(sock))
      setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
   #endif

   hx::EnterGCFreeZone();
   if( bind(sock,(struct sockaddr*)&addr,sizeof(addr)) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("Bind failed"));
   }
   hx::ExitGCFreeZone();
}


/**
   socket_bind - ipv6 version
   <doc>Bind the socket for server usage on the given host and port</doc>
**/
void _hx_std_socket_bind_ipv6( Dynamic o, Array<unsigned char> host, int port )
{
   SOCKET sock = val_sock(o);

   int opt = 1;

   struct sockaddr_in6 addr;
   memset(&addr,0,sizeof(addr));
   addr.sin6_family = AF_INET6;
   addr.sin6_port = htons(port);
   memcpy(&addr.sin6_addr,&host[0], 16);
   #ifndef NEKO_WINDOWS
   if (isStreamSocket(sock))
      setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
   #endif

   hx::EnterGCFreeZone();
   if( bind(sock,(struct sockaddr*)&addr,sizeof(addr)) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("Bind failed"));
   }
   hx::ExitGCFreeZone();
}


/**
   socket_accept : 'socket -> 'socket
   <doc>Accept an incoming connection request</doc>
**/

#ifdef _WIN32
   typedef int SockLen;
#elif defined(ANDROID)
   typedef socklen_t SockLen;
#else
   typedef unsigned int SockLen;
#endif

Dynamic _hx_std_socket_accept( Dynamic o )
{
   SOCKET sock = val_sock(o);
   // Room for any family's peer address: an IPv6 one does not fit a
   // sockaddr_in, and Windows fails the accept with WSAEFAULT
   struct sockaddr_storage addr;
   SockLen addrlen = sizeof(addr);
   SOCKET s;
   hx::EnterGCFreeZone();
   POSIX_LABEL(accept_again);
   #if defined(HX_LINUX) && defined(SOCK_CLOEXEC)
   // Close-on-exec from the start, as in socket_new
   s = accept4(sock,(struct sockaddr*)&addr,&addrlen,SOCK_CLOEXEC);
   #else
   s = accept(sock,(struct sockaddr*)&addr,&addrlen);
   #endif
   if( s == INVALID_SOCKET )
   {
      HANDLE_EINTR(accept_again);
      block_error();
   }

   // macOS copies SO_NOSIGPIPE from the listening socket, but a socket
   // passed in from elsewhere may not have it
   #ifdef NEKO_MAC
      int set = 1;
      setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,(void *)&set, sizeof(int));
   #endif
   #ifdef NEKO_WINDOWS
      // An accepted handle is not inheritable only when the listener was
      // created with WSA_FLAG_NO_HANDLE_INHERIT - not when its flag was
      // cleared afterwards, as socket_new's fallback does.  An inheritable
      // one reached every process started while the connection was open,
      // and the peer saw this process close it only when they all exited
      SetHandleInformation((HANDLE)s,HANDLE_FLAG_INHERIT,0);
   #elif defined(NEKO_POSIX) && !(defined(HX_LINUX) && defined(SOCK_CLOEXEC))
      int old = fcntl(s,F_GETFD,0);
      if( old >= 0 ) fcntl(s,F_SETFD,old|FD_CLOEXEC);
   #endif
   hx::ExitGCFreeZone();

   return SocketWrapper::create(s,true);
}

/**
   socket_peer : 'socket -> #address
   <doc>Return the socket connected peer address composed of an (host,port) array</doc>
**/
Array<int> _hx_std_socket_peer( Dynamic o )
{
   SOCKET sock = val_sock(o);
   struct sockaddr_in addr;
   SockLen addrlen = sizeof(addr);
   hx::EnterGCFreeZone();
   if( getpeername(sock,(struct sockaddr*)&addr,&addrlen) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      return null();
   }
   hx::ExitGCFreeZone();

   Array<int> ret = Array_obj<int>::__new(2,2);
   ret[0] = *(int*)&addr.sin_addr;
   ret[1] = ntohs(addr.sin_port);
   return ret;
}

/**
   socket_host : 'socket -> #address
   <doc>Return the socket local address composed of an (host,port) array</doc>
**/
Array<int> _hx_std_socket_host( Dynamic o )
{
   SOCKET sock = val_sock(o);
   struct sockaddr_in addr;
   SockLen addrlen = sizeof(addr);
   hx::EnterGCFreeZone();
   if( getsockname(sock,(struct sockaddr*)&addr,&addrlen) == SOCKET_ERROR )
   {
      hx::ExitGCFreeZone();
      return null();
   }
   hx::ExitGCFreeZone();

   Array<int> ret = Array_obj<int>::__new(2,2);
   ret[0] = *(int*)&addr.sin_addr;
   ret[1] = ntohs(addr.sin_port);
   return ret;
}

/**
   socket_set_timeout : 'socket -> timout:number? -> void
   <doc>Set the socket send and recv timeout in seconds to the given value (or null for blocking)</doc>
**/
void _hx_std_socket_set_timeout( Dynamic o, Dynamic t )
{
   SOCKET sock = val_sock(o);

   // null means no timeout (0).  Reject nonsense rather than configuring a
   // garbage timeout (the old code passed an uninitialized timeval for
   // negative values), and clamp huge values instead of overflowing
   double seconds = t.mPtr ? (double)t : 0.0;
   if( seconds < 0 || seconds != seconds )
      hx::Throw(HX_CSTRING("Invalid socket timeout"));
   if( seconds > 0x7fffffff )
      seconds = 0x7fffffff;

#ifdef NEKO_WINDOWS
   int time;
   if( seconds == 0 )
      time = 0;
   else {
      double ms = seconds * 1000;
      time = ms > 0x7fffffff ? 0x7fffffff : (int)ms;
      // Sub-millisecond must not round to 0 = block forever
      if( time == 0 )
         time = 1;
   }
#else
   struct timeval time;
   time.tv_usec = (int)( (seconds - (int)seconds) * 1000000 );
   time.tv_sec = (int)seconds;
   // Sub-microsecond must not round to 0 = block forever
   if( seconds > 0 && time.tv_sec == 0 && time.tv_usec == 0 )
      time.tv_usec = 1;
#endif

   hx::EnterGCFreeZone();
   if( setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,(char*)&time,sizeof(time)) != 0
    || setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&time,sizeof(time)) != 0 )
   {
      #ifdef __EMSCRIPTEN__
      // Emscripten's sockets have neither option, and setsockopt fails with
      // ENOPROTOOPT. A socket without them has no timeout, which is what zero
      // asks for, so only a positive timeout, which it cannot keep, fails.
      int err = errno;
      hx::ExitGCFreeZone();
      if( seconds == 0 && err == ENOPROTOOPT )
         return;
      #else
      hx::ExitGCFreeZone();
      #endif
      hx::Throw(HX_CSTRING("Could not set socket timeout"));
   }
   hx::ExitGCFreeZone();
}

/**
   socket_shutdown : 'socket -> read:bool -> write:bool -> void
   <doc>Prevent the socket from further reading or writing or both.</doc>
**/
void _hx_std_socket_shutdown( Dynamic o, bool r, bool w )
{
   hx::Object *handle = val_handle(o);
   SocketWrapper *wrap = ownWrapper(handle);
   SOCKET sock = handle ? handleSocket(handle) : INVALID_SOCKET;
   if( !r && !w )
      return;

   hx::EnterGCFreeZone();
   if( shutdown(sock,(r)?((w)?SHUT_RDWR:SHUT_RD):SHUT_WR) )
   {
      // Keep not-connected silent - shutting down defensively before
      // close is a common pattern.  Real failures should be visible
      #ifdef NEKO_WINDOWS
      int err = WSAGetLastError();
      hx::ExitGCFreeZone();
      if( err != WSAENOTCONN )
         hx::Throw(HX_CSTRING("Shutdown failed"));
      #else
      int err = errno;
      hx::ExitGCFreeZone();
      if( err != ENOTCONN )
         hx::Throw(HX_CSTRING("Shutdown failed"));
      #endif
      return;
   }
   hx::ExitGCFreeZone();

   #ifndef NEKO_LINUX
   // Shut for reading, the kernel drops what it holds and later reads find
   // none, or fail on Windows, so the bytes read ahead go too: Windows still
   // reports them as readable. Linux goes on giving what it holds, and the
   // bytes read ahead come first, as they did.
   ReadAhead *ahead = r && wrap ? wrap->currentAhead() : 0;
   if (ahead)
      #ifdef NEKO_WINDOWS
      ahead->discard(true);
      #else
      ahead->discard();
      #endif
   #endif
}

/**
   socket_set_blocking : 'socket -> bool -> void
   <doc>Turn on/off the socket blocking mode.</doc>
**/
#include <stdlib.h>

void _hx_std_socket_set_blocking( Dynamic o, bool b )
{
   SOCKET sock = val_sock(o);
   hx::EnterGCFreeZone();
#ifdef NEKO_WINDOWS
   {
      unsigned long arg = b?0:1;
      if( ioctlsocket(sock,FIONBIO,&arg) != 0 )
      {
         hx::ExitGCFreeZone();
         // Failing silently leaves the socket in the wrong mode and the
         // caller spinning or blocked with no clue why
         hx::Throw(HX_CSTRING("Set blocking failed"));
      }
   }
#else
   {
      int rights = fcntl(sock,F_GETFL);
      if( rights == -1 )
      {
         hx::ExitGCFreeZone();
         hx::Throw(HX_CSTRING("Set blocking failed"));
      }
      if( b )
         rights &= ~O_NONBLOCK;
      else
         rights |= O_NONBLOCK;
      if( fcntl(sock,F_SETFL,rights) == -1 )
      {
         hx::ExitGCFreeZone();
         hx::Throw(HX_CSTRING("Set blocking failed"));
      }
   }
#endif
   hx::ExitGCFreeZone();
   return;
}


void _hx_std_socket_set_fast_send( Dynamic o, bool b )
{
   SOCKET sock = val_sock(o);
   int fast = (b);
   hx::EnterGCFreeZone();
   setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&fast,sizeof(fast));
   hx::ExitGCFreeZone();
}

void _hx_std_socket_set_broadcast( Dynamic o, bool b )
{
   SOCKET sock = val_sock(o);
   int broadcast = (b);
   hx::EnterGCFreeZone();
   setsockopt(sock,SOL_SOCKET,SO_BROADCAST,(char*)&broadcast,sizeof(broadcast));
   hx::ExitGCFreeZone();
}


/**
   socket_poll_alloc : int -> 'poll
   <doc>Allocate memory to perform polling on a given number of sockets</doc>
**/

namespace
{

static int pollType = 0;

SOCKET wrapped_sock(Dynamic inWrapper)
{
   return inWrapper.mPtr ? handleSocket(inWrapper.mPtr) : INVALID_SOCKET;
}

#ifdef HX_POLL_QUEUE
// A poll's registrations, kept in the kernel from one events call to the
// next, with epoll on Linux and kqueue on macOS and the BSDs. poll() is
// handed every descriptor on every call, and takes as long as they take to
// look at, ready or not: 4,096 idle sockets cost 0.3 ms a call. prepare
// tells the kernel what changed since the last prepare, and an events call
// costs what is ready. The results are as poll() gives them: each ready
// index once, in the order prepared, a hang-up or error as ready both ways,
// and a socket closed after prepare as ready, as poll() reported its
// descriptor.
struct PollQueue
{
   // UNKNOWN: what the kernel has for the descriptor is not known
   enum { READ = 1, WRITE = 2, UNKNOWN = 4 };

   // One descriptor, for every read and write index that names it
   struct Slot
   {
      int fd;
      // Of the socket the descriptor was registered for: one that has the
      //  number of a closed socket is not registered yet. 0 for a library's
      //  handle, which has no serial
      unsigned int serial;
      int want;
      int registered;
      int firstRead;
      int firstWrite;
   };

   // For a descriptor, its slot and the slot a prepare in progress gives it,
   //  each plus one, or 0
   struct FdSlots
   {
      int registered;
      int pending;
   };

   int fd;
   std::vector<Slot> slots;
   std::vector<Slot> previous;
   std::vector<FdSlots> slotOf;
   // The read and write indexes of each slot, linked
   std::vector<int> nextRead;
   std::vector<int> nextWrite;
   // The descriptor each index had at prepare, or -1
   std::vector<int> readFds;
   std::vector<int> writeFds;
   // Indexes ready on every call: those the kernel would not register, as
   //  poll() reports a descriptor it cannot poll, and those closed since
   std::vector<int> failedReads;
   std::vector<int> failedWrites;
   std::vector<int> closedReads;
   std::vector<int> closedWrites;
   unsigned int closedSeen;
   // What the last wait found ready, sorted
   std::vector<int> readyReads;
   std::vector<int> readyWrites;
   #ifdef HX_POLL_EPOLL
   std::vector<struct epoll_event> events;
   #else
   std::vector<struct kevent> events;
   #endif

   static PollQueue *create()
   {
      #ifdef HX_POLL_EPOLL
      #if defined(__ANDROID__) && __ANDROID_API__ < 21
      int q = epoll_create(1);
      if (q >= 0)
         fcntl(q,F_SETFD,FD_CLOEXEC);
      #else
      int q = epoll_create1(EPOLL_CLOEXEC);
      #endif
      #else
      int q = kqueue();
      if (q >= 0)
         fcntl(q,F_SETFD,FD_CLOEXEC);
      #endif
      if (q < 0)
         return 0;
      PollQueue *queue = new PollQueue();
      queue->fd = q;
      queue->closedSeen = 0;
      // Room for a wait before the first prepare
      queue->events.resize(1);
      return queue;
   }

   ~PollQueue()
   {
      close(fd);
   }

   // Tells the kernel a descriptor's interest changed from inFrom to inTo,
   //  where inFrom can be UNKNOWN
   bool change(int inFd, int inFrom, int inTo)
   {
      #ifdef HX_POLL_EPOLL
      struct epoll_event ev;
      memset(&ev,0,sizeof(ev));
      ev.events = ((inTo & READ) ? EPOLLIN : 0) | ((inTo & WRITE) ? EPOLLOUT : 0);
      ev.data.fd = inFd;
      int op = inTo==0 ? EPOLL_CTL_DEL : inFrom==0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
      if (epoll_ctl(fd, op, inFd, &ev) == 0)
         return true;
      // A registration the kernel still has for a descriptor that was
      //  closed and given out again
      if (op == EPOLL_CTL_ADD && errno == EEXIST)
         return epoll_ctl(fd, EPOLL_CTL_MOD, inFd, &ev) == 0;
      // None, for a descriptor whose registration went with the socket
      //  that had it
      if (op == EPOLL_CTL_MOD && errno == ENOENT)
         return epoll_ctl(fd, EPOLL_CTL_ADD, inFd, &ev) == 0;
      return false;
      #else
      bool ok = true;
      static const int filters[2] = { EVFILT_READ, EVFILT_WRITE };
      for(int f=0;f<2;f++)
      {
         int bit = f==0 ? READ : WRITE;
         if (inFrom != UNKNOWN && (inFrom & bit) == (inTo & bit))
            continue;
         struct kevent ev;
         EV_SET(&ev, inFd, filters[f], (inTo & bit) ? EV_ADD : EV_DELETE, 0, 0, 0);
         if (kevent(fd, &ev, 1, 0, 0, 0) < 0 && (inTo & bit))
            ok = false;
      }
      return ok;
      #endif
   }

   void add(int inIndex, hx::Object *inHandle, bool inRead)
   {
      int sock = inHandle ? handleSocket(inHandle) : INVALID_SOCKET;
      (inRead ? readFds : writeFds)[inIndex] = sock;
      if (sock < 0)
         return;
      if ((size_t)sock >= slotOf.size())
      {
         FdSlots none = { 0, 0 };
         slotOf.resize(sock + 1 + sock/2, none);
      }
      FdSlots &at = slotOf[sock];
      if (!at.pending)
      {
         SocketWrapper *wrap = ownWrapper(inHandle);
         Slot slot = { sock, wrap ? wrap->serial : 0, 0, 0, -1, -1 };
         slots.push_back(slot);
         at.pending = (int)slots.size();
      }
      Slot &slot = slots[at.pending-1];
      if (inRead)
      {
         nextRead[inIndex] = slot.firstRead;
         slot.firstRead = inIndex;
         slot.want |= READ;
      }
      else
      {
         nextWrite[inIndex] = slot.firstWrite;
         slot.firstWrite = inIndex;
         slot.want |= WRITE;
      }
   }

   void prepare(Array<Dynamic> inReads, Array<Dynamic> inWrites)
   {
      int len = inReads.mPtr ? inReads->length : 0;
      int wlen = inWrites.mPtr ? inWrites->length : 0;
      previous.swap(slots);
      slots.clear();
      readFds.assign(len, -1);
      writeFds.assign(wlen, -1);
      nextRead.assign(len, -1);
      nextWrite.assign(wlen, -1);
      failedReads.clear();
      failedWrites.clear();
      closedReads.clear();
      closedWrites.clear();
      closedSeen = sSocketsClosed.load();

      for(int i=0;i<len;i++)
         add(i, inReads[i].mPtr, true);
      for(int i=0;i<wlen;i++)
         add(i, inWrites[i].mPtr, false);

      // The last prepare's registrations carry over for the same socket.
      //  Those of a socket left out, or closed and its number given to
      //  another, go: a closed descriptor's is gone already, and taking out
      //  one the kernel no longer has fails harmlessly. A library's handle
      //  has no serial, so the one prepared now may be the socket that was
      //  registered or another the descriptor was given to since, whose
      //  registration the kernel dropped when the first closed: it is
      //  registered again, as UNKNOWN.
      for(size_t j=0;j<previous.size();j++)
      {
         Slot &old = previous[j];
         FdSlots &at = slotOf[old.fd];
         at.registered = 0;
         if (at.pending && slots[at.pending-1].serial==old.serial)
            slots[at.pending-1].registered = old.serial || !old.registered ? old.registered : (int)UNKNOWN;
         else if (old.registered)
            change(old.fd, old.registered, 0);
      }

      for(size_t j=0;j<slots.size();j++)
      {
         Slot &slot = slots[j];
         FdSlots &at = slotOf[slot.fd];
         at.registered = (int)j + 1;
         at.pending = 0;
         if (slot.registered == slot.want)
            continue;
         if (change(slot.fd, slot.registered, slot.want))
            slot.registered = slot.want;
         else
         {
            change(slot.fd, slot.want, 0);
            slot.registered = 0;
            for(int i=slot.firstRead;i>=0;i=nextRead[i])
               failedReads.push_back(i);
            for(int i=slot.firstWrite;i>=0;i=nextWrite[i])
               failedWrites.push_back(i);
         }
      }
      std::sort(failedReads.begin(), failedReads.end());
      std::sort(failedWrites.begin(), failedWrites.end());

      #ifdef HX_POLL_EPOLL
      events.resize(slots.size() ? slots.size() : 1);
      #else
      events.resize(slots.size() ? slots.size()*2 : 1);
      #endif
   }

   // The indexes whose socket was closed after prepare. Looked for only
   //  after a socket was closed somewhere.
   void findClosed(Array<Dynamic> inReads, Array<Dynamic> inWrites)
   {
      unsigned int closed = sSocketsClosed.load();
      if (closed == closedSeen)
         return;
      closedSeen = closed;
      closedReads.clear();
      closedWrites.clear();
      for(size_t i=0;i<readFds.size();i++)
         if (readFds[i] >= 0 && wrapped_sock(inReads[i]) == INVALID_SOCKET)
            closedReads.push_back((int)i);
      for(size_t i=0;i<writeFds.size();i++)
         if (writeFds[i] >= 0 && wrapped_sock(inWrites[i]) == INVALID_SOCKET)
            closedWrites.push_back((int)i);
   }

   // Waits as poll() would, and fills readyReads and readyWrites: false
   //  when the wait failed
   bool wait(int inWaitMs, double inTimeout, double inDeadline)
   {
      readyReads.clear();
      readyWrites.clear();
      int waitMs = inWaitMs;
      int n;
      hx::EnterGCFreeZone();
      while(true)
      {
         #ifdef HX_POLL_EPOLL
         n = epoll_wait(fd, &events[0], (int)events.size(), waitMs);
         #else
         struct timespec ts;
         ts.tv_sec = waitMs / 1000;
         ts.tv_nsec = (waitMs % 1000) * 1000000L;
         n = kevent(fd, 0, 0, &events[0], (int)events.size(), waitMs < 0 ? 0 : &ts);
         #endif
         if (n >= 0 || errno != EINTR)
            break;
         if (inTimeout >= 0)
         {
            double left = inDeadline - monotonicSeconds();
            waitMs = left > 0 ? (int)(left * 1000) + 1 : 0;
         }
      }
      hx::ExitGCFreeZone();
      if (n < 0)
         return false;

      for(int e=0;e<n;e++)
      {
         #ifdef HX_POLL_EPOLL
         int ready = events[e].data.fd;
         unsigned int flags = events[e].events;
         bool readable = flags & (EPOLLIN | EPOLLHUP | EPOLLERR);
         bool writable = flags & (EPOLLOUT | EPOLLHUP | EPOLLERR);
         #else
         int ready = (int)events[e].ident;
         bool readable = events[e].filter == EVFILT_READ;
         bool writable = events[e].filter == EVFILT_WRITE;
         #endif
         if (ready < 0 || (size_t)ready >= slotOf.size() || !slotOf[ready].registered)
            continue;
         Slot &slot = slots[slotOf[ready].registered-1];
         if (readable)
            for(int i=slot.firstRead;i>=0;i=nextRead[i])
               readyReads.push_back(i);
         if (writable)
            for(int i=slot.firstWrite;i>=0;i=nextWrite[i])
               readyWrites.push_back(i);
      }
      std::sort(readyReads.begin(), readyReads.end());
      std::sort(readyWrites.begin(), readyWrites.end());
      return true;
   }
};
#endif

struct polldata : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdPollData };

   bool ok;
   int max;
   #ifdef NEKO_WINDOWS
   struct fd_set *fdr;
   struct fd_set *fdw;
   struct fd_set *outr;
   struct fd_set *outw;
   // The exception set: every read socket, then every write socket. Windows
   //  reports a failed nonblocking connect only there
   struct fd_set *oute;
   #else
   struct pollfd *fds;
   int rcount;
   int wcount;
   #ifdef HX_POLL_QUEUE
   // Null where the kernel would not make one, and the poll uses poll()
   PollQueue *queue;
   #endif
   #endif
   Array<int> ridx;
   Array<int> widx;
   // The wrappers of the sockets registered by the last prepare: the poll
   //  holds their descriptors, so it must keep them, or a collection could
   //  finalize one and close the connection while it is still being polled.
   //  Null for a sys.net.Socket that had none.
   Array<Dynamic> rsockets;
   Array<Dynamic> wsockets;

   void create(int nsocks)
   {
      ok = true;
      max = nsocks;
      #ifdef NEKO_WINDOWS
      fdr = (fd_set*)malloc(FDSIZE(max));
      fdw = (fd_set*)malloc(FDSIZE(max));
      outr = (fd_set*)malloc(FDSIZE(max));
      outw = (fd_set*)malloc(FDSIZE(max));
      oute = (fd_set*)malloc(FDSIZE(max));
      fdr->fd_count = 0;
      fdw->fd_count = 0;

      #else
      fds = (struct pollfd*)malloc(sizeof(struct pollfd) * max);
      rcount = 0;
      wcount = 0;
      #ifdef HX_POLL_QUEUE
      queue = PollQueue::create();
      #endif
      #endif

      ridx = Array_obj<int>::__new(max+1,max+1);
      HX_OBJ_WB_GET(this, ridx.mPtr);
      widx = Array_obj<int>::__new(max+1,max+1);
      HX_OBJ_WB_GET(this, widx.mPtr);
      for(int i=0;i<=max;i++)
      {
         ridx[i] = -1;
         widx[i] = -1;
      }

      _hx_set_finalizer(this, finalize);
   }

   void destroy()
   {
      if (ok)
      {
         ok = false;
         #ifdef NEKO_WINDOWS
         free(fdr);
         free(fdw);
         free(outr);
         free(outw);
         free(oute);
         #else
         free(fds);
         #ifdef HX_POLL_QUEUE
         delete queue;
         queue = 0;
         #endif
         #endif
      }
   }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_MARK_MEMBER(ridx);
      HX_MARK_MEMBER(widx);
      HX_MARK_MEMBER(rsockets);
      HX_MARK_MEMBER(wsockets);
   }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
   {
      HX_VISIT_MEMBER(ridx);
      HX_VISIT_MEMBER(widx);
      HX_VISIT_MEMBER(rsockets);
      HX_VISIT_MEMBER(wsockets);
   }
   #endif

   int __GetType() const HXCPP_OVERRIDE { return pollType; }

   static void finalize(Dynamic obj)
   {
      ((polldata *)(obj.mPtr))->destroy();
   }

   String toString() HXCPP_OVERRIDE { return HX_CSTRING("polldata"); }
};

polldata *val_poll(Dynamic o)
{
   if (!o.mPtr || o->__GetType()!=pollType)
      hx::Throw(HX_CSTRING("Invalid polldata:") + o);
   return static_cast<polldata *>(o.mPtr);
}

// The read indexes of the sockets holding bytes read ahead, which the
// kernel does not count as readable
void poll_read_ahead(polldata *p, std::vector<int> &outHeld)
{
   if (!sReadAheadHeld.load() || !p->rsockets.mPtr)
      return;
   for(int i=0;i<p->rsockets->length;i++)
   {
      SocketWrapper *wrap = ownWrapper(p->rsockets[i].mPtr);
      if (wrap && wrap->socket != INVALID_SOCKET && wrap->holdsBytes())
         outHeld.push_back(i);
   }
}

// ioIndexes holds inCount indexes, ascending, and -1: adds inExtra, also
// ascending, to them, once each, and returns how many there are now
int merge_indexes(Array<int> &ioIndexes, int inCount, const std::vector<int> &inExtra)
{
   if (inExtra.empty())
      return inCount;
   int *base = (int *)ioIndexes->GetBase();
   std::vector<int> merged(inCount + inExtra.size());
   std::vector<int>::iterator last = std::set_union(base, base + inCount, inExtra.begin(), inExtra.end(), merged.begin());
   int count = (int)(last - merged.begin());
   std::copy(merged.begin(), last, base);
   base[count] = -1;
   return count;
}


} // end namespace



Dynamic _hx_std_socket_poll_alloc( int nsocks )
{
   if( nsocks < 0 || nsocks > 1000000 )
      return null();

   if (pollType==0)
      pollType = hxcpp_alloc_kind();

   polldata *p = new polldata;
   p->create(nsocks);
   return p;
}

/**
   socket_poll_prepare : 'poll -> read:'socket array -> write:'socket array -> int array array
   <doc>
   Prepare a poll for scanning events on sets of sockets.
   </doc>
**/
Array<Dynamic> _hx_std_socket_poll_prepare( Dynamic pdata, Array<Dynamic> rsocks, Array<Dynamic> wsocks )
{
   polldata *p = val_poll(pdata);

   int len = rsocks.mPtr ? rsocks->length : 0;
   int wlen = wsocks.mPtr ? wsocks->length : 0;
   if( len + wlen > p->max )
      hx::Throw(HX_CSTRING("Too many sockets in poll"));

   // Their own arrays, so that a caller changing its arrays afterwards
   //  cannot drop a socket the poll still has the descriptor of
   Array<Dynamic> rwraps = Array_obj<Dynamic>::__new(len,len);
   for(int i=0;i<len;i++)
      rwraps[i] = val_handle(rsocks[i]);
   Array<Dynamic> wwraps = Array_obj<Dynamic>::__new(wlen,wlen);
   for(int i=0;i<wlen;i++)
      wwraps[i] = val_handle(wsocks[i]);
   p->rsockets = len ? rwraps : null();
   HX_OBJ_WB_GET(p, p->rsockets.mPtr);
   p->wsockets = wlen ? wwraps : null();
   HX_OBJ_WB_GET(p, p->wsockets.mPtr);

   #ifdef NEKO_WINDOWS
   for(int i=0;i<len;i++)
      p->fdr->fd_array[i] = wrapped_sock( rwraps[i] );
   p->fdr->fd_count = len;

   len = wlen;
   for(int i=0;i<len;i++)
      p->fdw->fd_array[i] = wrapped_sock( wwraps[i]);
   p->fdw->fd_count = len;

   #else

   #ifdef HX_POLL_QUEUE
   if (p->queue)
   {
      p->queue->prepare(rwraps, wwraps);
      Array<Dynamic> a = Array_obj<Dynamic>::__new(2,2);
      a[0] = p->ridx;
      a[1] = p->widx;
      return a;
   }
   #endif

   for(int i=0;i<len;i++)
   {
      p->fds[i].fd = wrapped_sock( rwraps[i] );
      p->fds[i].events = POLLIN;
      p->fds[i].revents = 0;
   }
   p->rcount = len;
   len = wlen;
   for(int i=0;i<len;i++)
   {
      int k = i + p->rcount;
      p->fds[k].fd = wrapped_sock(wwraps[i]);
      p->fds[k].events = POLLOUT;
      p->fds[k].revents = 0;
   }
   p->wcount = len;

   #endif

   Array<Dynamic> a = Array_obj<Dynamic>::__new(2,2);
   a[0] = p->ridx;
   a[1] = p->widx;
   return a;
}


/**
   socket_poll_events : 'poll -> timeout:float -> void
   <doc>
   Update the read/write flags arrays that were created with [socket_poll_prepare].
   </doc>
**/
void _hx_std_socket_poll_events(Dynamic pdata, double timeout)
{
   polldata *p = val_poll(pdata);

   p->ridx[0] = -1;
   p->widx[0] = -1;

   // Sockets holding bytes read ahead are ready, so look for the others
   // without waiting
   std::vector<int> held;
   poll_read_ahead(p, held);
   if (!held.empty())
      timeout = 0;

#ifdef NEKO_WINDOWS
   // Winsock's select fails at once with no sockets, which looked like an
   // empty poll done: an event loop with nothing registered spun at full CPU.
   // With nothing to wait for, wait out the timeout, as poll does elsewhere.
   if (p->fdr->fd_count == 0 && p->fdw->fd_count == 0)
   {
      DWORD ms = INFINITE;
      if (timeout >= 0)
         ms = timeout * 1000 >= (double)(INFINITE - 1) ? INFINITE - 1 : (DWORD)ceil(timeout * 1000);
      if (ms)
      {
         hx::EnterGCFreeZone();
         Sleep(ms);
         hx::ExitGCFreeZone();
      }
      return;
   }

   memcpy(p->outr, p->fdr, FDSIZE(p->fdr->fd_count));
   memcpy(p->outw, p->fdw, FDSIZE(p->fdw->fd_count));

   // The exception set is every read socket, then every write socket: a
   // failed connect is reported only there, for a socket polled for reading
   // or for writing. It was a stack fd_set of the first FD_SETSIZE (64) read
   // sockets, so a failed socket after them, or one polled only for writing,
   // was never reported.
   fd_set *oute = p->oute;
   {
      u_int count = 0;
      for (u_int i = 0; i < p->fdr->fd_count; ++i)
         oute->fd_array[count++] = p->fdr->fd_array[i];
      for (u_int i = 0; i < p->fdw->fd_count; ++i)
         oute->fd_array[count++] = p->fdw->fd_array[i];
      oute->fd_count = count;
   }

   struct timeval t;
   struct timeval *tt = init_timeval(timeout, &t);

   hx::EnterGCFreeZone();
   int sel = select(
       0,
       p->fdr->fd_count ? p->outr : 0,
       p->fdw->fd_count ? p->outw : 0,
       oute->fd_count ? oute : 0,
       tt);
   if (sel == SOCKET_ERROR)
   {
      hx::ExitGCFreeZone();

      int k = 0;
      for (u_int i = 0; i < p->fdr->fd_count; ++i)
         p->ridx[k++] = i;
      p->ridx[k] = -1;

      return;
   }
   hx::ExitGCFreeZone();

   // Which of the sockets asked about are ready. select leaves each set
   // holding only its ready sockets, in the order they were given, so one
   // walk of the registered sockets alongside them finds every one: linear
   // in the sockets registered plus those ready. FD_ISSET, asked for each
   // registered socket, searched the ready set from its start every time -
   // registered x ready comparisons, 1.9 ms a pass at 4,096 sockets with
   // half of them ready. Should a set come back in another order the walk
   // misses some, and that pass is decided by FD_ISSET as it always was.
   int k = 0;
   // The exception set's ready sockets, in order: the read sockets' first
   u_int e = 0;
   {
      fd_set *ready = p->outr;
      u_int r = 0;
      for (u_int i = 0; i < p->fdr->fd_count; ++i)
      {
         SOCKET fd = p->fdr->fd_array[i];
         bool hit = false;
         if (r < ready->fd_count && ready->fd_array[r] == fd)
         {
            hit = true;
            r++;
         }
         if (e < oute->fd_count && oute->fd_array[e] == fd)
         {
            hit = true;
            e++;
         }
         if (hit)
            p->ridx[k++] = i;
      }
      if (r != ready->fd_count)
         e = oute->fd_count + 1;
      if (e > oute->fd_count)
      {
         k = 0;
         for (u_int i = 0; i < p->fdr->fd_count; ++i)
         {
            SOCKET fd = p->fdr->fd_array[i];
            if (FD_ISSET(fd, p->outr) || FD_ISSET(fd, oute))
               p->ridx[k++] = i;
         }
      }
   }
   p->ridx[k] = -1;
   merge_indexes(p->ridx, k, held);

   k = 0;
   {
      fd_set *ready = p->outw;
      u_int w = 0;
      for (u_int i = 0; i < p->fdw->fd_count; ++i)
      {
         SOCKET fd = p->fdw->fd_array[i];
         bool hit = false;
         if (w < ready->fd_count && ready->fd_array[w] == fd)
         {
            hit = true;
            w++;
         }
         if (e < oute->fd_count && oute->fd_array[e] == fd)
         {
            hit = true;
            e++;
         }
         if (hit)
            p->widx[k++] = i;
      }
      if (w != ready->fd_count || e != oute->fd_count)
      {
         k = 0;
         for (u_int i = 0; i < p->fdw->fd_count; ++i)
         {
            SOCKET fd = p->fdw->fd_array[i];
            if (FD_ISSET(fd, p->outw) || FD_ISSET(fd, oute))
               p->widx[k++] = i;
         }
      }
   }
   p->widx[k] = -1;

#else
   int waitMs = (int)(timeout * 1000);
   double deadline = monotonicSeconds() + timeout;

   #ifdef HX_POLL_QUEUE
   if (p->queue)
   {
      PollQueue *q = p->queue;
      q->findClosed(p->rsockets, p->wsockets);
      if (!q->failedReads.empty() || !q->failedWrites.empty() || !q->closedReads.empty() || !q->closedWrites.empty())
         waitMs = 0;
      q->wait(waitMs, timeout, deadline);
      int k = merge_indexes(p->ridx, 0, q->readyReads);
      int kw = merge_indexes(p->widx, 0, q->readyWrites);
      k = merge_indexes(p->ridx, k, held);
      k = merge_indexes(p->ridx, k, q->failedReads);
      merge_indexes(p->ridx, k, q->closedReads);
      kw = merge_indexes(p->widx, kw, q->failedWrites);
      merge_indexes(p->widx, kw, q->closedWrites);
      return;
   }
   #endif

   int tot = p->rcount + p->wcount;
   hx::EnterGCFreeZone();
poll_events_again:
   if (poll(p->fds, tot, waitMs) < 0)
   {
      if (errno == EINTR)
      {
         if (timeout >= 0)
         {
            double left = deadline - monotonicSeconds();
            waitMs = left > 0 ? (int)(left * 1000) + 1 : 0;
         }
         goto poll_events_again;
      }
      hx::ExitGCFreeZone();

      merge_indexes(p->ridx, 0, held);
      return;
   }
   hx::ExitGCFreeZone();

   int k = 0;
   int i = 0;

   for (i = 0; i < p->rcount; ++i)
   {
      if (p->fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))
         p->ridx[k++] = i;
   }
   p->ridx[k] = -1;
   merge_indexes(p->ridx, k, held);

   k = 0;

   for (; i < tot; ++i)
   {
      if (p->fds[i].revents & (POLLOUT | POLLHUP | POLLERR | POLLNVAL))
         p->widx[k++] = i - p->rcount;
   }
   p->widx[k] = -1;
#endif
}

/**
   socket_poll : 'socket array -> 'poll -> timeout:float -> 'socket array
   <doc>
   Perform a polling for data available over a given set of sockets. This is similar to [socket_select]
   except that [socket_select] is limited to a given number of simultaneous sockets to check.
   </doc>
**/
Array<Dynamic> _hx_std_socket_poll( Array<Dynamic> socks, Dynamic pdata, double timeout )
{
   polldata *p = val_poll(pdata);

   _hx_std_socket_poll_prepare(pdata,socks,null());

   _hx_std_socket_poll_events(pdata,timeout);

   int rcount = 0;
   while( p->ridx[rcount] != -1 )
      rcount++;

   Array<Dynamic> a = Array_obj<Dynamic>::__new(rcount,rcount);
   for(int i=0;i<rcount;i++)
      a[i] = socks[p->ridx[i]];
   return a;
}



/**
   socket_send_to : 'socket -> buf:string -> pos:int -> length:int -> addr:{host:'int32,port:int} -> int
   <doc>
   Send data from an unconnected UDP socket to the given address.
   </doc>
**/
int _hx_std_socket_send_to( Dynamic o, Array<unsigned char> buf, int p, int l, Dynamic inAddr )
{
   SOCKET sock = val_sock(o);

   int dlen = buf->length;
   if( p < 0 || l < 0 || p > dlen || l > dlen - p )
      hx::Throw(HX_CSTRING("Invalid data position"));


   int host = inAddr->__Field(HX_CSTRING("host"), hx::paccDynamic);
   int port = inAddr->__Field(HX_CSTRING("port"), hx::paccDynamic);
   struct sockaddr_in addr;
   memset(&addr,0,sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   *(int*)&addr.sin_addr.s_addr = host;

   // Capture after the field lookups above (which can allocate and move
   // buf); volatile keeps the allocation start in the scanned frame
   const char * volatile cdata = (const char *)buf->GetBase();

   hx::EnterGCFreeZone();
   POSIX_LABEL(send_again);
   dlen = sendto(sock, cdata + p , l, MSG_NOSIGNAL, (struct sockaddr*)&addr, sizeof(addr));
   if( dlen == SOCKET_ERROR ) {
      HANDLE_EINTR(send_again);
      block_error();
   }
   hx::ExitGCFreeZone();
   return dlen;
}

/**
   socket_recv_from : 'socket -> buf:string -> pos:int -> length:int -> addr:{host:'int32,port:int} -> int
   <doc>
   Read data from an unconnected UDP socket, store the address from which we received data in addr.
   </doc>
**/
#define NRETRYS   20
int _hx_std_socket_recv_from( Dynamic o, Array<unsigned char> buf, int p, int l, Dynamic outAddr)
{
   int retry = 0;
   hx::Object *handle = val_handle(o);
   SocketWrapper *wrap = ownWrapper(handle);
   SOCKET sock = handle ? handleSocket(handle) : INVALID_SOCKET;

   int dlen = buf->length;
   if( p < 0 || l < 0 || p > dlen || l > dlen - p )
      hx::Throw(HX_CSTRING("Invalid data position"));

   // A stream socket has no sender address to give, as recvfrom gives none
   ReadAhead *ahead = wrap && l > 0 ? wrap->readAhead() : 0;
   ReadAheadLock lock(ahead);
   if (ahead && ahead->unread() && sock != INVALID_SOCKET)
   {
      int got = ahead->take((unsigned char *)buf->GetBase() + p, l);
      outAddr->__SetField(HX_CSTRING("host"),0, hx::paccDynamic);
      outAddr->__SetField(HX_CSTRING("port"),0, hx::paccDynamic);
      return got;
   }

   char * volatile data = (char *)buf->GetBase();

   // Zeroed so the plain-recv retry fallback below reports 0.0.0.0:0
   // instead of uninitialized stack memory
   struct sockaddr_in saddr;
   memset(&saddr,0,sizeof(saddr));
   SockLen slen = sizeof(saddr);

   int ret = 0;
   hx::EnterGCFreeZone();
   POSIX_LABEL(recv_from_again);
   if( retry++ > NRETRYS ) {
      ret = recv(sock,data+p,l,MSG_NOSIGNAL);
   } else
      ret = recvfrom(sock, data + p , l, MSG_NOSIGNAL, (struct sockaddr*)&saddr, &slen);
   if( ret == SOCKET_ERROR ) {
      #ifdef NEKO_WINDOWS
      // Datagram larger than the buffer: the buffer and address are
      // filled, Winsock just insists on reporting the truncation.
      // posix truncates silently - match posix
      if( WSAGetLastError() == WSAEMSGSIZE )
         ret = l;
      else
      #endif
      {
         HANDLE_EINTR(recv_from_again);
         block_error();
      }
   }

   hx::ExitGCFreeZone();
   outAddr->__SetField(HX_CSTRING("host"),*(int*)&saddr.sin_addr, hx::paccDynamic);
   outAddr->__SetField(HX_CSTRING("port"),ntohs(saddr.sin_port), hx::paccDynamic);

   return ret;
}


#else // !HX_WINRT
// TODO: WinRT StreamSocket port
#endif // HX_WINRT

