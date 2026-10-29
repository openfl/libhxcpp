#include <hxcpp.h>

#if (HXCPP_API_LEVEL>=500)
#include <array>

using namespace cpp::marshal;

bool cpp::encoding::Ascii::isEncoded(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

#if defined(HX_SMART_STRINGS)
    return string.isAsciiEncoded();
#else
    for (auto i = 0; i < string.length; i++)
    {
        if (static_cast<unsigned char>(string.raw_ptr()[i]) > 127)
        {
            return false;
        }
    }

    return true;
#endif
}

int64_t cpp::encoding::Ascii::encode(const String& string, View<uint8_t> buffer)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (!isEncoded(string))
    {
        hx::Throw(HX_CSTRING("String cannot be encoded to ASCII"));
    }

    auto src = cpp::marshal::View<char>(string.raw_ptr(), string.length).reinterpret<uint8_t>();

    if (src.tryCopyTo(buffer))
    {
        return src.length;
    }
    else
    {
        return hx::Throw(HX_CSTRING("Buffer too small"));
    }
}

String cpp::encoding::Ascii::decode(View<uint8_t> view)
{
    // Consistent with the Utf8/Utf16 decoders - empty input is ""
    if (view.isEmpty())
    {
        return String::emptyString;
    }

    // The view length is authoritative: scanning for a NUL silently
    // truncated binary-ish payloads ("a\0b" decoded as "a"), and Haxe
    // strings legally contain NULs.  Utf8::decode hands every all-ASCII
    // buffer to this function, so the scan also made Utf8::decode stop at
    // a NUL only when the rest of the buffer happened to be ASCII.
    auto bytes = view.length;

    auto backing = hx::NewGCPrivate(0, bytes + sizeof(char));

    std::memcpy(backing, view.ptr.ptr, bytes);
    // NewGCPrivate does not zero - the terminator must be written
    static_cast<char*>(backing)[bytes] = 0;

    return String(static_cast<const char*>(backing), static_cast<int>(bytes));
}

namespace
{
    bool isAsciiUtf8Buffer(const View<uint8_t>& buffer)
    {
        for (int64_t i = 0; i < buffer.length; i++)
        {
            if (buffer[i] > 127)
            {
                return false;
            }
        }
        return true;
    }

    // UTF-16 units in valid UTF-8: one for each byte that is not a
    // continuation byte (10xxxxxx), and a second for each four-byte lead
    // (11110xxx). Bit 7 of each byte holds the test, so eight bytes go at once
    int64_t utf16Units(const View<uint8_t>& buffer)
    {
        const uint64_t high = 0x8080808080808080ull;
        const uint64_t ones = 0x0101010101010101ull;
        auto bytes = buffer.ptr.ptr;
        auto units = int64_t{ 0 };
        auto i     = int64_t{ 0 };

        for (; i + 8 <= buffer.length; i += 8)
        {
            uint64_t w;
            std::memcpy(&w, bytes + i, sizeof(w));
            auto continuation = w & ~(w << 1) & high;
            auto fourByteLead = w & (w << 1) & (w << 2) & (w << 3) & high;
            units += 8 - static_cast<int64_t>(((continuation >> 7) * ones) >> 56);
            units += static_cast<int64_t>(((fourByteLead >> 7) * ones) >> 56);
        }
        for (; i < buffer.length; i++)
        {
            units += (bytes[i] & 0xC0) != 0x80;
            units += bytes[i] >= 0xF0;
        }
        return units;
    }

#if defined(HX_SMART_STRINGS)
    // The code point at units[i] of a String, advancing i past it. A String
    // may hold a lone surrogate, as substr leaves one when it splits a pair,
    // and it becomes U+FFFD, as Bytes.ofString and utf8_str() give with Haxe 4
    // and as the other targets give. Utf16::codepoint, which reads UTF-16
    // bytes rather than a String, still throws
    char32_t stringCodepoint(const char16_t* units, int64_t length, int64_t& i)
    {
        auto first = static_cast<char32_t>(units[i++]);

        if (first < 0xD800 || first >= 0xE000)
        {
            return first;
        }

        if (first < 0xDC00 && i < length && units[i] >= 0xDC00 && units[i] < 0xE000)
        {
            auto second = static_cast<char32_t>(units[i++]);

            return (((first - 0xD800) << 10) | (second - 0xDC00)) + 0x10000;
        }

        return 0xFFFD;
    }
#endif
}

int cpp::encoding::Utf8::getByteCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf8::getByteCount(char32_t codepoint)
{
    if (codepoint <= 0x7F)
    {
        return 1;
    }
    else if (codepoint <= 0x7FF)
    {
        return 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        return 3;
    }
    else
    {
        return 4;
    }
}

int64_t cpp::encoding::Utf8::getByteCount(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (string.isAsciiEncoded())
    {
        return string.length;
    }

#if defined(HX_SMART_STRINGS)
    auto units = string.raw_wptr();
    auto bytes = int64_t{ 0 };
    auto i     = int64_t{ 0 };

    while (i < string.length)
    {
        bytes += getByteCount(stringCodepoint(units, string.length, i));
    }

    return bytes;
#else
    return hx::Throw(HX_CSTRING("Unexpected encoding error"));
#endif
}

int cpp::encoding::Utf8::getCharCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf8::getCharCount(char32_t codepoint)
{
    return getByteCount(codepoint) / sizeof(char);
}

int64_t cpp::encoding::Utf8::getCharCount(const String& string)
{
    return getByteCount(string) / sizeof(char);
}

int cpp::encoding::Utf8::encode(const null&, const cpp::marshal::View<uint8_t>& buffer)
{
    hx::NullReference("String", false);
    return 0;
}

int64_t cpp::encoding::Utf8::encode(const String& string, const cpp::marshal::View<uint8_t>& buffer)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (0 == string.length)
    {
        return 0;
    }

    if (buffer.isEmpty())
    {
        return hx::Throw(HX_CSTRING("Buffer too small"));
    }

    if (string.isAsciiEncoded())
    {
        auto src = cpp::marshal::View<uint8_t>(reinterpret_cast<uint8_t*>(const_cast<char*>(string.raw_ptr())), string.length);

        if (src.tryCopyTo(buffer))
        {
            return src.length;
        }
        else
        {
            return hx::Throw(HX_CSTRING("Buffer too small"));
        }
    }

#if defined(HX_SMART_STRINGS)
    if (getByteCount(string) > buffer.length)
    {
        hx::Throw(HX_CSTRING("Buffer too small"));
    }

    auto units = string.raw_wptr();
    auto i     = int64_t{ 0 };
    auto k     = int64_t{ 0 };

    while (i < string.length)
    {
        k += encode(stringCodepoint(units, string.length, i), buffer.slice(k));
    }

    return k;
#else
    return hx::Throw(HX_CSTRING("Unexpected encoding error"));
#endif
}

Array<uint8_t> cpp::encoding::Utf8::encode(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (0 == string.length)
    {
        return 0;
    }

    if (string.isAsciiEncoded())
    {
        Array<uint8_t> out(string.length, 0);

        View<uint8_t> src(reinterpret_cast<uint8_t*>(const_cast<char*>(string.raw_ptr())), string.length);
        View<uint8_t> buffer(out->Pointer(), out->length);

        src.copyTo(buffer);

        return out;
    }

#if defined(HX_SMART_STRINGS)
    // estimate the utf8 length with an upper bound
    Array<uint8_t> out(string.length * 3, 0);
    View<uint8_t> buffer(out->Pointer(), out->length);

    auto units = string.raw_wptr();
    auto i     = int64_t{ 0 };
    auto k     = int64_t{ 0 };

    while (i < string.length)
    {
        k += encode(stringCodepoint(units, string.length, i), buffer.slice(k));
    }

    out->resize(k);

    return out;
#else
    return hx::Throw(HX_CSTRING("Unexpected encoding error"));
#endif
}

int cpp::encoding::Utf8::encode(char32_t codepoint, const cpp::marshal::View<uint8_t>& buffer)
{
    if (codepoint <= 0x7F)
    {
        buffer[0] = static_cast<uint8_t>(codepoint);

        return 1;
    }
    else if (codepoint <= 0x7FF)
    {
        auto data = std::array<uint8_t, 2>
        { {
            static_cast<uint8_t>(0xC0 | (codepoint >> 6)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };
        auto src = View<uint8_t>(data.data(), data.size());

        src.copyTo(buffer);

        return data.size();
    }
    else if (codepoint <= 0xFFFF)
    {
        auto data = std::array<uint8_t, 3>
        { {
            static_cast<uint8_t>(0xE0 | (codepoint >> 12)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 63)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };

        auto src = View<uint8_t>(data.data(), data.size());

        src.copyTo(buffer);

        return data.size();
    }
    else
    {
        auto data = std::array<uint8_t, 4>
        { {
            static_cast<uint8_t>(0xF0 | (codepoint >> 18)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 63)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 63)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };

        auto src = View<uint8_t>(data.data(), data.size());

        src.copyTo(buffer);

        return data.size();
    }
}

String cpp::encoding::Utf8::decode(const cpp::marshal::View<uint8_t>& buffer)
{
    if (buffer.isEmpty())
    {
        return String::emptyString;
    }

    if (isAsciiUtf8Buffer(buffer))
    {
        return Ascii::decode(buffer);
    }
#if defined(HX_SMART_STRINGS)
    auto bytes  = utf16Units(buffer) * sizeof(char16_t);
    auto exact  = static_cast<char16_t*>(::hx::InternalNew(bytes + sizeof(char16_t), false));
    auto output = View<uint8_t>(reinterpret_cast<uint8_t*>(exact), bytes);
    int64_t k   = 0;
    int64_t i   = 0;

    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        i += getByteCount(p);
        k += Utf16::encode(p, output.slice(k));
    }

    auto chars = static_cast<int>(k / sizeof(char16_t));

    ((unsigned int*)exact)[-1] |= HX_GC_STRING_CHAR16_T;
    exact[chars] = 0;

    return String(exact, chars);
#else
    // The bytes are kept as they are, but malformed input still throws, as
    // it does with smart strings. codepoint takes an encoded surrogate
    // (ED A0 80 to ED BF BF), which Utf16::encode rejects there, so it is
    // rejected here too
    for (int64_t i = 0; i < buffer.length;)
    {
        auto p = codepoint(buffer.slice(i));

        if (0xD800 <= p && p < 0xE000)
        {
            hx::Throw(HX_CSTRING("Failed to read codepoint"));
        }

        i += getByteCount(p);
    }

    // +1 for the terminator - the allocation is not zeroed and hxcpp
    // strings are assumed NUL-terminated by native consumers
    auto backing = View<char>(hx::InternalNew(buffer.length + 1, false), buffer.length + 1);

    std::memcpy(backing.ptr.ptr, buffer.ptr.ptr, buffer.length);
    backing.ptr.ptr[buffer.length] = 0;

    return String(backing.ptr.ptr, static_cast<int>(buffer.length));
#endif
}

char32_t cpp::encoding::Utf8::codepoint(const cpp::marshal::View<uint8_t>& buffer)
{
    // Strict decoding.  Unvalidated continuation bytes silently produced
    // mojibake from malformed input (and consumed a valid following
    // character), and accepting overlong forms desynchronized the decode
    // loops, which advance by the re-encoded length of the value
    auto b0 = static_cast<char32_t>(buffer[0]);

    if ((b0 & 0x80) == 0)
    {
        return b0;
    }
    else if ((b0 & 0xE0) == 0xC0)
    {
        auto b1 = static_cast<char32_t>(buffer.slice(1)[0]);
        if ((b1 & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p = (static_cast<char32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F);
        if (p < 0x80) // overlong
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        auto staging = std::array<uint8_t, 2>();
        auto dst     = View<uint8_t>(staging.data(), staging.size());

        buffer.slice(1, staging.size()).copyTo(dst);

        if ((staging[0] & 0xC0) != 0x80 || (staging[1] & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p = (static_cast<char32_t>(b0 & 0x0F) << 12) | (static_cast<char32_t>(staging[0] & 0x3F) << 6) | static_cast<char32_t>(staging[1] & 0x3F);
        if (p < 0x800) // overlong
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        auto staging = std::array<uint8_t, 3>();
        auto dst     = View<uint8_t>(staging.data(), staging.size());

        buffer.slice(1, staging.size()).copyTo(dst);

        if ((staging[0] & 0xC0) != 0x80 || (staging[1] & 0xC0) != 0x80 || (staging[2] & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p =
            (static_cast<char32_t>(b0 & 0x07) << 18) |
            (static_cast<char32_t>(staging[0] & 0x3F) << 12) |
            (static_cast<char32_t>(staging[1] & 0x3F) << 6) |
            static_cast<char32_t>(staging[2] & 0x3F);
        // Overlong or beyond U+10FFFF (out-of-range values leaked
        // uninitialized memory into the decoded String via the silent
        // Utf16::encode failure)
        if (p < 0x10000 || p > 0x10FFFF)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else
    {
        return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
    }
}

namespace
{
    String toAsciiString(const View<uint8_t>& buffer)
    {
        auto bytes  = buffer.length / sizeof(char16_t);
        auto chars  = View<char>(hx::InternalNew(bytes + 1, false), bytes * sizeof(char));
        auto i      = int64_t{ 0 };
        auto k      = int64_t{ 0 };

        while (i < buffer.length)
        {
            auto p = cpp::encoding::Utf16::codepoint(buffer.slice(i));

            chars[k++] = static_cast<char>(p);

            i += cpp::encoding::Utf16::getByteCount(p);
        }

        // The allocation is not zeroed - the terminator must be written
        chars.ptr.ptr[k] = 0;

        return String(chars.ptr.ptr, chars.length);
    }

#if !defined(HX_SMART_STRINGS)
    View<uint8_t> utf8View(const String& string)
    {
        return View<uint8_t>(reinterpret_cast<uint8_t*>(const_cast<char*>(string.raw_ptr())), string.length);
    }
#endif
}

bool cpp::encoding::Utf16::isEncoded(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    return string.isUTF16Encoded();
}

int cpp::encoding::Utf16::getByteCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf16::getByteCount(char32_t codepoint)
{
    return codepoint <= 0xFFFF ? 2 : 4;
}

int64_t cpp::encoding::Utf16::getByteCount(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (string.isUTF16Encoded())
    {
        return string.length * sizeof(char16_t);
    }
    else
    {
        auto bytes = int64_t{ 0 };
#if defined(HX_SMART_STRINGS)
        for (auto i = 0; i < string.length; i++)
        {
            // Through unsigned char: raw_ptr() is signed, so a byte >= 0x80
            // sign-extended to a huge "codepoint" and miscounted as 4 bytes
            bytes += getByteCount(static_cast<char32_t>(static_cast<unsigned char>(string.raw_ptr()[i])));
        }
#else
        auto source = utf8View(string);
        auto i      = int64_t{ 0 };

        while (i < source.length)
        {
            auto p = Utf8::codepoint(source.slice(i));

            i     += Utf8::getByteCount(p);
            bytes += getByteCount(p);
        }
#endif

        return bytes;
    }
}

int cpp::encoding::Utf16::getCharCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf16::getCharCount(char32_t codepoint)
{
    return getByteCount(codepoint) / sizeof(char16_t);
}

int64_t cpp::encoding::Utf16::getCharCount(const String& string)
{
    return getByteCount(string) / sizeof(char16_t);
}

int cpp::encoding::Utf16::encode(const null&, const cpp::marshal::View<uint8_t>& buffer)
{
    hx::NullReference("String", false);
    return 0;
}

int64_t cpp::encoding::Utf16::encode(const String& string, const cpp::marshal::View<uint8_t>& buffer)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (0 == string.length)
    {
        return 0;
    }

    if (buffer.isEmpty())
    {
        return hx::Throw(HX_CSTRING("Buffer too small"));
    }

#if defined(HX_SMART_STRINGS)
    if (string.isUTF16Encoded())
    {
        auto src = cpp::marshal::View<uint8_t>(reinterpret_cast<uint8_t*>(const_cast<char16_t*>(string.raw_wptr())), string.length * sizeof(char16_t));

        if (src.tryCopyTo(buffer))
        {
            return src.length;
        }
        else
        {
            return hx::Throw(HX_CSTRING("Buffer too small"));
        }
    }
    else
#endif
    {
        auto bytes = getByteCount(string);

        if (bytes > buffer.length)
        {
            return hx::Throw(HX_CSTRING("Buffer too small"));
        }

        auto i = int64_t{ 0 };
#if defined(HX_SMART_STRINGS)
        for (auto k = 0; k < string.length; k++)
        {
            // See getByteCount - avoid signed-char sign extension
            i += encode(static_cast<char32_t>(static_cast<unsigned char>(string.raw_ptr()[k])), buffer.slice(i));
        }
#else
        auto source = utf8View(string);
        auto k      = int64_t{ 0 };

        while (k < source.length)
        {
            auto p = Utf8::codepoint(source.slice(k));

            k += Utf8::getByteCount(p);
            i += encode(p, buffer.slice(i));
        }
#endif

        return bytes;
    }
}

int cpp::encoding::Utf16::encode(char32_t codepoint, const cpp::marshal::View<uint8_t>& buffer)
{
    if (codepoint < 0xD800)
    {
        Marshal::writeUInt16(buffer, static_cast<char16_t>(codepoint));

        return 2;
    }
    else if (codepoint < 0xE000)
    {
        // D800 - DFFF is invalid

        return hx::Throw(HX_CSTRING("Invalid UTF16"));
    }
    else if (codepoint < 0x10000)
    {
        Marshal::writeUInt16(buffer, static_cast<char16_t>(codepoint));

        return 2;
    }
    else if (codepoint < 0x110000)
    {
        auto staging = std::array<uint8_t, 4>();
        auto fst     = View<uint8_t>(staging.data(), 2);
        auto snd     = View<uint8_t>(staging.data() + 2, 2);
        auto all     = View<uint8_t>(staging.data(), staging.size());

        Marshal::writeUInt16(fst, 0xD800 + (((codepoint - 0x10000) >> 10) & 0x3FF));
        Marshal::writeUInt16(snd, 0xDC00 + ((codepoint - 0x10000) & 0x3FF));

        all.copyTo(buffer);

        return 4;
    }

    // Beyond U+10FFFF is not representable - the silent return 0 left the
    // caller's buffer tail unwritten (uninitialized memory in the decoded
    // String) while counts assumed it was filled
    return hx::Throw(HX_CSTRING("Invalid codepoint"));
}

String cpp::encoding::Utf16::decode(const cpp::marshal::View<uint8_t>& buffer)
{
    if (buffer.isEmpty())
    {
        return String::emptyString;
    }

#if defined(HX_SMART_STRINGS)
    auto i = int64_t{ 0 };
    bool isAscii = true;
    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        if (p > 127)
        {
            isAscii = false;
        }

        i += getByteCount(p);
    }

    if (isAscii)
    {
        return toAsciiString(buffer);
    }

    auto chars   = i / sizeof(char16_t);
    auto backing = View<char16_t>(::String::allocChar16Ptr(chars), chars);
    auto output  = backing.reinterpret<uint8_t>();
    auto k       = int64_t{ 0 };

    i = 0;
    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        i += getByteCount(p);
        k += encode(p, output.slice(k));
    }

    return String(backing.ptr.ptr, chars);
#else
    auto i     = int64_t{ 0 };
    auto bytes = int64_t{ 0 };

    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        if (0xD800 <= p && p < 0xE000)
        {
            return hx::Throw(HX_CSTRING("Invalid UTF16"));
        }

        i     += getByteCount(p);
        bytes += Utf8::getByteCount(p);
    }

    auto backing = View<uint8_t>(static_cast<uint8_t*>(hx::InternalNew(bytes + 1, false)), bytes + 1);
    auto k       = int64_t{ 0 };

    i = 0;
    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        i += getByteCount(p);
        k += Utf8::encode(p, backing.slice(k));
    }

    backing.ptr.ptr[bytes] = 0;

    return String(reinterpret_cast<char*>(backing.ptr.ptr), static_cast<int>(bytes));
#endif
}

char32_t cpp::encoding::Utf16::codepoint(const cpp::marshal::View<uint8_t>& buffer)
{
    auto first = static_cast<char16_t>(Marshal::readUInt16(buffer));

    if (0xD800 <= first && first < 0xDc00)
    {
        // A lone trailing high surrogate (trivially produced by substr
        // splitting a pair) used to surface as a confusing internal
        // "View too small" from the read past the end
        if (buffer.length < 4)
        {
            return int{ hx::Throw(HX_CSTRING("Invalid UTF16")) };
        }
        auto second = static_cast<char16_t>(Marshal::readUInt16(buffer.slice(2)));
        if (0xDC00 <= second && second < 0xE000)
        {
            return static_cast<char32_t>((((first - 0xD800) << 10) | (second - 0xDC00)) + 0x10000);
        }

        return int{ hx::Throw(HX_CSTRING("Invalid UTF16")) };
    }
    else
    {
        return static_cast<char32_t>(first);
    }
}

#endif
