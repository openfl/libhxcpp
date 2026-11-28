#include <hxcpp.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <atomic>
#ifndef HXCPP_SINGLE_THREADED_APP
#include <thread>
#include <mutex>
#include <condition_variable>
#include "../../thread/Detach.hpp"
#endif

#include <hx/OS.h>

#ifdef NEKO_WINDOWS
#   include <windows.h>
#   include <io.h>
#else
#   include <fcntl.h>
#   include <unistd.h>
#   include <errno.h>
#   include <sys/stat.h>
#endif

/**
   <doc>
   <h1>File</h1>
   <p>
   The file api can be used for different kind of file I/O.
   </p>
   </doc>
**/

namespace
{

#ifndef NEKO_WINDOWS
// fopen with the descriptor close-on-exec from the start, so a child that
// another thread starts meanwhile does not inherit it
static FILE *hxFopen(const char *inPath, const char *inMode)
{
   bool update = strchr(inMode,'+')!=0;
   // C11's exclusive create, which only goes with w (libhxcpp #391)
   bool exclusive = strchr(inMode,'x')!=0;
   int flags = 0;
   switch(inMode[0])
   {
      case 'r': flags = update ? O_RDWR : O_RDONLY; break;
      case 'w': flags = (update ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
      case 'a': flags = (update ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
      default: errno = EINVAL; return 0;
   }
   if (exclusive)
   {
      if (inMode[0]!='w')
      {
         errno = EINVAL;
         return 0;
      }
      flags |= O_EXCL;
   }
   #ifdef O_CLOEXEC
   flags |= O_CLOEXEC;
   #endif
   int fd;
   do
      fd = open(inPath, flags, 0666);
   while (fd<0 && errno==EINTR);
   if (fd<0)
      return 0;
   #ifndef O_CLOEXEC
   fcntl(fd, F_SETFD, FD_CLOEXEC);
   #endif
   // open did the exclusive part, and fdopen need not know x
   char streamMode[8];
   int modeLen = 0;
   for(const char *m = inMode; *m && modeLen<7; m++)
      if (*m!='x')
         streamMode[modeLen++] = *m;
   streamMode[modeLen] = 0;
   FILE *file = fdopen(fd, streamMode);
   if (!file)
   {
      int err = errno;
      close(fd);
      errno = err;
   }
   return file;
}
#endif

#ifndef HXCPP_SINGLE_THREADED_APP
// Closes the streams of files the collector found dead. Closing flushes, which
// can wait on a pipe reader or slow storage, and finalizers run with every
// thread stopped, so the wait was a pause for all of them (libhxcpp #331).
// This thread closes them once they run again; a collection does not wait for
// it, and exit flushes any stream still open
struct LateCloser
{
   std::mutex lock;
   std::condition_variable wake;
   std::vector<FILE *> pending;
   bool started = false;

   // False when no thread can be started, for the caller to close it
   bool add(FILE *inFile)
   {
      std::lock_guard<std::mutex> hold(lock);
      if (!started)
      {
         try
         {
            std::thread thread([this]() { run(); });
            hx::DetachThread(thread);
         }
         catch(...)
         {
            return false;
         }
         started = true;
      }
      pending.push_back(inFile);
      wake.notify_one();
      return true;
   }

   void run()
   {
      std::vector<FILE *> closing;
      std::unique_lock<std::mutex> hold(lock);
      for(;;)
      {
         wake.wait(hold, [this]() { return !pending.empty(); });
         closing.swap(pending);
         hold.unlock();
         for(FILE *file : closing)
            fclose(file);
         closing.clear();
         hold.lock();
      }
   }
};
// Never destroyed: the thread can still be closing at exit
static LateCloser &sLateCloser = *new LateCloser();
#endif

struct fio : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdFio };

   String name;
   FILE   *io;
   bool   closeIo;
   // A regular file opened only for reading: readByte takes bytes from
   // ahead, refilled a block at a time, instead of entering a GC-free zone
   // and calling fread for each one
   bool   readAhead;
   unsigned char *ahead;
   int    aheadPos;
   int    aheadLen;
   // The refill reached the end with no read failing there yet, which is
   // when feof was still false for a byte at a time
   bool   aheadAtEnd;
   std::atomic_flag aheadBusy;

   void create(FILE *inFile, String inName, bool inClose, bool inReadAhead = false)
   {
      name = inName;
      HX_OBJ_WB_GET(this,name.raw_ref());
      io = inFile;
      closeIo = inClose;
      readAhead = inReadAhead;
      ahead = 0;
      aheadPos = aheadLen = 0;
      aheadAtEnd = false;
      aheadBusy.clear();

      _hx_set_finalizer(this, finalize);
   }

   void releaseAhead()
   {
      free(ahead);
      ahead = 0;
      aheadPos = aheadLen = 0;
      aheadAtEnd = false;
   }

   void destroy(bool inForceClose = false)
   {
      if (io && (inForceClose || closeIo))
         fclose(io);
      io = 0;
      name = String();
      releaseAhead();
   }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(name); }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(name); }
   #endif

   static void finalize(Dynamic inObj)
   {
      fio *f = (fio *)(inObj.mPtr);
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (f->io && f->closeIo && sLateCloser.add(f->io))
         f->io = 0;
      #endif
      f->destroy();
   }

   String toString() HXCPP_OVERRIDE { return HX_CSTRING("fio:") + name; }

};

// Handles shared by threads: the second waits in a GC-free zone, so a
// collection never waits on it
struct AheadLock
{
   fio *f;
   AheadLock(fio *inF) : f(inF && inF->readAhead ? inF : 0)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (f && f->aheadBusy.test_and_set(std::memory_order_acquire))
      {
         hx::EnterGCFreeZone();
         while (f->aheadBusy.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
         hx::ExitGCFreeZone();
      }
      #endif
   }
   ~AheadLock()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (f)
         f->aheadBusy.clear(std::memory_order_release);
      #endif
   }
   int unread() const { return f ? f->aheadLen - f->aheadPos : 0; }
};

static const int READ_AHEAD = 4096;

static bool isRegularFile(FILE *inFile)
{
   #if defined(HX_WINRT)
   return false;
   #elif defined(NEKO_WINDOWS)
   HANDLE h = (HANDLE)_get_osfhandle(_fileno(inFile));
   return h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK;
   #else
   struct stat st;
   return fstat(fileno(inFile), &st) == 0 && S_ISREG(st.st_mode);
   #endif
}

fio *getFio(Dynamic handle, bool inRequireFile=true)
{
   fio *result = dynamic_cast<fio *>(handle.mPtr);
   if (!result || (!result->io && inRequireFile))
      hx::Throw( HX_CSTRING("Bad file handle") );
   return result;
}


static void file_error(const char *msg, String inName)
{
   hx::ExitGCFreeZone();
   Array<String> err = Array_obj<String>::__new(2,2);
   err->__unsafe_set(0, String(msg));
   err->__unsafe_set(1, inName);
   hx::Throw(err);
}

}

/**
   file_open : f:string -> r:string -> 'file
   <doc>
   Call the C function [fopen] with the file path and access rights. 
   Return the opened file or throw an exception if the file couldn't be open.
   </doc>
**/
Dynamic _hx_std_file_open( String fname, String r )
{
   FILE *file = 0;

   hx::strbuf buf0;
   hx::strbuf buf1;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   file = _wfopen(fname.wchar_str(&buf0),r.wchar_str(&buf1));
#else
   hx::EnterGCFreeZone();
   file = hxFopen(fname.utf8_str(&buf0),r.utf8_str(&buf1));
#endif
   if (!file)
      file_error("file_open",fname);
   const char *mode = r.utf8_str(&buf1);
   bool readAhead = mode[0]=='r' && !strchr(mode,'+') && isRegularFile(file);
   #ifdef NEKO_WINDOWS
   // Not in text mode, where the CRT turns CRLF into LF as it reads: the
   // bytes read ahead are fewer than the stream moved over, so tell and
   // relative seeks, which take them back, landed past unread text
   if (!strchr(mode,'b'))
      readAhead = false;
   #endif
   hx::ExitGCFreeZone();

   fio *f = new fio;
   f->create(file,fname,true,readAhead);
   return f;
}

/**
   file_close : 'file -> void
   <doc>Close an file. Any other operations on this file will fail</doc> 
**/
void _hx_std_file_close( Dynamic handle )
{
   fio *f = getFio(handle);
   // fclose releases the stream even when its final write fails
   FILE *io = f->io;
   String name = f->name;
   f->io = 0;
   f->name = String();
   f->releaseAhead();
   hx::EnterGCFreeZone();
   if (fclose(io) != 0)
      file_error("file_close",name);
   hx::ExitGCFreeZone();
}

/**
   file_name : 'file -> string
   <doc>Return the name of the file which was opened</doc>
**/
String hx_std_file_name( Dynamic handle )
{
   fio *fio = getFio(handle);
   return fio->name;
}

/**
   file_write : 'file -> s:string -> p:int -> l:int -> int
   <doc>
   Write up to [l] chars of string [s] starting at position [p]. 
   Returns the number of chars written which is >= 0.
   </doc>
**/
int _hx_std_file_write( Dynamic handle, Array<unsigned char> s, int p, int n )
{
   fio *f = getFio(handle);

   int buflen = s->length;
   int len = n;
   if( p < 0 || len < 0 || p > buflen || p + len > buflen )
      return 0;

   hx::AutoGCFreeZone zone;
#ifdef HX_WINDOWS
   // _isatty is true for ANY character device - NUL, a serial port, a printer - and none of
   // those accept WriteConsoleW. Only a real console has a console mode.
   DWORD console_mode;
   if (_isatty(_fileno(f->io)) && GetConsoleMode((HANDLE)_get_osfhandle(_fileno(f->io)), &console_mode)) {
      fflush(f->io);
      HANDLE win_handle = (HANDLE)_get_osfhandle(_fileno(f->io));
      static const int MAX_CHARS = 4096;
      wchar_t buf[MAX_CHARS];
      int done = 0;
      bool convertOk = true;
      while (done < len) {
         // Convert in bounded chunks - a single oversized conversion fails
         // outright and previously reported the whole write as successful
         // while printing nothing.  Each utf8 byte yields at most one utf16
         // unit, so a chunk of MAX_CHARS bytes always fits the buffer.
         int chunk = len - done;
         if (chunk > MAX_CHARS) {
            chunk = MAX_CHARS;
            // Do not split a multi-byte utf8 sequence between chunks
            while (chunk > 0 && (s[p + done + chunk] & 0xC0) == 0x80)
               chunk--;
            if (chunk == 0)
               chunk = MAX_CHARS;
         }
         int result = MultiByteToWideChar(CP_UTF8, 0, (char *)&s[p + done], chunk, buf, MAX_CHARS);
         if (result == 0) {
            // Not convertible - write the remainder as raw bytes below
            convertOk = false;
            break;
         }
         DWORD written = 0;
         if(!WriteConsoleW(win_handle, buf, result, &written, NULL)) {
            zone.locked = false;
            file_error("file_write", f->name);
         }
         if ((int)written == result) {
            done += chunk;
            continue;
         }
         // Partial console write - report how many utf8 bytes were consumed
         // so the caller can retry the rest
         if (written < (DWORD)result) {
            wchar_t next = buf[written];
            if (next >= 0xDC00 && next <= 0xDFFF) {
               // Do not leave a surrogate pair half-written
               DWORD tmp;
               WriteConsoleW(win_handle, &buf[written], 1, &tmp, NULL);
               written += 1;
            }
         }
         int count = 0;
         for (DWORD i = 0; i < written; i++) {
            wchar_t ch = buf[i];
            if (ch <= 0x7F)
               count += 1;
            else if (ch <= 0x07FF)
               count += 2;
            else if (ch >= 0xD800 && ch <= 0xDFFF)
               count += 2;   // 2 utf8 bytes per half: a full pair is 4
            else
               count += 3;
         }
         return done + count;
      }
      if (convertOk)
         return len;
      p += done;
      len -= done;
   }
#endif
   while( len > 0 )
   {
      POSIX_LABEL(file_write_again);
      int d = (int)fwrite(&s[p],1,len,f->io);
      if( d <= 0 )
      {
         HANDLE_FINTR(f->io,file_write_again);
         zone.locked = false;
         file_error("file_write",f->name);
      }
      p += d;
      len -= d;
   }
   return n;
}

/**
   file_read : 'file -> s:string -> p:int -> l:int -> int
   <doc>
   Read up to [l] chars into the string [s] starting at position [p].
   Returns the number of chars readed which is > 0 (or 0 if l == 0).
   </doc>
**/
int _hx_std_file_read( Dynamic handle, Array<unsigned char> buf, int p, int n )
{
   fio *f = getFio(handle);

   int buf_len = buf->length;
   int len = n;
   if( p < 0 || len < 0 || p > buf_len || len > buf_len - p )
      return 0;
   // Reading nothing changes nothing: &buf[0] would grow an empty buffer, and
   //  clearing aheadAtEnd below would report the end of a read-ahead file early
   if( len == 0 )
      return 0;

   AheadLock lock(f);
   // Attempt to increase the chances of pinning on the stack...
   unsigned char *bufPtr = &buf[0];
   int unread = lock.unread();
   if (unread > 0)
   {
      int take = unread < len ? unread : len;
      memcpy(bufPtr + p, f->ahead + f->aheadPos, take);
      f->aheadPos += take;
      p += take;
      len -= take;
      if (len == 0)
         return n;
   }
   f->aheadAtEnd = false;

   hx::EnterGCFreeZone();
   while( len > 0 )
   {
      POSIX_LABEL(file_read_again);
      int d = (int)fread(bufPtr + p,1,len,f->io);
      if( d <= 0 )
      {
         int size = n - len;
         HANDLE_FINTR(f->io,file_read_again);
         if( size == 0 )
            file_error("file_read",f->name);
         hx::ExitGCFreeZone();
         return size;
      }
      p += d;
      len -= d;
   }
   hx::ExitGCFreeZone();
   return n;
}

/**
   file_write_char : 'file -> c:int -> void
   <doc>Write the char [c]. Error if [c] outside of the range 0..255</doc>
**/
void _hx_std_file_write_char( Dynamic handle, int c )
{
   fio *f = getFio(handle);
   if( c < 0 || c > 255 )
      return;
   char cc = (char)c;

   hx::EnterGCFreeZone();
   POSIX_LABEL(write_char_again);
   if( fwrite(&cc,1,1,f->io) != 1 )
   {
      HANDLE_FINTR(f->io,write_char_again);
      file_error("file_write_char",f->name);
   }
   hx::ExitGCFreeZone();
}

/**
   file_read_char : 'file -> int
   <doc>Read a char from the file. Exception on error</doc>
**/
int _hx_std_file_read_char( Dynamic handle )
{
   fio *f = getFio(handle);

   if (f->readAhead)
   {
      AheadLock lock(f);
      if (f->aheadPos < f->aheadLen)
         return f->ahead[f->aheadPos++];
      if (!f->ahead)
         f->ahead = (unsigned char *)malloc(READ_AHEAD);
      hx::EnterGCFreeZone();
      POSIX_LABEL(read_ahead_again);
      int got = f->ahead ? (int)fread(f->ahead,1,READ_AHEAD,f->io) : 0;
      if( got <= 0 )
      {
         HANDLE_FINTR(f->io,read_ahead_again);
         f->aheadAtEnd = false;
         file_error("file_read_char",f->name);
      }
      hx::ExitGCFreeZone();
      f->aheadPos = 1;
      f->aheadLen = got;
      f->aheadAtEnd = got < READ_AHEAD;
      return f->ahead[0];
   }

   unsigned char cc = 0;
   hx::EnterGCFreeZone();
   POSIX_LABEL(read_char_again);
   if( fread(&cc,1,1,f->io) != 1 )
   {
      HANDLE_FINTR(f->io,read_char_again);
      file_error("file_read_char",f->name);
   }
   hx::ExitGCFreeZone();
   return cc;
}

/**
   file_seek : 'file -> pos:int -> mode:int -> void
   <doc>Use [fseek] to move the file pointer.</doc>
**/
void _hx_std_file_seek( Dynamic handle, int pos, int kind )
{
   fio *f = getFio(handle);
   AheadLock lock(f);
   // The stream is past the bytes read ahead, so a relative seek takes them
   // back too: in 64 bits, as an int wrapped a large backward seek into a
   // forward one
   long long offset = pos;
   if (kind == SEEK_CUR)
      offset -= lock.unread();
   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   int failed = _fseeki64(f->io,offset,kind);
   #else
   int failed = (off_t)offset != offset || fseeko(f->io,(off_t)offset,kind);
   #endif
   // A failed seek leaves the stream, and so the bytes read ahead, as they were
   if( failed )
      file_error("file_seek",f->name);
   hx::ExitGCFreeZone();
   f->aheadPos = f->aheadLen = 0;
   f->aheadAtEnd = false;
}

/**
   file_tell : 'file -> int
   <doc>Return the current position in the file</doc>
**/
int _hx_std_file_tell( Dynamic handle )
{
   fio *f = getFio(handle);
   AheadLock lock(f);
   hx::EnterGCFreeZone();
   int p = ftell(f->io);
   if( p == -1 )
      file_error("file_tell",f->name);
   hx::ExitGCFreeZone();
   return p - lock.unread();
}

/**
   file_eof : 'file -> bool
   <doc>Tell if we have reached the end of the file</doc>
**/
bool _hx_std_file_eof( Dynamic handle )
{
   fio *f = getFio(handle);
   AheadLock lock(f);
   return lock.unread() == 0 && !f->aheadAtEnd && feof(f->io);
}

/**
   file_flush : 'file -> void
   <doc>Flush the file buffer</doc>
**/
void _hx_std_file_flush( Dynamic handle )
{
   fio *f = getFio(handle);
   hx::EnterGCFreeZone();
   if( fflush( f->io ) != 0 )
      file_error("file_flush",f->name);
   hx::ExitGCFreeZone();
}

/**
   file_contents : f:string -> string
   <doc>Read the content of the file [f] and return it.</doc>
**/

// 64-bit file length - plain ftell is 32-bit on Windows and the int cast
// silently wrapped huge files elsewhere, returning truncated content with
// no error.  Returns -1 on failure.
static long long file_length64(FILE *file)
{
#ifdef NEKO_WINDOWS
   if (_fseeki64(file,0,SEEK_END))
      return -1;
   return _ftelli64(file);
#else
   if (fseeko(file,0,SEEK_END))
      return -1;
   return ftello(file);
#endif
}

// For a size that is zero or unknown, as for /proc files and pipes: read to
// the end. 0 when done, else the error to report
static const char *readToEnd(FILE *inFile, std::vector<char> &outData)
{
   char chunk[16384];
   while(true)
   {
      size_t got = fread(chunk,1,sizeof(chunk),inFile);
      if (got)
      {
         if (outData.size() + got > 0x7ffffff0)
            return "file_too_large";
         outData.insert(outData.end(), chunk, chunk+got);
      }
      if (got < sizeof(chunk))
      {
         if (!ferror(inFile))
            return 0;
         #ifndef NEKO_WINDOWS
         if (errno==EINTR)
         {
            clearerr(inFile);
            continue;
         }
         #endif
         return "file_contents";
      }
   }
}

static String stringFromUtf8(char *inData, int inLength)
{
   // Non-ASCII content still needs the utf16 conversion pass
   #ifdef HX_SMART_STRINGS
   const unsigned char *c = (const unsigned char *)inData;
   for(int i=0;i<inLength;i++)
      if (c[i]>127)
         return _hx_utf8_to_utf16(c, inLength, false);
   #endif
   return String(inData, inLength);
}

String _hx_std_file_contents_string( String name )
{
   hx::strbuf buf;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   FILE *file = _wfopen(name.wchar_str(&buf), L"rb");
#else
   hx::EnterGCFreeZone();
   FILE *file = hxFopen(name.utf8_str(&buf), "rb");
#endif
   if(!file)
      file_error("file_contents",name);

   long long len64 = file_length64(file);
   if (len64>0x7ffffff0)
   {
      fclose(file);
      file_error("file_too_large",name);
   }
   if (len64<=0)
   {
      std::vector<char> data;
      const char *err = readToEnd(file, data);
      fclose(file);
      if (err)
         file_error(err,name);
      hx::ExitGCFreeZone();
      if (data.empty())
         return String::emptyString;
      char *dest = hx::NewString((int)data.size());
      memcpy(dest, &data[0], data.size());
      return stringFromUtf8(dest, (int)data.size());
   }
   int len = (int)len64;

   fseek(file,0,SEEK_SET);
   // Read straight into the GC string buffer - the old std::vector staging
   // copy doubled peak memory and added a full extra memcpy
   hx::ExitGCFreeZone();
   char *dest = hx::NewString(len);
   hx::EnterGCFreeZone();
   int total = len;
   int p = 0;
   while( len > 0 )
   {
      POSIX_LABEL(file_contents);
      int d = (int)fread(dest+p,1,len,file);
      if( d <= 0 )
      {
         HANDLE_FINTR(file,file_contents);
         // The size was only an estimate: keep what was there
         if (feof(file))
            break;
         fclose(file);
         file_error("file_contents",name);
      }
      p += d;
      len -= d;
   }
   fclose(file);
   hx::ExitGCFreeZone();
   if (p < total)
   {
      dest[p] = 0;
      total = p;
   }

   // dest is already a GC string buffer - wrap it without re-copying
   return stringFromUtf8(dest, total);
}



/**
   file_contents : f:string -> string
   <doc>Read the content of the file [f] and return it.</doc>
**/
Array<unsigned char> _hx_std_file_contents_bytes( String name )
{
   hx::strbuf buf;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   FILE *file = _wfopen(name.wchar_str(&buf), L"rb");
#else
   hx::EnterGCFreeZone();
   FILE *file = hxFopen(name.utf8_str(&buf), "rb");
#endif
   if(!file)
      file_error("file_contents",name);

   long long len64 = file_length64(file);
   if (len64>0x7ffffff0)
   {
      fclose(file);
      file_error("file_too_large",name);
   }
   if (len64<=0)
   {
      std::vector<char> data;
      const char *err = readToEnd(file, data);
      fclose(file);
      if (err)
         file_error(err,name);
      hx::ExitGCFreeZone();
      Array<unsigned char> buffer = Array_obj<unsigned char>::__new((int)data.size(),(int)data.size());
      if (!data.empty())
         memcpy(&buffer[0], &data[0], data.size());
      return buffer;
   }
   int len = (int)len64;

   fseek(file,0,SEEK_SET);
   hx::ExitGCFreeZone();

   Array<unsigned char> buffer = Array_obj<unsigned char>::__new(len,len);
   hx::EnterGCFreeZone();
   if (len)
   {
      char *dest = (char *)&buffer[0];

      int p = 0;
      while( len > 0 )
      {
         POSIX_LABEL(file_contents1);
         int d = (int)fread(dest + p,1,len,file);
         if( d <= 0 )
         {
            HANDLE_FINTR(file,file_contents1);
            if (feof(file))
               break;
            fclose(file);
            file_error("file_contents",name);
         }
         p += d;
         len -= d;
      }
      if (len > 0)
      {
         fclose(file);
         hx::ExitGCFreeZone();
         buffer->__SetSize(p);
         return buffer;
      }
   }
   fclose(file);
   hx::ExitGCFreeZone();
   return buffer;
}



Dynamic _hx_std_file_stdin()
{
   fio *f = new fio();
   f->create(stdin, HX_CSTRING("stdin"), false);
   return f;
}


Dynamic _hx_std_file_stdout()
{
   fio *f = new fio();
   f->create(stdout, HX_CSTRING("stdout"), false);
   return f;
}

Dynamic _hx_std_file_stderr()
{
   fio *f = new fio();
   f->create(stderr, HX_CSTRING("stderr"), false);
   return f;
}

