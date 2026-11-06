#include <hxcpp.h>

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef HX_WINDOWS
   #include <windows.h>
#else
   #include <stdint.h>
   #if defined(__unix__) || defined(__APPLE__)
      #include <unistd.h>
      #include <stdio.h>
      #if (_POSIX_VERSION >= 1)
         #define USE_TIME_R
      #endif
      #if (_POSIX_VERSION >= 199309L)
         #include <sys/time.h>
         #define USE_CLOCK_GETTIME
         #define USE_GETTIMEOFDAY
      #endif
   #endif
   #if defined(__ORBIS__)
      // fill in for a missing localtime_r with localtime_s
      #define localtime_r localtime_s
      #define gmtime_r gmtime_s
   #endif
#endif

#ifdef HX_MACOS
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <CoreServices/CoreServices.h>
#endif

#if defined(IPHONE) || defined(APPLETV)
#include <QuartzCore/QuartzCore.h>
#endif


//#include <hxMacros.h>

double __hxcpp_time_stamp()
{
#ifdef HX_WINDOWS
   static __int64 t0=0;
   static double period=0;
   __int64 now;

   if (QueryPerformanceCounter((LARGE_INTEGER*)&now))
   {
      if (t0==0)
      {
         t0 = now;
         __int64 freq;
         QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
         if (freq!=0)
            period = 1.0/freq;
      }
      if (period!=0)
         return (now-t0)*period;
   }

   return (double)clock() / ( (double)CLOCKS_PER_SEC);
#elif defined(HX_MACOS)
   static double time_scale = 0.0;
   if (time_scale==0.0)
   {
      mach_timebase_info_data_t info;
      mach_timebase_info(&info);
      time_scale = 1e-9 * (double)info.numer / info.denom;
   }
   double r =  mach_absolute_time() * time_scale;
   return mach_absolute_time() * time_scale;
#else
   #if defined(IPHONE) || defined(APPLETV)
      double t = CACurrentMediaTime();
   #elif defined(USE_CLOCK_GETTIME)
      // The collector's pause times and heap sizing take differences of these,
      //  which a wall-clock correction made negative or a minute long when
      //  gettimeofday came first (libhxcpp #347)
      struct timespec ts;
      if( clock_gettime(CLOCK_MONOTONIC, &ts) )
         return 0;
      double t =  ( ts.tv_sec + ((double)ts.tv_nsec)*1e-9  );
   #elif defined(USE_GETTIMEOFDAY)
      struct timeval tv;
      if( gettimeofday(&tv,NULL) )
         return 0;
      double t =  ( tv.tv_sec + ((double)tv.tv_usec) / 1000000.0 );
   #else
      double t = (double)clock() * (1.0 / CLOCKS_PER_SEC);
   #endif
   // Set once, by whichever thread calls first
   static const double start = t;
   return t-start;
#endif
}

/*
 * Calendar arithmetic for any time, for where the C library's has a range.
 *
 * On Windows localtime and gmtime return NULL for a time before 1970 (and
 * after 3000), and mktime fails for one. localtime's NULL was turned into an
 * all-zero struct tm, which strftime then refused as an invalid day of the
 * month, through the CRT's invalid-parameter handler, which ends the
 * process (0xC0000409): Date.fromTime(-1000).toString() killed it. gmtime's
 * NULL was dereferenced outright. A 32-bit time_t covers only 1901 to 2038.
 */
static long long hx_days_from_civil(long long y, long long m, long long d)
{
   y -= m <= 2;
   long long era = (y >= 0 ? y : y - 399) / 400;
   long long yoe = y - era * 400;
   long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
   long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
   return era * 146097 + doe - 719468;
}

static bool hx_time_t_holds(double inSeconds)
{
   return sizeof(time_t) >= 8 || (inSeconds >= -2147483648.0 && inSeconds < 2147483648.0);
}

// A time that is not a number, or whose year a tm cannot hold, gives the
//  fields of the epoch: converting it to whole seconds is undefined
static double hx_field_seconds(double inSeconds)
{
   return fabs(inSeconds) < 6.0e16 ? inSeconds : 0.0;
}

static void hx_civil_from_days(long long z, long long *outY, int *outM, int *outD)
{
   z += 719468;
   long long era = (z >= 0 ? z : z - 146096) / 146097;
   long long doe = z - era * 146097;
   long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
   long long y = yoe + era * 400;
   long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
   long long mp = (5 * doy + 2) / 153;
   long long d = doy - (153 * mp + 2) / 5 + 1;
   long long m = mp + (mp < 10 ? 3 : -9);
   *outY = y + (m <= 2);
   *outM = (int)m;
   *outD = (int)d;
}

// The fields of a UTC time, whatever the time.
static void hx_utc_fields(double inSeconds, struct tm *time)
{
   long long t = (long long)floor(inSeconds);
   long long days = t / 86400;
   long long rem = t % 86400;
   if (rem < 0)
   {
      rem += 86400;
      days--;
   }

   long long y;
   int m, d;
   hx_civil_from_days(days, &y, &m, &d);

   memset(time, 0, sizeof(*time));
   time->tm_year = (int)(y - 1900);
   time->tm_mon = m - 1;
   time->tm_mday = d;
   time->tm_hour = (int)(rem / 3600);
   time->tm_min = (int)(rem % 3600 / 60);
   time->tm_sec = (int)(rem % 60);
   // 1970-01-01 was a Thursday.
   time->tm_wday = (int)(((days % 7) + 11) % 7);
   time->tm_yday = (int)(days - hx_days_from_civil(y, 1, 1));
   time->tm_isdst = 0;
}

#ifdef HX_WINDOWS
/*
 * The local zone's offset from UTC, in seconds, at a UTC time the CRT cannot
 * convert: Windows' own zone rules through SystemTimeToTzSpecificLocalTime
 * for any year a SYSTEMTIME holds (1601 on), and the zone's standard offset
 * before that.
 */
static TIME_ZONE_INFORMATION hx_load_zone()
{
   TIME_ZONE_INFORMATION zone;
   GetTimeZoneInformation(&zone);
   return zone;
}

static long long hx_windows_offset(double inSeconds, int *outIsDst)
{
   // Read once, as the CRT reads its own: Date.fromTime builds a Date for
   // the year 0 first, which comes through here, so this is on the path of
   // every date made from a time.
   static const TIME_ZONE_INFORMATION zone = hx_load_zone();
   long long standard = -(long long)(zone.Bias + zone.StandardBias) * 60;
   *outIsDst = 0;

   struct tm utc;
   hx_utc_fields(inSeconds, &utc);
   if (utc.tm_year + 1900 < 1601)
      return standard;

   SYSTEMTIME utcTime, localTime;
   memset(&utcTime, 0, sizeof(utcTime));
   utcTime.wYear = (WORD)(utc.tm_year + 1900);
   utcTime.wMonth = (WORD)(utc.tm_mon + 1);
   utcTime.wDay = (WORD)utc.tm_mday;
   utcTime.wHour = (WORD)utc.tm_hour;
   utcTime.wMinute = (WORD)utc.tm_min;
   utcTime.wSecond = (WORD)utc.tm_sec;
   if (!SystemTimeToTzSpecificLocalTime(NULL, &utcTime, &localTime))
      return standard;

   long long local = hx_days_from_civil(localTime.wYear, localTime.wMonth, localTime.wDay) * 86400
                   + localTime.wHour * 3600 + localTime.wMinute * 60 + localTime.wSecond;
   long long universal = hx_days_from_civil(utcTime.wYear, utcTime.wMonth, utcTime.wDay) * 86400
                   + utcTime.wHour * 3600 + utcTime.wMinute * 60 + utcTime.wSecond;
   long long offset = local - universal;
   *outIsDst = offset != standard ? 1 : 0;
   return offset;
}
#else
// A time time_t cannot hold is moved 28 years at a time into its range: from
// 1901 to 2099 the calendar repeats every 28 years.
static long long hx_posix_offset(double inSeconds, int *outIsDst)
{
   const double cycle = 10227.0 * 86400.0;
   double seconds = floor(inSeconds);
   if (!hx_time_t_holds(seconds))
   {
      double limit = seconds >= 0 ? 2147483647.0 : -2147483648.0;
      double cycles = ceil(fabs(seconds - limit) / cycle);
      seconds += seconds >= 0 ? -cycles * cycle : cycles * cycle;
   }
   time_t t = (time_t)seconds;
   struct tm local;
   #ifdef USE_TIME_R
   localtime_r(&t, &local);
   #else
   struct tm *result = localtime(&t);
   if (!result)
   {
      *outIsDst = 0;
      return 0;
   }
   local = *result;
   #endif
   *outIsDst = local.tm_isdst > 0 ? 1 : 0;
   long long localSeconds = hx_days_from_civil(local.tm_year + 1900LL, local.tm_mon + 1, local.tm_mday) * 86400
                          + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
   return localSeconds - (long long)t;
}
#endif

static long long hx_zone_offset(double inSeconds, int *outIsDst)
{
   #ifdef HX_WINDOWS
   return hx_windows_offset(inSeconds, outIsDst);
   #else
   return hx_posix_offset(inSeconds, outIsDst);
   #endif
}

// The fields of a local time the C library would not convert.
static void hx_local_fields(double inSeconds, struct tm *time)
{
   int isDst = 0;
   long long offset = hx_zone_offset(inSeconds, &isDst);
   hx_utc_fields(inSeconds + (double)offset, time);
   time->tm_isdst = isDst;
   #if !defined(HX_WINDOWS) && !defined(__SNC__) && !defined(__ORBIS__)
   time->tm_gmtoff = (long)offset;
   #endif
}

// The offset is the one at the time the fields name: a first guess, corrected once.
static double hx_utc_from_local(long long inYear, long long inMonth, int inDay, int inHour, int inMin, int inSeconds)
{
   double naive = (double)(hx_days_from_civil(inYear, inMonth + 1, 1) + (inDay - 1)) * 86400.0
                + inHour * 3600.0 + inMin * 60.0 + inSeconds;
   int isDst = 0;
   double guess = naive - (double)hx_zone_offset(naive, &isDst);
   return naive - (double)hx_zone_offset(guess, &isDst);
}

// The zone the C library's last tzset left, which a cached local time is only
// good for: Sys.putEnv("TZ") or a native extension calling tzset changes it
// (libhxcpp #392). Read from the library's globals, without its lock.
struct hx_zone_state
{
   long offset;
   char names[2][64];
};

static void hx_read_zone(hx_zone_state *outState)
{
   memset(outState, 0, sizeof(*outState));
   #if defined(HX_WINDOWS)
   outState->offset = _timezone;
   for(int i=0;i<2;i++)
      if (_tzname[i])
         strncpy(outState->names[i], _tzname[i], sizeof(outState->names[i])-1);
   #elif !defined(__SNC__) && !defined(__ORBIS__)
   #if defined(__GLIBC__) || defined(__ANDROID__)
   outState->offset = timezone;
   #endif
   for(int i=0;i<2;i++)
      if (tzname[i])
         strncpy(outState->names[i], tzname[i], sizeof(outState->names[i])-1);
   #endif
}

/*
 * for the provided Epoch time, fills the passed struct tm with date_time representation in local time zone
 */
void __internal_localtime(double inSeconds, struct tm* time)
{
   inSeconds = hx_field_seconds(inSeconds);
   // Date field getters (getHours, getMinutes, getFullYear, ...) each call this
   // on the SAME timestamp, so formatting one date would do N localtime() calls.
   // localtime is expensive (timezone lookup, CRT lock on Windows); a 1-entry
   // per-thread cache collapses the repeated calls for one date into one.
   static thread_local double sLastSeconds = 0;
   static thread_local struct tm sLastTm;
   static thread_local hx_zone_state sLastZone;
   static thread_local bool sValid = false;
   hx_zone_state zone;
   hx_read_zone(&zone);
   if (sValid && sLastSeconds==inSeconds && !memcmp(&zone, &sLastZone, sizeof(zone)))
   {
      *time = sLastTm;
      return;
   }
   time_t t = (time_t) floor(inSeconds);
   #ifdef USE_TIME_R
   if (hx_time_t_holds(inSeconds))
      localtime_r(&t, time);
   else
      hx_local_fields(inSeconds, time);
   #else
   struct tm *result = hx_time_t_holds(inSeconds) ? localtime(&t) : 0;
   if (result)
      *time = *result;
   else
      hx_local_fields(inSeconds, time);
   #endif
   sLastSeconds = inSeconds;
   sLastTm = *time;
   // localtime can run tzset itself, so the zone is read again after it
   hx_read_zone(&sLastZone);
   sValid = true;
}

/*
 * for the provided Epoch time, fills the passed struct tm with with date_time representation in UTC
 */
void __internal_gmtime(double inSeconds, struct tm* time)
{
   inSeconds = hx_field_seconds(inSeconds);
   // Same rationale as __internal_localtime: cache the last conversion so the
   // UTC field getters don't each re-run gmtime for one timestamp.
   static thread_local double sLastSeconds = 0;
   static thread_local struct tm sLastTm;
   static thread_local bool sValid = false;
   if (sValid && sLastSeconds==inSeconds)
   {
      *time = sLastTm;
      return;
   }
   time_t t = (time_t) floor(inSeconds);
   #ifdef USE_TIME_R
   if (hx_time_t_holds(inSeconds))
      gmtime_r(&t, time);
   else
      hx_utc_fields(inSeconds, time);
   #else
   struct tm *result = hx_time_t_holds(inSeconds) ? gmtime(&t) : 0;
   if (result)
      *time = *result;
   else
      hx_utc_fields(inSeconds, time);
   #endif
   sLastSeconds = inSeconds;
   sLastTm = *time;
   sValid = true;
}

/*
 * input: takes Y-M-D h:m:s.ms (considers that date parts are in local date_time representation)
 * output: returns UTC time stamp (Epoch), in seconds
 */
double __hxcpp_new_date(int inYear,int inMonth,int inDay,int inHour, int inMin, int inSeconds, int inMilliseconds)
{
   struct tm time;

   time.tm_isdst = -1;
   time.tm_year = inYear - 1900;
   time.tm_mon = inMonth;
   time.tm_mday = inDay;
   time.tm_hour = inHour;
   time.tm_min = inMin;
   time.tm_sec = inSeconds;

   long long y = inYear + (inMonth >= 0 ? inMonth / 12 : -((11 - inMonth) / 12));
   long long m = inMonth - (y - inYear) * 12;
   #ifndef HX_WINDOWS
   // A 32-bit time_t's mktime fails outside 1901 to 2038
   if (sizeof(time_t) < 8 && (y < 1903 || y > 2036))
      return hx_utc_from_local(y, m, inDay, inHour, inMin, inSeconds) + ((double) inMilliseconds * 0.001);
   #endif

   time_t t = mktime(&time);
   #ifdef HX_WINDOWS
   // The Windows CRT's mktime fails before 1970 (and after 3000), and never
   // returns -1 as a real time
   if (t == (time_t)-1)
      return hx_utc_from_local(y, m, inDay, inHour, inMin, inSeconds) + ((double) inMilliseconds * 0.001);
   #endif

   return (t + ((double) inMilliseconds * 0.001));
}

// Used by DateTools.makeUtc
double __hxcpp_utc_date(int inYear,int inMonth,int inDay,int inHour, int inMin, int inSeconds)
{
   // Arithmetic, not mktime and gmtime: those read the fields in the local
   // zone and then took the zone back off, which on Windows failed before
   // 1970: gmtime(-1) is NULL there, and it was dereferenced.
   long long y = inYear + (inMonth >= 0 ? inMonth / 12 : -((11 - inMonth) / 12));
   long long m = inMonth - (y - inYear) * 12;
   return (double)(hx_days_from_civil(y, m + 1, 1) + (inDay - 1)) * 86400.0
        + inHour * 3600.0 + inMin * 60.0 + inSeconds;
}

/*
 * returns hh value (in hh:mm:ss) of date_time representation in local time zone
 */
int __hxcpp_get_hours(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_hour;
}

/*
 * returns mm value (in hh:mm:ss) of date_time representation in local time zone
 */
int __hxcpp_get_minutes(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_min;
}

/*
 * returns ss value (in hh:mm:ss) of date_time representation in local time zone
 */
int __hxcpp_get_seconds(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_sec;
}

/*
 * returns YYYY value (in YYYY-MM-DD) of date_time representation in local time zone
 */
int __hxcpp_get_year(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return (time.tm_year + 1900);
}

/*
 * returns MM value (in YYYY-MM-DD) of date_time representation in local time zone
 */
int __hxcpp_get_month(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_mon;
}

/*
 * returns DD value (in YYYY-MM-DD) of date_time representation in local time zone
 */
int __hxcpp_get_date(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_mday;
}

/*
 * returns week day (as int, Sun=0...Sat=6) of date_time representation in local time zone
 */
int __hxcpp_get_day(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_wday;
}

/*
 * returns hh value (in hh:mm:ss) of date_time representation in UTC
 */
int __hxcpp_get_utc_hours(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_hour;
}

/*
 * returns mm value (in hh:mm:ss) of date_time representation in UTC
 */
int __hxcpp_get_utc_minutes(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_min;
}

/*
 * returns ss value (in hh:mm:ss) of date_time representation in UTC
 */
int __hxcpp_get_utc_seconds(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_sec;
}

/*
 * returns YYYY value (in YYYY-MM-DD) of date_time representation in UTC
 */
int __hxcpp_get_utc_year(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return (time.tm_year + 1900);
}

/*
 * returns MM value (in YYYY-MM-DD) of date_time representation in UTC
 */
int __hxcpp_get_utc_month(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_mon;
}

/*
 * returns DD value (in YYYY-MM-DD) of date_time representation in UTC
 */
int __hxcpp_get_utc_date(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_mday;
}

/*
 * returns week day (as int, Sun=0...Sat=6) of date_time representation in UTC
 */
int __hxcpp_get_utc_day(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return time.tm_wday;
}

/*
 * similar to __hxcpp_new_date but, takes no date parts as input because, it assumes NOW date_time
 */
double __hxcpp_date_now()
{
   #ifdef HX_WINDOWS
   typedef unsigned __int64 uint64_t;
   static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

   SYSTEMTIME  system_time;
   FILETIME    file_time;
   ULARGE_INTEGER ularge;

   GetSystemTime( &system_time );
   SystemTimeToFileTime( &system_time, &file_time );

   ularge.LowPart = file_time.dwLowDateTime;
   ularge.HighPart = file_time.dwHighDateTime;

   // A long is 32 bits on Windows, x64 too, and the seconds pass it in 2038
   return (double)( (__int64) ((ularge.QuadPart - EPOCH) / 10000000L) ) +
          system_time.wMilliseconds*0.001;
   #elif defined(USE_GETTIMEOFDAY)
   struct timeval tv;
   gettimeofday(&tv, 0);
   return (tv.tv_sec + (((double) tv.tv_usec) / (1000 * 1000)));
   #else
   // per-second time resolution. not ideal, but OK given the docs for Date.now
   time_t t;
   struct tm ti;
   time(&t);
   __internal_localtime((double)t, &ti);
   return mktime(&ti);
   #endif
}

/*
 * for the input Epoch time, returns whether the corresponding local time would be in DST
 * 1 : yes, in DST ; 0 : no, not in DST
 */
int __hxcpp_is_dst(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return time.tm_isdst;
}

/*
 * for the input Epoch time, returns the correct timezone offset of local time zone;
 * return value is in seconds e.g. -28800 (that would be -8 hrs)
 */
double __hxcpp_timezone_offset(double inSeconds)
{
   #ifdef HX_WINDOWS
   int isDst = 0;
   return (double)hx_windows_offset(inSeconds, &isDst);
   #else
   struct tm localTime;
   __internal_localtime( inSeconds, &localTime);

   #if defined(__SNC__) || defined(__ORBIS__)
   struct tm gmTime;
   __internal_gmtime(inSeconds, &gmTime );

   return mktime(&localTime) - mktime(&gmTime);
   #else
   return localTime.tm_gmtoff;
   #endif
   #endif
}

String __internal_to_string(struct tm time)
{
   // YYYY-MM-DD hh:mm:ss
   //
   // Formatted by hand. strftime validates the fields, and the Windows CRT
   // answers a value it rejects (a year before 1900, say) through its
   // invalid-parameter handler, which ends the process.
   char buf[100];
   snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
   return String::create(buf);
}

/*
 * string form of a given Epoch time, without milliseconds,
 * as in format [YYYY-MM-DD hh:mm:ss +hhmm] ex: [1997-07-16 19:20:30 +0100].
 */
String __hxcpp_to_utc_string(double inSeconds)
{
   struct tm time;
   __internal_gmtime( inSeconds, &time);
   return __internal_to_string( time);
}

/*
 * string form of a given Epoch time, without milliseconds and timezone offset,
 * as in  format [YYYY-MM-DD hh:mm:ss] ex: [1997-07-16 19:20:30].
 */
String __hxcpp_to_string(double inSeconds)
{
   struct tm time;
   __internal_localtime( inSeconds, &time);
   return __internal_to_string( time);
}

/*
 * input: takes Y-M-D h:m:s.ms (considers that date parts are in UTC date_time representation)
 * output: returns UTC time stamp (Epoch), in seconds
 */
double __hxcpp_from_utc(int inYear,int inMonth,int inDay,int inHour, int inMin, int inSeconds, int inMilliseconds)
{
   struct tm time;

   time.tm_isdst = -1;
   time.tm_year  = inYear - 1900;
   time.tm_mon   = inMonth;
   time.tm_mday  = inDay;
   time.tm_hour  = inHour;
   time.tm_min   = inMin;
   time.tm_sec   = inSeconds;

   time_t z = mktime(&time);
   time_t t = z + __hxcpp_timezone_offset(z);

   struct tm local_tm;
   __internal_localtime( t, &local_tm);

   return (mktime(&local_tm) + ((double) inMilliseconds * 0.001));
}

