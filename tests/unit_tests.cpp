// FIGHTO - headless unit tests (no window, GL context or audio device needed).
//
// Built as the fighto_tests target (see the block in CMakeLists.txt):
//   cmake -S . -B build-tests -G Ninja && ninja -C build-tests fighto_tests
//   ./build-tests/fighto_tests           run every test
//   ./build-tests/fighto_tests save_     run only the tests whose name contains "save_"
//
// Prints one PASS / FAIL line per test followed by a summary, and exits non-zero
// when any test fails. Tests registered with a known-bug description document a
// defect that is still present in the code: they print XFAIL (and do not fail the
// run) while the bug reproduces, and XPASS once it has been fixed so they can be
// promoted to regular tests.
#include "../src/game/game.h"
#include "../src/game/script.h"
#include "../src/core/noise.h"
#include "../src/platform/platform_desktop.h"
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// ===========================================================================
// Test-only stubs for game symbols referenced by src/game/save.cpp. The real
// definitions live in game.cpp, weapons.cpp and world_landmarks.cpp, which pull
// in the renderer, audio and world generation and are not linked into this binary.
// ===========================================================================
Game g;

// Only clipSize / maxReserve are read by save_deserialize; the values mirror weapons.cpp.
const WeaponDef g_weapons[W_COUNT] = {
    {"Fists", 0, 0},          {"Kestrel P9", 15, 150},  {"Breacher 12", 6, 48},
    {"Hornet SMG", 30, 300},  {"Ravager AR", 30, 240},  {"Gravedigger .50", 6, 48},
    {"A.E.G.I.S. Arc Carbine", 40, 400}, {"Void Lance", 8, 64}, {"Shard Thrower", 18, 180},
    {"Claws", 0, 0},          {"Plasma Emitter", 60, 9999},
};

static const vec3 STUB_LANDMARK_POS(-12.5f, 3.0f, 45.25f);
static int g_stubLandmarkId = -1;
vec3 landmark_pos(int id) {
    g_stubLandmarkId = id;
    return STUB_LANDMARK_POS;
}

// ===========================================================================
// Minimal test harness
// ===========================================================================
static int g_checkCount;   // every check run
static int g_checkFails;   // failed checks in the current test
static int g_reportCount;  // failure lines printed for the current test

static void out(const char* f, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    fmt_strv(buf, (int)sizeof(buf), f, ap);
    va_end(ap);
    fputs(buf, stdout);
}

static void report(const char* f, ...) {
    if (g_reportCount >= 12) {
        if (g_reportCount == 12) fputs("    ... further failed checks in this test not shown\n", stdout);
        g_reportCount++;
        return;
    }
    g_reportCount++;
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    fmt_strv(buf, (int)sizeof(buf), f, ap);
    va_end(ap);
    fputs(buf, stdout);
}

static void num_str(char* buf, int cap, double v) {
    if (v != v) str_copy(buf, cap, "nan");
    else if (v > 1e15) str_copy(buf, cap, "+huge");
    else if (v < -1e15) str_copy(buf, cap, "-huge");
    else fmt_str(buf, cap, "%.7f", v);
}

static bool check_true(bool ok, const char* expr, int line) {
    g_checkCount++;
    if (ok) return true;
    g_checkFails++;
    report("    check failed at line %d: %s\n", line, expr);
    return false;
}

static bool check_int(i64 a, i64 b, const char* ea, const char* eb, int line) {
    g_checkCount++;
    if (a == b) return true;
    g_checkFails++;
    report("    check failed at line %d: %s == %s (got %ld, expected %ld)\n", line, ea, eb, a, b);
    return false;
}

static bool check_hex(u32 a, u32 b, const char* ea, const char* eb, int line) {
    g_checkCount++;
    if (a == b) return true;
    g_checkFails++;
    report("    check failed at line %d: %s == %s (got 0x%08x, expected 0x%08x)\n", line, ea, eb, a, b);
    return false;
}

static bool check_near(double a, double b, double eps, const char* ea, const char* eb, int line) {
    g_checkCount++;
    if (fabs(a - b) <= eps) return true;  // NaN never passes
    g_checkFails++;
    char sa[48], sb[48], se[48];
    num_str(sa, sizeof(sa), a);
    num_str(sb, sizeof(sb), b);
    num_str(se, sizeof(se), eps);
    report("    check failed at line %d: %s ~= %s (got %s, expected %s, eps %s)\n", line, ea, eb, sa, sb, se);
    return false;
}

static bool check_str(const char* a, const char* b, const char* ea, const char* eb, int line) {
    g_checkCount++;
    if (str_eq(a, b)) return true;
    g_checkFails++;
    report("    check failed at line %d: %s == %s (got \"%s\", expected \"%s\")\n", line, ea, eb, a, b);
    return false;
}

static bool check_contains(const char* hay, const char* needle, const char* eh, int line) {
    g_checkCount++;
    if (hay && needle && strstr(hay, needle)) return true;
    g_checkFails++;
    report("    check failed at line %d: %s contains \"%s\" (got \"%s\")\n", line, eh, needle, hay);
    return false;
}

static bool check_vec(vec3 a, vec3 b, float eps, const char* ea, const char* eb, int line) {
    g_checkCount++;
    if (fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps) return true;
    g_checkFails++;
    char ax[48], ay[48], az[48], bx[48], by[48], bz[48];
    num_str(ax, sizeof(ax), a.x); num_str(ay, sizeof(ay), a.y); num_str(az, sizeof(az), a.z);
    num_str(bx, sizeof(bx), b.x); num_str(by, sizeof(by), b.y); num_str(bz, sizeof(bz), b.z);
    report("    check failed at line %d: %s ~= %s (got (%s, %s, %s), expected (%s, %s, %s))\n", line, ea, eb, ax, ay, az, bx, by, bz);
    return false;
}

#define CHECK(c) check_true((c), #c, __LINE__)
#define CHECK_INT(a, b) check_int((i64)(a), (i64)(b), #a, #b, __LINE__)
#define CHECK_HEX(a, b) check_hex((u32)(a), (u32)(b), #a, #b, __LINE__)
#define CHECK_NEAR(a, b, eps) check_near((double)(a), (double)(b), (double)(eps), #a, #b, __LINE__)
#define CHECK_STR(a, b) check_str((a), (b), #a, #b, __LINE__)
#define CHECK_CONTAINS(hay, needle) check_contains((hay), (needle), #hay, __LINE__)
#define CHECK_VEC(a, b, eps) check_vec((a), (b), (eps), #a, #b, __LINE__)

static char g_fb[256];
// Formats into a shared buffer and yields it, for CHECK_STR(FMT(...), "expected").
#define FMT(...) (fmt_str(g_fb, (int)sizeof(g_fb), __VA_ARGS__), g_fb)

// ===========================================================================
// src/core/base
// ===========================================================================
static void test_base_str_copy() {
    char buf[8];
    memset(buf, 'x', sizeof(buf));
    str_copy(buf, 4, "hello");
    CHECK_STR(buf, "hel");
    CHECK(buf[4] == 'x');  // nothing written past cap
    str_copy(buf, (int)sizeof(buf), "1234567");
    CHECK_STR(buf, "1234567");  // exactly fits
    str_copy(buf, (int)sizeof(buf), "12345678");
    CHECK_STR(buf, "1234567");  // one too long: truncated and terminated
    str_copy(buf, 1, "abc");
    CHECK_STR(buf, "");
    buf[0] = 'q';
    str_copy(buf, 0, "abc");
    CHECK(buf[0] == 'q');  // cap 0 writes nothing
    str_copy(buf, (int)sizeof(buf), nullptr);
    CHECK_STR(buf, "");  // null source gives an empty string
    str_copy(buf, (int)sizeof(buf), "");
    CHECK_STR(buf, "");
}

static void test_base_str_eq_starts() {
    CHECK(str_eq("abc", "abc"));
    CHECK(!str_eq("abc", "abd"));
    CHECK(!str_eq("abc", "ab"));
    CHECK(str_eq("", ""));
    CHECK(!str_eq("a", ""));
    CHECK(str_eq(nullptr, nullptr));
    CHECK(!str_eq("a", nullptr));
    CHECK(!str_eq(nullptr, "a"));
    CHECK(str_starts("hello", "he"));
    CHECK(str_starts("hello", "hello"));
    CHECK(str_starts("hello", ""));
    CHECK(!str_starts("he", "hello"));
    CHECK(!str_starts("abc", "abd"));
}

static void test_base_str_to_int() {
    CHECK_INT(str_to_int("42"), 42);
    CHECK_INT(str_to_int("-17"), -17);
    CHECK_INT(str_to_int("+8"), 8);
    CHECK_INT(str_to_int("  \t 99"), 99);
    CHECK_INT(str_to_int("123abc"), 123);
    CHECK_INT(str_to_int("3.9"), 3);
    CHECK_INT(str_to_int("007"), 7);
    CHECK_INT(str_to_int("abc"), 0);
    CHECK_INT(str_to_int(""), 0);
    CHECK_INT(str_to_int("-"), 0);
    CHECK_INT(str_to_int("2147483647"), 2147483647);
    CHECK_INT(str_to_int("-2147483647"), -2147483647);
}

static void test_base_str_to_float() {
    CHECK_NEAR(str_to_float("3.5"), 3.5, 0);
    CHECK_NEAR(str_to_float("-0.25"), -0.25, 0);
    CHECK_NEAR(str_to_float("+2"), 2.0, 0);
    CHECK_NEAR(str_to_float("  \t1e3"), 1000.0, 0);
    CHECK_NEAR(str_to_float("2.5E-2"), 0.025, 1e-7);
    CHECK_NEAR(str_to_float("1.5e+2"), 150.0, 1e-5);
    CHECK_NEAR(str_to_float(".5"), 0.5, 0);
    CHECK_NEAR(str_to_float("7."), 7.0, 0);
    CHECK_NEAR(str_to_float("12.75xyz"), 12.75, 0);
    CHECK_NEAR(str_to_float("0.1"), 0.1f, 1e-7);
    CHECK_NEAR(str_to_float("abc"), 0.0, 0);
    CHECK_NEAR(str_to_float(""), 0.0, 0);
    CHECK_NEAR(str_to_float("-"), 0.0, 0);
}

static void test_base_fmt_integers() {
    CHECK_STR(FMT("%d", 0), "0");
    CHECK_STR(FMT("%d", 123), "123");
    CHECK_STR(FMT("%d", -45), "-45");
    CHECK_STR(FMT("%d", -2147483647 - 1), "-2147483648");
    CHECK_STR(FMT("%i", 7), "7");
    CHECK_STR(FMT("%+d", 5), "+5");
    CHECK_STR(FMT("%u", 4000000000u), "4000000000");
    CHECK_STR(FMT("%x", 0xdeadbeefu), "deadbeef");
    CHECK_STR(FMT("%X", 0xdeadbeefu), "DEADBEEF");
    CHECK_STR(FMT("%x", 0u), "0");
    CHECK_STR(FMT("%ld", (i64)-9000000000LL), "-9000000000");
    CHECK_STR(FMT("%lu", (u64)18446744073709551615ULL), "18446744073709551615");
    CHECK_STR(FMT("%zu", (size_t)42), "42");
    CHECK_STR(FMT("%p", (void*)(uintptr_t)0x1234), "0x1234");
    CHECK_STR(FMT("%c%c", 'O', 'K'), "OK");
    CHECK_STR(FMT("100%%"), "100%");
    CHECK_STR(FMT("a%db%sc", 1, "x"), "a1bxc");
    CHECK_STR(FMT("%q"), "%q");  // unknown conversion is echoed, no argument consumed
}

static void test_base_fmt_strings() {
    CHECK_STR(FMT("%s", "hello"), "hello");
    CHECK_STR(FMT("[%s]", ""), "[]");
    CHECK_STR(FMT("%s", (const char*)nullptr), "(null)");
    CHECK_STR(FMT("%.3s", "abcdef"), "abc");
    CHECK_STR(FMT("%.10s", "abc"), "abc");
    CHECK_STR(FMT("%s-%s", "foo", "bar"), "foo-bar");
}

static void test_base_fmt_floats() {
    CHECK_STR(FMT("%.2f", 3.14159), "3.14");
    CHECK_STR(FMT("%.0f", 2.4), "2");
    CHECK_STR(FMT("%.0f", 2.5), "3");  // halves round away from zero (glibc printf would give "2")
    CHECK_STR(FMT("%.2f", 0.999), "1.00");  // rounding carries into the integer part
    CHECK_STR(FMT("%.2f", 0.05), "0.05");   // fraction keeps its leading zeros
    CHECK_STR(FMT("%.4f", 0.0625), "0.0625");
    CHECK_STR(FMT("%.3f", -1.5), "-1.500");
    CHECK_STR(FMT("%.2f", -0.001), "-0.00");
    CHECK_STR(FMT("%f", 1.5), "1.500000");  // default precision 6
    CHECK_STR(FMT("%.1f", 1000000.0), "1000000.0");
    CHECK_STR(FMT("%.3f", 0.1f), "0.100");  // float promoted to double
    CHECK_STR(FMT("%+.1f", 2.0), "+2.0");
    CHECK_STR(FMT("%f", (double)NAN), "nan");
    CHECK_STR(FMT("%5.1f", 3.14159), "  3.1");
    CHECK_STR(FMT("%06.2f", 3.14159), "003.14");
    CHECK_STR(FMT("%g", 1.5), "1.500");  // this %g is fixed-point with 3 decimals
}

static void test_base_fmt_width_padding() {
    // Field widths are supported (right-aligned, space or '0' padding); '-' is not.
    CHECK_STR(FMT("%d:%02d", 3, 7), "3:07");  // HUD clock format
    CHECK_STR(FMT("%02d", 123), "123");
    CHECK_STR(FMT("%5d", 42), "   42");
    CHECK_STR(FMT("%05d", -42), "-0042");
    CHECK_STR(FMT("%3u", 7u), "  7");
    CHECK_STR(FMT("%08x", 0xbeefu), "0000beef");
    CHECK_STR(FMT("%5s", "ab"), "   ab");
    CHECK_STR(FMT("%-5d|", 42), "%-5d|");  // left-justify flag unsupported: echoed literally
    CHECK_INT(strlen(FMT("%40d", 1)), 31);   // numeric width is capped at 31 characters
}

// Known bug: a negative number with a space-padded width puts the sign before the padding.
static void test_base_fmt_negative_space_padding() {
    CHECK_STR(FMT("%5d", -42), "  -42");
    CHECK_STR(FMT("%6.1f", -2.5), "  -2.5");
}

static void test_base_fmt_truncation_return() {
    char b[8];
    memset(b, 'x', sizeof(b));
    int r = fmt_str(b, 4, "abcdef");
    CHECK_INT(r, 6);  // returns the untruncated length, like snprintf
    CHECK_STR(b, "abc");
    CHECK(b[4] == 'x');  // nothing written past cap
    r = fmt_str(b, 4, "abc");
    CHECK_INT(r, 3);
    CHECK_STR(b, "abc");
    r = fmt_str(b, 1, "abc");
    CHECK_INT(r, 3);
    CHECK_STR(b, "");
    memset(b, 'x', sizeof(b));
    r = fmt_str(b, 0, "abc");
    CHECK_INT(r, 0);  // cap 0: nothing written, returns 0
    CHECK(b[0] == 'x');
    r = fmt_str(b, (int)sizeof(b), "%d", 1234567890);
    CHECK_INT(r, 10);
    CHECK_STR(b, "1234567");
    r = fmt_str(b, (int)sizeof(b), "%s", "abcdefghijkl");
    CHECK_INT(r, 12);
    CHECK_STR(b, "abcdefg");
    r = fmt_str(b, 4, "%.3f", 12.5);
    CHECK_INT(r, 6);
    CHECK_STR(b, "12.");
}

// Known bug: a format string that ends in a lone '%' consumes the terminating NUL
// and keeps reading past the end of the string (src/core/base.cpp:36).
static void test_base_fmt_trailing_percent() {
    // The bytes after the terminator are part of the same array, so the over-read is
    // observable here without undefined behaviour.
    const char fmtbuf[8] = {'5', '0', '%', '\0', 'Z', 'Z', '\0', '\0'};
    char b[16];
    memset(b, 0, sizeof(b));
    int r = fmt_str(b, (int)sizeof(b), fmtbuf);
    CHECK_INT(r, 3);
    CHECK_STR(b, "50%");
    CHECK(memchr(b, 'Z', sizeof(b)) == nullptr);
}

static void test_base_hash_str() {
    // 32-bit FNV-1a reference vectors.
    CHECK_HEX(hash_str(""), 0x811c9dc5u);
    CHECK_HEX(hash_str("a"), 0xe40c292cu);
    CHECK_HEX(hash_str("foobar"), 0xbf9cf968u);
    CHECK(hash_str("thug") != hash_str("thuh"));
    CHECK(hash_str("ab") != hash_str("ba"));
}

static void test_base_crc32() {
    // Standard reflected CRC-32 (IEEE 802.3, poly 0xEDB88320): check value of "123456789".
    CHECK_HEX(crc32("123456789", 9), 0xCBF43926u);
    CHECK_HEX(crc32("", 0), 0u);
    CHECK_HEX(crc32("a", 1), 0xe8b7be43u);
    // The seed chains partial results.
    CHECK_HEX(crc32("6789", 4, crc32("12345", 5)), 0xCBF43926u);
    u8 data[1000];
    Rng r;
    r.seed(7);
    for (int i = 0; i < 1000; i++) data[i] = (u8)r.next();
    u32 whole = crc32(data, sizeof(data));
    u32 chained = 0;
    for (int i = 0; i < 1000; i += 37) chained = crc32(data + i, (size_t)min_t(37, 1000 - i), chained);
    CHECK_HEX(chained, whole);
    // Every single-bit error changes the checksum.
    int undetected = 0;
    for (int i = 0; i < 1000; i++)
        for (int bit = 0; bit < 8; bit++) {
            data[i] ^= (u8)(1u << bit);
            if (crc32(data, sizeof(data)) == whole) undetected++;
            data[i] ^= (u8)(1u << bit);
        }
    CHECK_INT(undetected, 0);
}

static void test_base_array_push_reserve() {
    Array<int> a = {};
    CHECK(a.data == nullptr && a.count == 0 && a.cap == 0);  // zero-initialised == empty
    a.reserve(5);
    CHECK_INT(a.cap, 8);  // first allocation is 8 elements
    a.reserve(9);
    CHECK_INT(a.cap, 16);  // then doubles
    a.reserve(3);
    CHECK_INT(a.cap, 16);  // never shrinks
    for (int i = 0; i < 100; i++) a.push(i * 3);
    CHECK_INT(a.count, 100);
    CHECK_INT(a.cap, 128);
    bool ok = true;
    for (int i = 0; i < 100; i++) ok = ok && a[i] == i * 3;
    CHECK(ok);
    a.push(5) = 6;  // push returns a reference to the stored element
    CHECK_INT(a.last(), 6);
    long sum = 0;
    for (int* it = a.begin(); it != a.end(); ++it) sum += *it;
    CHECK_INT(sum, 3 * 4950 + 6);
    a.free_mem();
}

static void test_base_array_resize() {
    Array<int> a = {};
    a.push(1);
    a.push(2);
    a.push(3);
    a.resize(6);
    CHECK_INT(a.count, 6);
    CHECK(a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 0 && a[4] == 0 && a[5] == 0);
    a.resize(2);
    CHECK_INT(a.count, 2);
    CHECK(a[0] == 1 && a[1] == 2);
    a.resize(5);  // growing again zero-fills, even over the old values
    CHECK(a[2] == 0 && a[3] == 0 && a[4] == 0);
    a.resize(0);
    CHECK_INT(a.count, 0);
    a.resize(1000);
    CHECK_INT(a.count, 1000);
    CHECK(a.cap >= 1000);
    bool zero = true;
    for (int i = 0; i < 1000; i++) zero = zero && a[i] == 0;
    CHECK(zero);
    a.free_mem();
}

static void test_base_array_remove() {
    Array<int> a = {};
    for (int i = 1; i <= 5; i++) a.push(i * 10);  // 10 20 30 40 50
    a.remove_swap(1);                             // last element moves into the hole
    CHECK_INT(a.count, 4);
    CHECK(a[0] == 10 && a[1] == 50 && a[2] == 30 && a[3] == 40);
    a.remove_swap(3);  // removing the last element
    CHECK_INT(a.count, 3);
    CHECK(a[0] == 10 && a[1] == 50 && a[2] == 30);
    a.clear();
    for (int i = 1; i <= 5; i++) a.push(i * 10);
    a.remove_ordered(1);  // order preserved
    CHECK_INT(a.count, 4);
    CHECK(a[0] == 10 && a[1] == 30 && a[2] == 40 && a[3] == 50);
    a.remove_ordered(3);  // last element
    CHECK_INT(a.count, 3);
    CHECK(a[0] == 10 && a[1] == 30 && a[2] == 40);
    a.remove_ordered(0);  // first element
    CHECK_INT(a.count, 2);
    CHECK(a[0] == 30 && a[1] == 40);
    a.remove_swap(0);
    a.remove_ordered(0);
    CHECK_INT(a.count, 0);
    a.free_mem();
}

static void test_base_array_misc() {
    struct Item { int id; float w; char tag[6]; };
    Array<Item> a = {};
    a.pop();  // pop on empty is a no-op
    CHECK_INT(a.count, 0);
    Item& z = a.push_zero();
    CHECK(z.id == 0 && z.w == 0.0f && z.tag[0] == 0 && z.tag[5] == 0);
    Item it = {7, 1.5f, "abc"};
    a.push(it);
    CHECK_INT(a.count, 2);
    CHECK(a[1].id == 7 && a[1].w == 1.5f && str_eq(a[1].tag, "abc"));
    a.pop();
    CHECK_INT(a.count, 1);
    int cap = a.cap;
    a.clear();
    CHECK_INT(a.count, 0);
    CHECK_INT(a.cap, cap);  // clear keeps the allocation
    CHECK(a.data != nullptr);
    a.free_mem();
    CHECK(a.data == nullptr && a.count == 0 && a.cap == 0);
    a.push(it);  // reusable after free_mem
    CHECK(a.count == 1 && a[0].id == 7);
    a.free_mem();
}

static void test_base_rng() {
    // PCG32 reference output for initstate 42, initseq 54 (pcg32-demo).
    Rng r;
    r.seed(42);
    const u32 ref[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (int i = 0; i < 6; i++) CHECK_HEX(r.next(), ref[i]);
    Rng a, b;
    a.seed(1234);
    b.seed(1234);
    bool same = true;
    for (int i = 0; i < 100; i++) same = same && a.next() == b.next();
    CHECK(same);
    a.seed(1);
    b.seed(2);
    int equal = 0;
    for (int i = 0; i < 100; i++) equal += a.next() == b.next();
    CHECK(equal < 3);
    bool lo = false, hi = false, inRange = true, f01ok = true, rangeOk = true;
    for (int i = 0; i < 4000; i++) {
        int v = r.irange(-2, 3);
        if (v == -2) lo = true;
        if (v == 3) hi = true;
        if (v < -2 || v > 3) inRange = false;
        float f = r.f01();
        if (!(f >= 0.0f && f < 1.0f)) f01ok = false;
        float x = r.range(5.0f, 6.0f);
        if (!(x >= 5.0f && x < 6.0f)) rangeOk = false;
    }
    CHECK(lo && hi);  // irange is inclusive on both ends
    CHECK(inRange);
    CHECK(f01ok);
    CHECK(rangeOk);
}

// ===========================================================================
// src/core/vmath
// ===========================================================================
static vec3 rand_vec(Rng& r, float s) { return vec3(r.range(-s, s), r.range(-s, s), r.range(-s, s)); }
static vec3 rand_unit(Rng& r) {
    for (;;) {
        vec3 v = rand_vec(r, 1.0f);
        float l = length(v);
        if (l > 0.1f && l <= 1.0f) return v / l;
    }
}
static quat rand_quat(Rng& r) { return quat_axis_angle(rand_unit(r), r.range(-TAU_F, TAU_F)); }
static bool vec_near(vec3 a, vec3 b, float eps) {
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps;
}
static float mat_max_diff(const mat4& a, const mat4& b) {
    float d = 0;
    for (int i = 0; i < 16; i++) d = fmaxf(d, fabsf(a.m[i] - b.m[i]));
    return d;
}
static float mat_max_abs(const mat4& a) {
    float d = 0;
    for (int i = 0; i < 16; i++) d = fmaxf(d, fabsf(a.m[i]));
    return d;
}

static void test_vmath_quat_vs_matrix() {
    // Right-handed rotations by +90 degrees.
    CHECK_VEC(quat_rotate(quat_axis_angle(vec3(0, 1, 0), PI_F * 0.5f), vec3(1, 0, 0)), vec3(0, 0, -1), 1e-6f);
    CHECK_VEC(quat_rotate(quat_axis_angle(vec3(1, 0, 0), PI_F * 0.5f), vec3(0, 1, 0)), vec3(0, 0, 1), 1e-6f);
    CHECK_VEC(quat_rotate(quat_axis_angle(vec3(0, 0, 1), PI_F * 0.5f), vec3(1, 0, 0)), vec3(0, 1, 0), 1e-6f);
    // The axis is normalised by quat_axis_angle.
    CHECK_VEC(quat_rotate(quat_axis_angle(vec3(0, 5, 0), PI_F), vec3(1, 0, 0)), vec3(-1, 0, 0), 1e-6f);
    Rng r;
    r.seed(1);
    int mismatch = 0, notUnit = 0, notRigid = 0;
    for (int i = 0; i < 500; i++) {
        quat q = rand_quat(r);
        vec3 v = rand_vec(r, 10.0f);
        mat4 m = mat4_from_quat(q);
        vec3 a = quat_rotate(q, v);
        vec3 b = transform_dir(m, v);
        vec3 c = transform_point(m, v);
        if (!vec_near(a, b, 1e-4f) || !vec_near(a, c, 1e-4f)) mismatch++;
        if (fabsf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) > 1e-5f) notUnit++;
        if (fabsf(length(a) - length(v)) > 1e-4f) notRigid++;
    }
    CHECK_INT(mismatch, 0);
    CHECK_INT(notUnit, 0);
    CHECK_INT(notRigid, 0);
}

static void test_vmath_quat_compose_conj() {
    Rng r;
    r.seed(2);
    int composeBad = 0, conjBad = 0, matBad = 0;
    for (int i = 0; i < 300; i++) {
        quat a = rand_quat(r), b = rand_quat(r);
        vec3 v = rand_vec(r, 5.0f);
        if (!vec_near(quat_rotate(a * b, v), quat_rotate(a, quat_rotate(b, v)), 1e-4f)) composeBad++;
        if (!vec_near(quat_rotate(quat_conj(a), quat_rotate(a, v)), v, 1e-4f)) conjBad++;
        if (mat_max_diff(mat4_from_quat(a * b), mat4_from_quat(a) * mat4_from_quat(b)) > 1e-5f) matBad++;
    }
    CHECK_INT(composeBad, 0);
    CHECK_INT(conjBad, 0);
    CHECK_INT(matBad, 0);
    // Euler order: yaw (Y) * pitch (X) * roll (Z).
    float p = 0.3f, y = -1.1f, ro = 0.7f;
    mat4 e = mat4_from_quat(quat_euler(p, y, ro));
    mat4 ref = mat4_from_quat(quat_axis_angle(vec3(0, 1, 0), y)) * mat4_from_quat(quat_axis_angle(vec3(1, 0, 0), p)) *
               mat4_from_quat(quat_axis_angle(vec3(0, 0, 1), ro));
    CHECK_NEAR(mat_max_diff(e, ref), 0, 1e-5);
    quat n = quat_normalize(quat(0, 0, 0, 0));
    CHECK(n.x == 0 && n.y == 0 && n.z == 0 && n.w == 1);  // degenerate input gives identity
    n = quat_normalize(quat(0, 0, 0, 2));
    CHECK(n.x == 0 && n.y == 0 && n.z == 0 && n.w == 1);
    CHECK_VEC(quat_rotate(quat_identity(), vec3(1, 2, 3)), vec3(1, 2, 3), 0);
}

static void test_vmath_mat4_inverse() {
    mat4 mats[6];
    mats[0] = mat4_identity();
    mats[1] = mat4_trs(vec3(1, -2, 3), quat_axis_angle(vec3(1, 1, 0), 0.7f), vec3(2, 0.5f, 3));
    mats[2] = mat4_look_at(vec3(5, 2, 5), vec3(0, 1, 0), vec3(0, 1, 0));
    mats[3] = mat4_perspective(1.0f, 1.6f, 0.1f, 500.0f);
    mats[4] = mat4_ortho(-4, 6, -2, 3, 0.5f, 80);
    mats[5] = mats[3] * mats[2];
    mat4 I = mat4_identity();
    for (int i = 0; i < 6; i++) {
        mat4 inv = mat4_inverse(mats[i]);
        CHECK_NEAR(mat_max_diff(mats[i] * inv, I), 0, 2e-4);
        CHECK_NEAR(mat_max_diff(inv * mats[i], I), 0, 2e-4);
        CHECK_NEAR(mat_max_diff(mat4_inverse(inv), mats[i]) / fmaxf(1.0f, mat_max_abs(mats[i])), 0, 1e-4);
    }
    Rng r;
    r.seed(3);
    int bad = 0;
    for (int i = 0; i < 200; i++) {
        vec3 s(r.range(0.2f, 4.0f), r.range(0.2f, 4.0f), r.range(0.2f, 4.0f));
        mat4 m = mat4_trs(rand_vec(r, 100.0f), rand_quat(r), s);
        if (mat_max_diff(m * mat4_inverse(m), I) > 1e-4f) bad++;
    }
    CHECK_INT(bad, 0);
    CHECK_NEAR(mat_max_diff(mat4_inverse(mat4_translate(vec3(3, -4, 5))), mat4_translate(vec3(-3, 4, -5))), 0, 1e-6);
    quat q = quat_axis_angle(vec3(0.2f, 1, -0.3f), 1.3f);
    CHECK_NEAR(mat_max_diff(mat4_inverse(mat4_from_quat(q)), mat4_from_quat(quat_conj(q))), 0, 1e-5);
    // A singular matrix returns identity instead of dividing by zero.
    CHECK_NEAR(mat_max_diff(mat4_inverse(mat4_scale(vec3(1, 0, 1))), I), 0, 0);
}

static void test_vmath_mat4_trs() {
    vec3 t(4, -1, 2.5f), s(2, 3, 0.5f);
    quat q = quat_axis_angle(vec3(0.3f, 1, 0.2f), 0.9f);
    mat4 m = mat4_trs(t, q, s);
    mat4 ref = mat4_translate(t) * mat4_from_quat(q) * mat4_scale(s);
    CHECK_NEAR(mat_max_diff(m, ref), 0, 1e-5);
    CHECK(m.m[3] == 0 && m.m[7] == 0 && m.m[11] == 0 && m.m[15] == 1);  // affine bottom row
    Rng r;
    r.seed(4);
    int bad = 0;
    for (int i = 0; i < 200; i++) {
        vec3 p = rand_vec(r, 10.0f);
        if (!vec_near(transform_point(m, p), t + quat_rotate(q, s * p), 1e-4f)) bad++;
        if (!vec_near(transform_dir(m, p), quat_rotate(q, s * p), 1e-4f)) bad++;  // directions ignore translation
    }
    CHECK_INT(bad, 0);
    CHECK_NEAR(mat_max_diff(mat4_identity() * m, m), 0, 0);
    CHECK_NEAR(mat_max_diff(m * mat4_identity(), m), 0, 0);
    CHECK_NEAR(mat_max_diff(mat4_rotate_y(0.8f), mat4_from_quat(quat_axis_angle(vec3(0, 1, 0), 0.8f))), 0, 0);
    CHECK_VEC(transform_point(mat4_trs(vec3(0, 0, 0), quat_identity(), vec3(1, 1, 1)), vec3(1, 2, 3)), vec3(1, 2, 3), 0);
}

static void test_vmath_normalize() {
    CHECK_VEC(normalize(vec3(3, 4, 0)), vec3(0.6f, 0.8f, 0), 1e-6f);
    CHECK_VEC(normalize(vec3(0, -5, 0)), vec3(0, -1, 0), 0);
    CHECK_VEC(normalize(vec3(0, 0, 0)), vec3(0, 0, 0), 0);      // zero stays zero (no NaN)
    CHECK_VEC(normalize(vec3(1e-9f, 0, 0)), vec3(0, 0, 0), 0);  // below the 1e-8 threshold
    vec2 v2 = normalize(vec2(3, 4));
    CHECK(fabsf(v2.x - 0.6f) < 1e-6f && fabsf(v2.y - 0.8f) < 1e-6f);
    v2 = normalize(vec2(0, 0));
    CHECK(v2.x == 0 && v2.y == 0);
    Rng r;
    r.seed(5);
    int bad = 0;
    for (int i = 0; i < 500; i++) {
        vec3 v = rand_vec(r, 1000.0f);
        if (length(v) < 1e-3f) continue;
        vec3 n = normalize(v);
        if (fabsf(length(n) - 1.0f) > 1e-5f) bad++;
        if (dot(n, v) <= 0) bad++;  // same direction
    }
    CHECK_INT(bad, 0);
}

static void test_vmath_wrap_angle() {
    CHECK_NEAR(wrap_angle(0.0f), 0.0, 0);
    CHECK_NEAR(wrap_angle(PI_F), PI_F, 0);    // boundaries are kept
    CHECK_NEAR(wrap_angle(-PI_F), -PI_F, 0);
    CHECK_NEAR(wrap_angle(1.5f * PI_F), -0.5f * PI_F, 1e-5);
    CHECK_NEAR(wrap_angle(-1.5f * PI_F), 0.5f * PI_F, 1e-5);
    CHECK_NEAR(wrap_angle(10.0f * PI_F + 0.5f), 0.5, 1e-4);
    CHECK_NEAR(wrap_angle(-7.0f), -7.0f + TAU_F, 1e-5);
    int bad = 0;
    for (float a = -50.0f; a <= 50.0f; a += 0.37f) {
        float w = wrap_angle(a);
        if (w < -PI_F || w > PI_F) bad++;
        if (fabsf(sinf(w) - sinf(a)) > 1e-4f || fabsf(cosf(w) - cosf(a)) > 1e-4f) bad++;
    }
    CHECK_INT(bad, 0);
    // angle_approach takes the short way round through +/-PI.
    CHECK_NEAR(angle_approach(3.0f, -3.0f, 0.1f), 3.1, 1e-5);
    CHECK_NEAR(angle_approach(-3.0f, 3.0f, 0.1f), -3.1, 1e-5);
    CHECK_NEAR(angle_approach(1.0f, 1.05f, 0.1f), 1.05, 0);
    CHECK_NEAR(approach(0.0f, 10.0f, 3.0f), 3.0, 0);
    CHECK_NEAR(approach(9.0f, 10.0f, 3.0f), 10.0, 0);
    CHECK_NEAR(approach(10.0f, 0.0f, 4.0f), 6.0, 0);
    CHECK_NEAR(approach(1.0f, 0.0f, 4.0f), 0.0, 0);
}

static void test_vmath_dir_from_yaw() {
    CHECK_VEC(dir_from_yaw(0.0f), vec3(0, 0, 1), 1e-6f);
    CHECK_VEC(dir_from_yaw(PI_F * 0.5f), vec3(1, 0, 0), 1e-6f);
    CHECK_VEC(dir_from_yaw(PI_F), vec3(0, 0, -1), 1e-6f);
    CHECK_VEC(dir_from_yaw(-PI_F * 0.5f), vec3(-1, 0, 0), 1e-6f);
    int bad = 0;
    for (float a = -3.1f; a <= 3.1f; a += 0.05f) {
        vec3 d = dir_from_yaw(a);
        if (fabsf(length(d) - 1.0f) > 1e-6f || d.y != 0) bad++;
        if (fabsf(yaw_from_dir(d) - a) > 1e-5f) bad++;
        // Consistent with a rotation of +Z about +Y (quaternion and matrix forms).
        if (!vec_near(quat_rotate(quat_axis_angle(vec3(0, 1, 0), a), vec3(0, 0, 1)), d, 1e-5f)) bad++;
        if (!vec_near(transform_dir(mat4_rotate_y(a), vec3(0, 0, 1)), d, 1e-5f)) bad++;
    }
    CHECK_INT(bad, 0);
}

static void test_vmath_reflect() {
    CHECK_VEC(reflect_v(vec3(1, -1, 0), vec3(0, 1, 0)), vec3(1, 1, 0), 0);
    CHECK_VEC(reflect_v(vec3(0, -2, 0), vec3(0, 1, 0)), vec3(0, 2, 0), 0);  // head-on: reversed
    CHECK_VEC(reflect_v(vec3(3, 0, 1), vec3(0, 1, 0)), vec3(3, 0, 1), 0);   // parallel to surface: unchanged
    Rng r;
    r.seed(6);
    int bad = 0;
    for (int i = 0; i < 300; i++) {
        vec3 d = rand_vec(r, 10.0f), n = rand_unit(r);
        vec3 rf = reflect_v(d, n);
        if (fabsf(length(rf) - length(d)) > 1e-4f) bad++;          // length preserved
        if (fabsf(dot(rf, n) + dot(d, n)) > 1e-4f) bad++;           // normal component flipped
        if (!vec_near(reflect_v(rf, n), d, 1e-4f)) bad++;           // involution
    }
    CHECK_INT(bad, 0);
}

static void test_vmath_slerp_from_to() {
    quat a = quat_axis_angle(vec3(0, 1, 0), 0.0f), b = quat_axis_angle(vec3(0, 1, 0), PI_F * 0.5f);
    vec3 v(0, 0, 1);
    CHECK_VEC(quat_rotate(quat_slerp(a, b, 0.0f), v), quat_rotate(a, v), 1e-5f);
    CHECK_VEC(quat_rotate(quat_slerp(a, b, 1.0f), v), quat_rotate(b, v), 1e-5f);
    CHECK_VEC(quat_rotate(quat_slerp(a, b, 0.5f), v), quat_rotate(quat_axis_angle(vec3(0, 1, 0), PI_F * 0.25f), v), 1e-5f);
    // q and -q are the same rotation: slerp takes the short path either way.
    quat nb = quat(-b.x, -b.y, -b.z, -b.w);
    CHECK_VEC(quat_rotate(quat_slerp(a, nb, 0.5f), v), quat_rotate(quat_slerp(a, b, 0.5f), v), 1e-5f);
    // Nearly identical inputs use the normalised lerp path.
    quat c = quat_axis_angle(vec3(0, 1, 0), 0.001f);
    quat s = quat_slerp(a, c, 0.5f);
    CHECK_NEAR(s.x * s.x + s.y * s.y + s.z * s.z + s.w * s.w, 1.0, 1e-5);
    Rng r;
    r.seed(8);
    int bad = 0;
    for (int i = 0; i < 300; i++) {
        vec3 from = rand_unit(r), to = rand_unit(r);
        if (!vec_near(quat_rotate(quat_from_to(from, to), from), to, 1e-4f)) bad++;
    }
    CHECK_INT(bad, 0);
    CHECK_VEC(quat_rotate(quat_from_to(vec3(1, 0, 0), vec3(-1, 0, 0)), vec3(1, 0, 0)), vec3(-1, 0, 0), 1e-5f);
    CHECK_VEC(quat_rotate(quat_from_to(vec3(0, 1, 0), vec3(0, -1, 0)), vec3(0, 1, 0)), vec3(0, -1, 0), 1e-5f);
    quat id = quat_from_to(vec3(0, 0, 1), vec3(0, 0, 1));
    CHECK(id.x == 0 && id.y == 0 && id.z == 0 && id.w == 1);
}

static void test_vmath_projection() {
    float zn = 0.5f, zf = 200.0f;
    mat4 P = mat4_perspective(60.0f * DEG2RAD, 16.0f / 9.0f, zn, zf);
    vec4 cn = P * vec4(0, 0, -zn, 1);
    vec4 cf = P * vec4(0, 0, -zf, 1);
    CHECK_NEAR(cn.z / cn.w, -1.0, 1e-5);  // near plane maps to NDC -1
    CHECK_NEAR(cf.z / cf.w, 1.0, 1e-4);   // far plane maps to NDC +1
    CHECK_NEAR((P * vec4(0, 0, -10, 1)).w, 10.0, 1e-5);
    // Top edge of the view at depth 10 is at y = tan(30 deg) * 10.
    vec4 ct = P * vec4(0, tanf(30.0f * DEG2RAD) * 10.0f, -10, 1);
    CHECK_NEAR(ct.y / ct.w, 1.0, 1e-5);
    mat4 O = mat4_ortho(-4, 6, -2, 3, 0.5f, 80);
    CHECK_VEC(transform_point(O, vec3(6, 3, -80)), vec3(1, 1, 1), 1e-5f);
    CHECK_VEC(transform_point(O, vec3(-4, -2, -0.5f)), vec3(-1, -1, -1), 1e-5f);
    mat4 V = mat4_look_at(vec3(0, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0));
    CHECK_NEAR(mat_max_diff(V, mat4_identity()), 0, 1e-6);
    mat4 V2 = mat4_look_at(vec3(5, 2, 5), vec3(0, 1, 0), vec3(0, 1, 0));
    CHECK_VEC(transform_point(V2, vec3(5, 2, 5)), vec3(0, 0, 0), 1e-5f);  // eye at the origin
    vec3 tgt = transform_point(V2, vec3(0, 1, 0));
    CHECK(fabsf(tgt.x) < 1e-5f && fabsf(tgt.y) < 1e-5f && tgt.z < 0);    // target straight ahead (-Z)
}

static void test_vmath_frustum() {
    mat4 P = mat4_perspective(60.0f * DEG2RAD, 16.0f / 9.0f, 0.1f, 100.0f);
    Frustum f = frustum_from_matrix(P * mat4_look_at(vec3(0, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0)));
    int notUnit = 0;
    for (int i = 0; i < 6; i++) {
        vec4 p = f.planes[i];
        if (fabsf(sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) - 1.0f) > 1e-5f) notUnit++;
    }
    CHECK_INT(notUnit, 0);
    CHECK(frustum_sphere(f, vec3(0, 0, -10), 1.0f));
    CHECK(!frustum_sphere(f, vec3(0, 0, 10), 1.0f));          // behind the camera
    CHECK(!frustum_sphere(f, vec3(0, 0, -200), 1.0f));        // beyond the far plane
    CHECK(!frustum_sphere(f, vec3(0, 0, -0.01f), 0.001f));    // in front of the near plane
    CHECK(!frustum_sphere(f, vec3(-30, 0, -10), 1.0f));       // left of the view
    CHECK(frustum_sphere(f, vec3(-10.5f, 0, -10), 1.0f));     // straddles the left plane
    CHECK(frustum_sphere(f, vec3(-30, 0, -10), 25.0f));       // large sphere reaches in
    CHECK(frustum_aabb(f, vec3(-1, -1, -12), vec3(1, 1, -8)));
    CHECK(!frustum_aabb(f, vec3(-1, -1, 5), vec3(1, 1, 8)));
    CHECK(frustum_aabb(f, vec3(-50, -50, -50), vec3(50, 50, 50)));  // contains the camera
    CHECK(!frustum_aabb(f, vec3(40, -1, -12), vec3(42, 1, -8)));
    // Random points: the culling planes agree with the clip-space test.
    mat4 VP = mat4_perspective(50.0f * DEG2RAD, 1.5f, 0.5f, 200.0f) * mat4_look_at(vec3(5, 2, 5), vec3(0, 1, 0), vec3(0, 1, 0));
    Frustum f2 = frustum_from_matrix(VP);
    Rng r;
    r.seed(9);
    int tested = 0, mismatch = 0, insideCount = 0;
    for (int i = 0; i < 4000; i++) {
        vec3 p = rand_vec(r, 150.0f);
        vec4 c = VP * vec4(p, 1.0f);
        if (fabsf(c.w) < 0.05f) continue;
        bool inside = false;
        if (c.w > 0) {
            float m = fmaxf(fmaxf(fabsf(c.x / c.w), fabsf(c.y / c.w)), fabsf(c.z / c.w));
            if (fabsf(m - 1.0f) < 0.02f) continue;  // too close to a boundary to compare
            inside = m < 1.0f;
        }
        tested++;
        insideCount += inside;
        if (frustum_sphere(f2, p, 0.0f) != inside) mismatch++;
        if (frustum_aabb(f2, p, p) != inside) mismatch++;
    }
    CHECK(tested > 3000);
    CHECK(insideCount > 20);
    CHECK_INT(mismatch, 0);
}

static void test_vmath_ray_queries() {
    AABB box = {vec3(0, 0, 0), vec3(1, 1, 1)};
    float t = -1;
    vec3 n(0, 0, 0);
    CHECK(ray_aabb(vec3(-5, 0.5f, 0.5f), vec3(1, INFINITY, INFINITY), box, 100, &t, &n));
    CHECK_NEAR(t, 5.0, 1e-6);
    CHECK_VEC(n, vec3(-1, 0, 0), 0);
    CHECK(ray_aabb(vec3(0.5f, 5, 0.5f), vec3(INFINITY, -1, INFINITY), box, 100, &t, &n));
    CHECK_NEAR(t, 4.0, 1e-6);
    CHECK_VEC(n, vec3(0, 1, 0), 0);  // landing on the top face
    CHECK(!ray_aabb(vec3(-5, 0.5f, 0.5f), vec3(1, INFINITY, INFINITY), box, 4.0f, &t, &n));  // beyond maxT
    CHECK(!ray_aabb(vec3(5, 0.5f, 0.5f), vec3(1, INFINITY, INFINITY), box, 100, &t, &n));    // box behind
    CHECK(!ray_aabb(vec3(-5, 3, 0.5f), vec3(1, INFINITY, INFINITY), box, 100, &t, &n));      // passes above
    CHECK(ray_aabb(vec3(0.5f, 0.5f, 0.5f), vec3(1, INFINITY, INFINITY), box, 100, &t, nullptr));
    CHECK_NEAR(t, 0.0, 0);  // origin inside
    CHECK(ray_sphere(vec3(0, 0, -10), vec3(0, 0, 1), vec3(0, 0, 0), 1.0f, 100, &t));
    CHECK_NEAR(t, 9.0, 1e-5);
    CHECK(!ray_sphere(vec3(0, 0, -10), vec3(0, 0, 1), vec3(0, 0, 0), 1.0f, 5.0f, &t));
    CHECK(!ray_sphere(vec3(0, 0, -10), vec3(0, 0, -1), vec3(0, 0, 0), 1.0f, 100, &t));  // pointing away
    CHECK(!ray_sphere(vec3(2, 0, -10), vec3(0, 0, 1), vec3(0, 0, 0), 1.0f, 100, &t));   // misses
    CHECK(ray_sphere(vec3(0, 0, 0.5f), vec3(0, 0, 1), vec3(0, 0, 0), 1.0f, 100, &t));
    CHECK_NEAR(t, 0.0, 0);
    vec3 cl;
    CHECK_NEAR(dist_point_segment(vec3(0, 1, 0), vec3(-1, 0, 0), vec3(1, 0, 0), &cl), 1.0, 1e-6);
    CHECK_VEC(cl, vec3(0, 0, 0), 1e-6f);
    CHECK_NEAR(dist_point_segment(vec3(3, 0, 0), vec3(-1, 0, 0), vec3(1, 0, 0), &cl), 2.0, 1e-6);
    CHECK_VEC(cl, vec3(1, 0, 0), 0);
    CHECK_NEAR(dist_point_segment(vec3(0, 3, 4), vec3(0, 0, 0), vec3(0, 0, 0), nullptr), 5.0, 1e-6);
    CHECK(aabb_overlap(box, AABB{vec3(0.5f, 0.5f, 0.5f), vec3(2, 2, 2)}));
    CHECK(aabb_overlap(box, AABB{vec3(1, 1, 1), vec3(2, 2, 2)}));  // touching counts
    CHECK(!aabb_overlap(box, AABB{vec3(1.1f, 0, 0), vec3(2, 1, 1)}));
    CHECK(aabb_contains(box, vec3(1, 0, 0.5f)));
    CHECK(!aabb_contains(box, vec3(1.01f, 0.5f, 0.5f)));
}

// ===========================================================================
// src/core/noise
// ===========================================================================
static void test_noise_determinism() {
    // Golden values: world generation and texture synthesis depend on these exact fields.
    CHECK_NEAR(noise_hash2(17, -5, 1234), 0.818621993, 1e-7);
    CHECK_NEAR(noise_value(3.3f, 7.7f, 42), 0.705725014, 1e-5);
    CHECK_NEAR(noise_fbm(1.25f, -2.5f, 4, 7), 0.44710353, 1e-5);
    CHECK_NEAR(noise_value_tiled(5.5f, 2.25f, 8, 99), 0.32198751, 1e-5);
    CHECK_NEAR(noise_fbm_tiled(0.3f, 0.6f, 4, 5, 11), 0.589661956, 1e-5);
    float f1 = -1, edge = -1;
    u32 id = 0;
    noise_worley_tiled(2.5f, 3.5f, 8, 77, &f1, &edge, &id);
    CHECK_NEAR(f1, 0.555496871, 1e-5);
    CHECK_NEAR(edge, 0.0538740158, 1e-5);
    CHECK_INT(id, 15840);
    // Repeated evaluation is bit-identical; different seeds give different fields.
    Rng r;
    r.seed(10);
    int unstable = 0, sameAcrossSeeds = 0;
    for (int i = 0; i < 200; i++) {
        float x = r.range(-100, 100), y = r.range(-100, 100);
        if (noise_value(x, y, 5) != noise_value(x, y, 5)) unstable++;
        if (noise_fbm(x, y, 5, 5) != noise_fbm(x, y, 5, 5)) unstable++;
        if (noise_fbm_tiled(x, y, 16, 4, 5) != noise_fbm_tiled(x, y, 16, 4, 5)) unstable++;
        if (fabsf(noise_value(x, y, 5) - noise_value(x, y, 6)) < 1e-6f) sameAcrossSeeds++;
    }
    CHECK_INT(unstable, 0);
    CHECK(sameAcrossSeeds < 5);
}

static void test_noise_ranges() {
    Rng r;
    r.seed(11);
    int bad = 0;
    float hmin = 1, hmax = 0, vmin = 1, vmax = 0;
    double hsum = 0;
    const int N = 20000;
    for (int i = 0; i < N; i++) {
        int ix = (int)(r.next() % 20001) - 10000, iy = (int)(r.next() % 20001) - 10000;
        float h = noise_hash2(ix, iy, r.next());
        if (!(h >= 0.0f && h < 1.0f)) bad++;
        hmin = fminf(hmin, h);
        hmax = fmaxf(hmax, h);
        hsum += h;
        float x = r.range(-1000, 1000), y = r.range(-1000, 1000);
        float v = noise_value(x, y, 3);
        if (!(v >= 0.0f && v <= 1.0f)) bad++;
        vmin = fminf(vmin, v);
        vmax = fmaxf(vmax, v);
        float fb = noise_fbm(x, y, 5, 3);
        if (!(fb >= 0.0f && fb <= 1.0f)) bad++;
        float tx = r.range(-50, 50), ty = r.range(-50, 50);
        float vt = noise_value_tiled(tx, ty, 8, 3);
        if (!(vt >= 0.0f && vt <= 1.0f)) bad++;
        float ft = noise_fbm_tiled(tx, ty, 8, 4, 3, 0.7f);
        if (!(ft >= 0.0f && ft <= 1.0f)) bad++;
        float f1, e;
        u32 id;
        noise_worley_tiled(tx, ty, 8, 3, &f1, &e, &id);
        if (!(f1 >= 0.0f && f1 < 1.4143f)) bad++;  // own cell's point is within the cell diagonal
        if (!(e >= 0.0f)) bad++;
        if (id > 7u * 7919u + 7u) bad++;
    }
    CHECK_INT(bad, 0);
    CHECK(hmin < 0.01f && hmax > 0.99f);  // covers the full range
    CHECK(vmin < 0.1f && vmax > 0.9f);
    CHECK_NEAR(hsum / N, 0.5, 0.02);      // roughly uniform
    float f1;
    noise_worley_tiled(1.5f, 1.5f, 8, 3, &f1, nullptr, nullptr);  // optional outputs may be null
    CHECK(f1 >= 0.0f);
}

static void test_noise_tiling() {
    Rng r;
    r.seed(12);
    int bad = 0;
    for (int i = 0; i < 500; i++) {
        float x = r.range(0, 8), y = r.range(0, 8);
        float v = noise_value_tiled(x, y, 8, 21);
        if (fabsf(v - noise_value_tiled(x + 8, y, 8, 21)) > 1e-4f) bad++;
        if (fabsf(v - noise_value_tiled(x, y - 8, 8, 21)) > 1e-4f) bad++;
        if (fabsf(v - noise_value_tiled(x - 16, y + 24, 8, 21)) > 1e-4f) bad++;
        float fb = noise_fbm_tiled(x, y, 4, 5, 21);
        if (fabsf(fb - noise_fbm_tiled(x + 4, y, 4, 5, 21)) > 1e-4f) bad++;
        if (fabsf(fb - noise_fbm_tiled(x, y + 4, 4, 5, 21)) > 1e-4f) bad++;
        float a1, ae, b1, be;
        u32 aid, bid;
        noise_worley_tiled(x, y, 8, 21, &a1, &ae, &aid);
        noise_worley_tiled(x + 8, y - 8, 8, 21, &b1, &be, &bid);
        if (fabsf(a1 - b1) > 1e-3f || fabsf(ae - be) > 1e-3f || aid != bid) bad++;
    }
    CHECK_INT(bad, 0);
}

static void test_noise_lattice_continuity() {
    int bad = 0;
    for (int x = -5; x <= 5; x++)
        for (int y = -5; y <= 5; y++) {
            if (noise_value((float)x, (float)y, 9) != noise_hash2(x, y, 9)) bad++;  // exact at lattice points
            int wx = ((x % 4) + 4) % 4, wy = ((y % 4) + 4) % 4;
            if (noise_value_tiled((float)x, (float)y, 4, 9) != noise_hash2(wx, wy, 9)) bad++;
        }
    CHECK_INT(bad, 0);
    Rng r;
    r.seed(13);
    int jumps = 0;
    for (int i = 0; i < 2000; i++) {
        float x = r.range(-100, 100), y = r.range(-100, 100);
        if (fabsf(noise_value(x + 1e-3f, y, 4) - noise_value(x, y, 4)) > 0.01f) jumps++;
        if (fabsf(noise_value(x, y + 1e-3f, 4) - noise_value(x, y, 4)) > 0.01f) jumps++;
    }
    CHECK_INT(jumps, 0);
}

// ===========================================================================
// src/game/script
// ===========================================================================
static void test_script_basic_tokens() {
    const char* src = "spawn  thug\t10 -2.5 \"Big Tony\"\n  a\t\tb   c  \n";
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 5);
    CHECK_STR(script_str(l, 0), "spawn");
    CHECK_STR(script_str(l, 1), "thug");
    CHECK_STR(script_str(l, 2), "10");
    CHECK_STR(script_str(l, 3), "-2.5");
    CHECK_STR(script_str(l, 4), "Big Tony");
    CHECK_INT(l.lineNo, 1);
    CHECK_INT(ln, 1);
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 3);
    CHECK(str_eq(l.argv[0], "a") && str_eq(l.argv[1], "b") && str_eq(l.argv[2], "c"));
    CHECK_INT(l.lineNo, 2);
    CHECK(!script_next_line(&cur, &l, &ln));
    CHECK(*cur == 0);  // cursor left at the end
    CHECK(!script_next_line(&cur, &l, &ln));
    CHECK_INT(ln, 2);
    const char* empty = "";
    CHECK(!script_next_line(&empty, &l, &ln));
    // Without a line counter the line number reads 0.
    const char* one = "x y";
    CHECK(script_next_line(&one, &l, nullptr));
    CHECK_INT(l.lineNo, 0);
    CHECK_INT(l.argc, 2);
    // The counter continues from the caller's value.
    const char* two = "\nz";
    int base = 100;
    CHECK(script_next_line(&two, &l, &base));
    CHECK_INT(l.lineNo, 102);
}

static void test_script_quotes() {
    const char* src =
        "say \"hello world\" \"\" end\n"
        "\"a#b\" c\n"
        "\"unterminated quote here\n"
        "\"ab\"cd\n"
        "ab\"cd\"\n"
        "\"tab\tinside\"\n";
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 4);
    CHECK_STR(script_str(l, 1), "hello world");
    CHECK_STR(script_str(l, 2, "default"), "");  // empty quoted token still counts
    CHECK_STR(script_str(l, 3), "end");
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 2);
    CHECK_STR(script_str(l, 0), "a#b");  // '#' inside quotes is not a comment
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 1);
    CHECK_STR(script_str(l, 0), "unterminated quote here");  // runs to the end of the line
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 2);  // closing quote ends the token
    CHECK(str_eq(l.argv[0], "ab") && str_eq(l.argv[1], "cd"));
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 1);  // quotes inside a bare word are kept
    CHECK_STR(script_str(l, 0), "ab\"cd\"");
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_STR(script_str(l, 0), "tab\tinside");
    CHECK_INT(l.lineNo, 6);
}

static void test_script_comments_lines() {
    const char* src =
        "# header comment\n"               // 1
        "\n"                                // 2
        "   \t  \n"                         // 3
        "cmd1 a b # trailing comment\n"     // 4
        "    # indented comment\n"          // 5
        "cmd2 x#y\n"                        // 6
        "\n"                                // 7
        "cmd3";                             // 8, no trailing newline
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.lineNo, 4);
    CHECK_INT(l.argc, 3);
    CHECK_STR(script_str(l, 2), "b");
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.lineNo, 6);
    CHECK_INT(l.argc, 2);
    CHECK_STR(script_str(l, 1), "x#y");  // '#' only starts a comment at a token boundary
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.lineNo, 8);
    CHECK_INT(l.argc, 1);
    CHECK_STR(script_str(l, 0), "cmd3");
    CHECK(!script_next_line(&cur, &l, &ln));
    CHECK_INT(ln, 8);
    const char* onlyComments = "# a\n\n# b\n   \n";
    int n2 = 0;
    CHECK(!script_next_line(&onlyComments, &l, &n2));
    CHECK_INT(n2, 4);
}

static void test_script_crlf() {
    const char* src = "a b\r\nc\r\n\r\n d \r\n";
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 2);
    CHECK_STR(script_str(l, 1), "b");  // no trailing '\r'
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_STR(script_str(l, 0), "c");
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.lineNo, 4);
    CHECK_INT(l.argc, 1);
    CHECK_STR(script_str(l, 0), "d");
    CHECK(!script_next_line(&cur, &l, &ln));
}

static void test_script_max_args() {
    const char* src =
        "t0 t1 t2 t3 t4 t5 t6 t7 t8 t9 t10 t11 t12 t13 t14 t15 t16 t17 t18 t19\n"
        "next 1\n"
        "q0 q1 q2 q3 q4 q5 q6 q7 q8 q9 q10 q11 q12 q13 q14 q15\n";
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, SCRIPT_MAX_ARGS);  // extra tokens are dropped
    CHECK_STR(script_str(l, SCRIPT_MAX_ARGS - 1), "t15");
    CHECK_STR(script_str(l, SCRIPT_MAX_ARGS, "none"), "none");
    CHECK(script_next_line(&cur, &l, &ln));  // the next line is unaffected
    CHECK_INT(l.lineNo, 2);
    CHECK_INT(l.argc, 2);
    CHECK_STR(script_str(l, 0), "next");
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 16);  // exactly SCRIPT_MAX_ARGS tokens
    CHECK_STR(script_str(l, 15), "q15");
}

static void test_script_long_line() {
    static char src[1024];
    int n = 0;
    memcpy(src, "cmd ", 4);
    n = 4;
    for (int i = 0; i < 696; i++) src[n++] = 'a';
    src[n++] = '\n';
    memcpy(src + n, "next 1\n", 8);  // includes the terminator
    const char* cur = src;
    ScriptLine l;
    int ln = 0;
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.argc, 2);
    CHECK_INT(strlen(script_str(l, 1)), (int)sizeof(l.buf) - 1 - 4);  // line truncated to the buffer
    CHECK(script_next_line(&cur, &l, &ln));
    CHECK_INT(l.lineNo, 2);
    CHECK_STR(script_str(l, 0), "next");
    CHECK_INT(script_int(l, 1), 1);
}

static void test_script_accessors() {
    const char* src = "op 42 -3.75 name";
    const char* cur = src;
    ScriptLine l;
    CHECK(script_next_line(&cur, &l, nullptr));
    CHECK_INT(script_int(l, 1), 42);
    CHECK_NEAR(script_float(l, 2), -3.75, 0);
    CHECK_STR(script_str(l, 3), "name");
    CHECK_NEAR(script_float(l, 1), 42.0, 0);
    CHECK_INT(script_int(l, 2), -3);   // integer parse stops at '.'
    CHECK_INT(script_int(l, 0), 0);    // non-numeric token
    CHECK_INT(script_int(l, 4), 0);    // out of range: defaults
    CHECK_INT(script_int(l, 4, -1), -1);
    CHECK_NEAR(script_float(l, 9), 0.0, 0);
    CHECK_NEAR(script_float(l, 9, 2.5f), 2.5, 0);
    CHECK_STR(script_str(l, 4), "");
    CHECK_STR(script_str(l, 4, "dflt"), "dflt");
}

// ===========================================================================
// src/game/save
// ===========================================================================
// Field ids of the save payload (mirror of enum SaveField in src/game/save.cpp).
enum TestSaveField {
    TSF_MONEY = 1, TSF_HAS_WEAPON, TSF_RESERVE, TSF_CLIP, TSF_UPGRADES, TSF_REP, TSF_EVIDENCE, TSF_CACHES, TSF_MISSIONS,
    TSF_PHASE, TSF_FLAGS, TSF_STATS, TSF_PLAYER_POS, TSF_PLAYER_VITALS, TSF_WEAPON, TSF_INFO, TSF_WORLD,
};

static void bytes_append(Array<u8>* a, const void* p, int n) {
    int at = a->count;
    a->resize(at + n);
    if (n > 0) memcpy(a->data + at, p, (size_t)n);
}
static void put_u16(Array<u8>* a, u16 v) { bytes_append(a, &v, 2); }
static void put_u32(Array<u8>* a, u32 v) { bytes_append(a, &v, 4); }
static void add_field(Array<u8>* payload, u16 id, const void* p, u32 len) {
    put_u16(payload, id);
    put_u32(payload, len);
    bytes_append(payload, p, (int)len);
}
// Wraps a payload in a header with the correct size and CRC.
static void make_save(Array<u8>* out, u32 version, const Array<u8>& payload) {
    out->clear();
    bytes_append(out, "FGTO", 4);
    put_u32(out, version);
    put_u32(out, (u32)payload.count);
    put_u32(out, crc32(payload.data, (size_t)payload.count, 0));
    bytes_append(out, payload.data, payload.count);
}
static const u8* find_field(const Array<u8>& save, u16 id, u32* lenOut) {
    u32 off = 16, size = (u32)save.count;
    while (off + 6 <= size) {
        u16 fid;
        u32 len;
        memcpy(&fid, save.data + off, 2);
        memcpy(&len, save.data + off + 2, 4);
        off += 6;
        if (len > size - off) return nullptr;
        if (fid == id) {
            *lenOut = len;
            return save.data + off;
        }
        off += len;
    }
    return nullptr;
}
static bool load_save(const Array<u8>& s, char* err, int errCap) {
    err[0] = 0;
    return save_deserialize(s.data, s.count, err, errCap);
}
static void copy_array(Array<u8>* dst, const Array<u8>& src) {
    dst->clear();
    bytes_append(dst, src.data, src.count);
}

static void reset_game() {
    memset(&g, 0, sizeof(g));
    g_stubLandmarkId = -1;
}

static void setup_known_state() {
    reset_game();
    PlayerData& pd = g.pd;
    pd.money = 123456;
    pd.hasWeapon[W_FISTS] = pd.hasWeapon[W_PISTOL] = pd.hasWeapon[W_SHOTGUN] = pd.hasWeapon[W_LANCE] = true;
    pd.reserve[W_PISTOL] = 90;
    pd.reserve[W_SHOTGUN] = 20;
    pd.reserve[W_LANCE] = 10;
    pd.clip[W_PISTOL] = 7;
    pd.clip[W_SHOTGUN] = 3;
    pd.clip[W_LANCE] = 5;
    const int ups[UP_COUNT] = {1, 2, 3, 0, 1};
    memcpy(pd.upgrades, ups, sizeof(ups));
    for (int f = 0; f < FAC_COUNT; f++) pd.rep[f] = f * 20 - 80;
    pd.evidence[0] = pd.evidence[5] = pd.evidence[MAX_EVIDENCE - 1] = true;
    pd.cachesFound[3] = pd.cachesFound[31] = true;
    pd.missionsDone = 0xA5u;
    pd.phase = 2;
    flag_set("thorne_dead", 1);
    flag_set("ch_m02_33", 2);
    flag_set("negative_flag", -5);
    pd.kills = 11; pd.knockouts = 22; pd.shotsFired = 333; pd.shotsHit = 44; pd.fightsWon = 5; pd.racesWon = 6;
    pd.civiliansRescued = 7; pd.timesArrested = 8; pd.deaths = 9;
    pd.playTime = 1234.5f;
    pd.distanceDriven = 5678.25f;
    pd.safehouseUnlocked = 1;
    pd.pitWins = 4;
    Actor& p = g.actors[PLAYER];
    p.active = true;
    p.pos = vec3(10.5f, 2.25f, -30.75f);
    p.yaw = 1.25f;
    p.health = 87.5f;
    p.armor = 42.0f;
    p.weapon = W_SHOTGUN;
    p.clipAmmo = 4;  // the live clip of the held weapon overrides pd.clip in the save
    g.power = false;
    g.rainTarget = 0.35f;
}

// Overwrites everything the loader is expected to restore.
static void clobber_state() {
    memset(&g.pd, 0, sizeof(g.pd));
    g.pd.money = 1;
    Actor& p = g.actors[PLAYER];
    p.pos = vec3(999, 999, 999);
    p.yaw = -3.0f;
    p.health = 1.0f;
    p.armor = 99.0f;
    p.weapon = W_RIFLE;
    p.clipAmmo = 77;
    g.power = true;
    g.rainTarget = 0.9f;
    g_stubLandmarkId = -1;
}

static void test_save_round_trip() {
    setup_known_state();
    Array<u8> s = {};
    CHECK(save_serialize(&s));
    CHECK(s.count > 16);
    CHECK(memcmp(s.data, "FGTO", 4) == 0);
    u32 version, psize, crc;
    memcpy(&version, s.data + 4, 4);
    memcpy(&psize, s.data + 8, 4);
    memcpy(&crc, s.data + 12, 4);
    CHECK_INT(version, 1);
    CHECK_INT(psize, s.count - 16);
    CHECK_HEX(crc, crc32(s.data + 16, psize));
    u32 len = 0;
    const u8* info = find_field(s, TSF_INFO, &len);
    CHECK(info != nullptr && len == 64);
    if (info) CHECK_STR((const char*)info, "Phase 2 | $123456 | 4 missions");

    clobber_state();
    char err[128];
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_STR(err, "");
    const PlayerData& pd = g.pd;
    CHECK_INT(pd.money, 123456);
    for (int w = 0; w < W_COUNT; w++) {
        bool expect = w == W_FISTS || w == W_PISTOL || w == W_SHOTGUN || w == W_LANCE;
        if (!CHECK(pd.hasWeapon[w] == expect)) out("    (weapon %d)\n", w);
    }
    CHECK_INT(pd.reserve[W_PISTOL], 90);
    CHECK_INT(pd.reserve[W_SHOTGUN], 20);
    CHECK_INT(pd.reserve[W_LANCE], 10);
    CHECK_INT(pd.reserve[W_SMG], 0);
    CHECK_INT(pd.clip[W_PISTOL], 7);
    CHECK_INT(pd.clip[W_SHOTGUN], 4);  // taken from the held weapon
    CHECK_INT(pd.clip[W_LANCE], 5);
    CHECK(pd.upgrades[0] == 1 && pd.upgrades[1] == 2 && pd.upgrades[2] == 3 && pd.upgrades[3] == 0 && pd.upgrades[4] == 1);
    bool repOk = true;
    for (int f = 0; f < FAC_COUNT; f++) repOk = repOk && pd.rep[f] == f * 20 - 80;
    CHECK(repOk);
    int ev = 0, ca = 0;
    for (int i = 0; i < MAX_EVIDENCE; i++) ev += pd.evidence[i];
    for (int i = 0; i < 32; i++) ca += pd.cachesFound[i];
    CHECK(ev == 3 && pd.evidence[0] && pd.evidence[5] && pd.evidence[MAX_EVIDENCE - 1]);
    CHECK(ca == 2 && pd.cachesFound[3] && pd.cachesFound[31]);
    CHECK_HEX(pd.missionsDone, 0xA5u);
    CHECK_INT(pd.phase, 2);
    CHECK_INT(pd.flagCount, 3);
    CHECK_INT(flag_get("thorne_dead"), 1);
    CHECK_INT(flag_get("ch_m02_33"), 2);
    CHECK_INT(flag_get("negative_flag"), -5);
    CHECK(pd.kills == 11 && pd.knockouts == 22 && pd.shotsFired == 333 && pd.shotsHit == 44 && pd.fightsWon == 5);
    CHECK(pd.racesWon == 6 && pd.civiliansRescued == 7 && pd.timesArrested == 8 && pd.deaths == 9);
    CHECK_NEAR(pd.playTime, 1234.5, 0);
    CHECK_NEAR(pd.distanceDriven, 5678.25, 0);
    CHECK_INT(pd.safehouseUnlocked, 1);
    CHECK_INT(pd.pitWins, 4);
    const Actor& p = g.actors[PLAYER];
    CHECK_VEC(p.pos, vec3(10.5f, 2.25f, -30.75f), 0);
    CHECK_NEAR(p.yaw, 1.25, 0);
    CHECK_NEAR(p.health, 87.5, 0);
    CHECK_NEAR(p.armor, 42.0, 0);
    CHECK_INT(p.weapon, W_SHOTGUN);
    CHECK_INT(p.clipAmmo, 4);
    CHECK(!g.power);
    CHECK_NEAR(g.rainTarget, 0.35f, 0);
    CHECK_INT(g_stubLandmarkId, -1);  // valid position: no fallback

    // Serialising the restored state reproduces the same file.
    Array<u8> s2 = {};
    save_serialize(&s2);
    CHECK(s2.count == s.count && memcmp(s2.data, s.data, (size_t)s.count) == 0);
    s.free_mem();
    s2.free_mem();
}

static void test_save_flags_round_trip() {
    reset_game();
    Array<u8> s = {};
    save_serialize(&s);
    g.pd.flagCount = 9;
    char err[128];
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(g.pd.flagCount, 0);  // no flags

    reset_game();
    char name[24];
    for (int i = 0; i < MAX_FLAGS; i++) {
        fmt_str(name, sizeof(name), "flag_%d", i);
        flag_set(name, i * 3 - 50);
    }
    CHECK_INT(g.pd.flagCount, MAX_FLAGS);
    str_copy(g.pd.flags[7].name, 24, "abcdefghijklmnopqrstuvw");  // 23 chars: the longest that fits
    save_serialize(&s);
    reset_game();
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(g.pd.flagCount, MAX_FLAGS);
    int bad = 0;
    for (int i = 0; i < MAX_FLAGS; i++) {
        if (i == 7) continue;
        fmt_str(name, sizeof(name), "flag_%d", i);
        if (flag_get(name) != i * 3 - 50) bad++;
    }
    CHECK_INT(bad, 0);
    CHECK_INT(flag_get("abcdefghijklmnopqrstuvw"), 7 * 3 - 50);
    s.free_mem();
}

static void test_save_reject_garbage() {
    reset_game();
    g.pd.money = 4242;
    char err[128];
    CHECK(!save_deserialize(nullptr, 100, err, sizeof(err)));
    CHECK_STR(err, "Save file is too small");
    const u8 small[15] = {'F', 'G', 'T', 'O', 1};
    CHECK(!save_deserialize(small, 0, err, sizeof(err)));
    CHECK_STR(err, "Save file is too small");
    CHECK(!save_deserialize(small, (int)sizeof(small), err, sizeof(err)));
    CHECK_STR(err, "Save file is too small");
    const char* text = "hello world, this is not a save file at all";
    CHECK(!save_deserialize((const u8*)text, (int)strlen(text), err, sizeof(err)));
    CHECK_STR(err, "Not a FIGHTO save file");
    u8 junk[256];
    Rng r;
    r.seed(14);
    for (int i = 0; i < 256; i++) junk[i] = (u8)r.next();
    junk[0] = 0x7f;
    CHECK(!save_deserialize(junk, (int)sizeof(junk), err, sizeof(err)));
    CHECK_STR(err, "Not a FIGHTO save file");
    // Junk behind a valid magic is rejected too (message quality: see save_junk_after_magic_message).
    int accepted = 0;
    for (int trial = 0; trial < 200; trial++) {
        for (int i = 0; i < 256; i++) junk[i] = (u8)r.next();
        memcpy(junk, "FGTO", 4);
        int size = 16 + (int)(r.next() % 240);
        if (save_deserialize(junk, size, err, sizeof(err)) || !err[0]) accepted++;
    }
    CHECK_INT(accepted, 0);
    CHECK_INT(g.pd.money, 4242);  // untouched
    // A small error buffer truncates the message without overflowing.
    char tiny[10];
    memset(tiny, '#', sizeof(tiny));
    CHECK(!save_deserialize((const u8*)text, (int)strlen(text), tiny, 8));
    CHECK_STR(tiny, "Not a F");
    CHECK(tiny[8] == '#' && tiny[9] == '#');
}

static void test_save_reject_wrong_magic() {
    setup_known_state();
    Array<u8> s = {}, bad = {};
    save_serialize(&s);
    const char* magics[] = {"fgto", "FGTX", "GTOF", "\0GTO"};
    char err[128];
    for (int i = 0; i < ARRAY_COUNT(magics); i++) {
        copy_array(&bad, s);
        memcpy(bad.data, magics[i], 4);
        CHECK(!load_save(bad, err, sizeof(err)));
        CHECK_STR(err, "Not a FIGHTO save file");
    }
    s.free_mem();
    bad.free_mem();
}

static void test_save_reject_truncated_padded() {
    setup_known_state();
    Array<u8> s = {}, bad = {};
    save_serialize(&s);
    char err[128];
    int wrong = 0;
    for (int n = 0; n < s.count; n++) {  // every possible truncation
        bool ok = save_deserialize(s.data, n, err, sizeof(err));
        const char* expect = n < 16 ? "Save file is too small" : "Save file is corrupted (truncated or padded)";
        if (ok || !str_eq(err, expect)) {
            if (wrong++ < 3) out("    truncated to %d bytes: accepted=%d err=\"%s\"\n", n, (int)ok, err);
        }
    }
    CHECK_INT(wrong, 0);
    copy_array(&bad, s);
    u8 zero = 0;
    bytes_append(&bad, &zero, 1);
    CHECK(!load_save(bad, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (truncated or padded)");
    // A shortened payload whose header was patched to match is caught by the CRC.
    copy_array(&bad, s);
    bad.count -= 10;
    u32 psize = (u32)(bad.count - 16);
    memcpy(bad.data + 8, &psize, 4);
    CHECK(!load_save(bad, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (checksum mismatch)");
    s.free_mem();
    bad.free_mem();
}

static void test_save_reject_flipped_bytes() {
    setup_known_state();
    Array<u8> s = {}, bad = {};
    save_serialize(&s);
    static u8 snapPd[sizeof(PlayerData)];
    memcpy(snapPd, &g.pd, sizeof(PlayerData));
    vec3 snapPos = g.actors[PLAYER].pos;
    char err[128];
    int accepted = 0, wrongMessage = 0, changedState = 0;
    const u8 masks[3] = {0xFF, 0x01, 0x80};
    copy_array(&bad, s);
    for (int i = 0; i < s.count; i++) {
        for (int m = 0; m < 3; m++) {
            bad.data[i] ^= masks[m];
            bool ok = load_save(bad, err, sizeof(err));
            bad.data[i] ^= masks[m];
            if (ok || !err[0]) {
                accepted++;
                continue;
            }
            const char* expect = 0;
            if (i < 4) expect = "Not a FIGHTO save file";
            else if (i >= 8 && i < 12) expect = "Save file is corrupted (truncated or padded)";
            else if (i >= 12) expect = "Save file is corrupted (checksum mismatch)";
            else if (bad.data[4] == 1 && masks[m] == 0x01 && i == 4) expect = "Save file is corrupted (invalid version)";
            // Other version-byte damage cannot be told apart from a genuine newer save, because
            // the header is not covered by the CRC: it must be rejected as a newer version.
            else if (!strstr(err, "Save was made by a newer version (v")) expect = "Save was made by a newer version (v...)";
            if (expect && !str_eq(err, expect)) {
                if (wrongMessage++ < 3) out("    byte %d mask 0x%x: \"%s\"\n", i, (unsigned)masks[m], err);
            }
        }
    }
    if (memcmp(snapPd, &g.pd, sizeof(PlayerData)) != 0 || !vec_near(snapPos, g.actors[PLAYER].pos, 0)) changedState++;
    CHECK_INT(accepted, 0);
    CHECK_INT(wrongMessage, 0);
    CHECK_INT(changedState, 0);
    CHECK(load_save(s, err, sizeof(err)));  // the pristine copy still loads
    s.free_mem();
    bad.free_mem();
}

static void test_save_reject_future_version() {
    setup_known_state();
    Array<u8> s = {};
    save_serialize(&s);
    g.pd.money = 4242;
    char err[128];
    u32 v = 2;
    memcpy(s.data + 4, &v, 4);  // header is not covered by the CRC, so the payload still checks out
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save was made by a newer version (v2)");
    v = 0xFFFFFFFFu;
    memcpy(s.data + 4, &v, 4);
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save was made by a newer version (v4294967295)");
    v = 0;
    memcpy(s.data + 4, &v, 4);
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (invalid version)");
    // Integrity is checked before the version: a damaged "newer" save reports the damage.
    v = 2;
    memcpy(s.data + 4, &v, 4);
    s.count -= 1;
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (truncated or padded)");
    s.count += 1;
    s.data[s.count - 1] ^= 0x22;
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (checksum mismatch)");
    CHECK_INT(g.pd.money, 4242);
    s.free_mem();
}

static void test_save_reject_malformed_fields() {
    reset_game();
    g.pd.money = 4242;
    Array<u8> payload = {}, s = {};
    char err[128];
    // Field length runs past the end of the payload.
    int money = 5;
    put_u16(&payload, TSF_MONEY);
    put_u32(&payload, 1000);
    bytes_append(&payload, &money, 4);
    make_save(&s, 1, payload);
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save field 1 is malformed");
    // Flag count above MAX_FLAGS.
    payload.clear();
    Array<u8> fl = {};
    put_u32(&fl, MAX_FLAGS + 1);
    fl.resize(4 + (MAX_FLAGS + 1) * 28);
    add_field(&payload, TSF_FLAGS, fl.data, (u32)fl.count);
    make_save(&s, 1, payload);
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save flags are malformed");
    // Flag count larger than the field.
    payload.clear();
    fl.clear();
    put_u32(&fl, 3);
    fl.resize(4 + 28);
    add_field(&payload, TSF_FLAGS, fl.data, (u32)fl.count);
    make_save(&s, 1, payload);
    CHECK(!load_save(s, err, sizeof(err)));
    CHECK_STR(err, "Save flags are malformed");
    CHECK_INT(g.pd.money, 4242);
    // A flags field shorter than its count word is ignored, not fatal.
    payload.clear();
    u16 two = 0;
    add_field(&payload, TSF_FLAGS, &two, 2);
    make_save(&s, 1, payload);
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(g.pd.flagCount, 0);
    payload.free_mem();
    fl.free_mem();
    s.free_mem();
}

static void test_save_rejection_keeps_state() {
    setup_known_state();
    Array<u8> good = {}, bad = {};
    save_serialize(&good);
    static u8 snapPd[sizeof(PlayerData)], snapActor[sizeof(Actor)];
    memcpy(snapPd, &g.pd, sizeof(PlayerData));
    memcpy(snapActor, &g.actors[PLAYER], sizeof(Actor));
    char err[128];
    int changed = 0, cases = 0;
    for (int c = 0; c < 6; c++) {
        copy_array(&bad, good);
        switch (c) {
        case 0: bad.count = 10; break;                    // too small
        case 1: bad.data[0] = 'X'; break;                 // magic
        case 2: bad.data[4] = 9; break;                   // version
        case 3: bad.count -= 1; break;                    // truncated
        case 4: bad.data[bad.count - 3] ^= 0x10; break;   // CRC
        case 5: {                                         // malformed flags with a valid CRC
            Array<u8> payload = {};
            u32 n = 1000;
            add_field(&payload, TSF_MONEY, &n, 4);
            add_field(&payload, TSF_FLAGS, &n, 4);
            make_save(&bad, 1, payload);
            payload.free_mem();
        } break;
        }
        cases++;
        if (load_save(bad, err, sizeof(err))) changed += 100;
        if (memcmp(snapPd, &g.pd, sizeof(PlayerData)) != 0 || memcmp(snapActor, &g.actors[PLAYER], sizeof(Actor)) != 0 ||
            g.power || g.rainTarget != 0.35f) {
            changed++;
            out("    case %d modified the game state (%s)\n", c, err);
        }
    }
    CHECK_INT(cases, 6);
    CHECK_INT(changed, 0);
    good.free_mem();
    bad.free_mem();
}

static void test_save_clamps_out_of_range() {
    char err[128];
    Array<u8> s = {};
    // Run A: values above / below every limit.
    setup_known_state();
    PlayerData& pd = g.pd;
    pd.money = -5;
    pd.phase = 9;
    pd.reserve[W_PISTOL] = 99999;
    pd.reserve[W_SMG] = -3;
    pd.clip[W_PISTOL] = 99;
    pd.clip[W_LANCE] = -1;
    pd.upgrades[0] = 7;
    pd.upgrades[1] = -2;
    pd.rep[FAC_POLICE] = 500;
    pd.rep[FAC_KINGS] = -500;
    pd.playTime = -1.0f;
    pd.distanceDriven = NAN;
    pd.hasWeapon[W_FISTS] = false;
    pd.hasWeapon[W_CLAWS] = true;
    Actor& p = g.actors[PLAYER];
    p.active = false;
    p.weapon = W_CLAWS;  // enemy-only weapon
    p.health = 0.0f;
    p.armor = 250.0f;
    p.pos = vec3(900, 0, 0);
    g.rainTarget = 3.0f;
    save_serialize(&s);
    clobber_state();
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(pd.money, 0);
    CHECK_INT(pd.phase, 3);
    CHECK_INT(pd.reserve[W_PISTOL], g_weapons[W_PISTOL].maxReserve);
    CHECK_INT(pd.reserve[W_SMG], 0);
    CHECK_INT(pd.clip[W_PISTOL], g_weapons[W_PISTOL].clipSize);
    CHECK_INT(pd.clip[W_LANCE], 0);
    CHECK_INT(pd.upgrades[0], 3);
    CHECK_INT(pd.upgrades[1], 0);
    CHECK_INT(pd.rep[FAC_POLICE], 100);
    CHECK_INT(pd.rep[FAC_KINGS], -100);
    CHECK_NEAR(pd.playTime, 0, 0);
    CHECK_NEAR(pd.distanceDriven, 0, 0);
    CHECK(pd.hasWeapon[W_FISTS]);  // fists are always owned
    CHECK_INT(p.weapon, W_FISTS);
    CHECK_INT(p.clipAmmo, 0);
    CHECK_NEAR(p.health, 100, 0);
    CHECK_NEAR(p.armor, 100, 0);
    CHECK_VEC(p.pos, STUB_LANDMARK_POS, 0);  // out-of-world position: respawn at the bus stop
    CHECK_INT(g_stubLandmarkId, LM_BUS_STOP);
    CHECK_NEAR(p.yaw, PI_F, 0);
    CHECK_NEAR(g.rainTarget, 1.0, 0);
    // Run B: the other side of each limit.
    setup_known_state();
    pd.money = 2000000000;
    pd.phase = -1;
    p.weapon = W_RIFLE;  // not owned
    p.health = 1000.0f;
    p.armor = -5.0f;
    p.pos = vec3(0, -200, 0);
    g.rainTarget = -1.0f;
    g.power = true;
    save_serialize(&s);
    clobber_state();
    g.power = false;
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(pd.money, 100000000);
    CHECK_INT(pd.phase, 0);
    CHECK_INT(p.weapon, W_FISTS);
    CHECK_NEAR(p.health, 100, 0);
    CHECK_NEAR(p.armor, 0, 0);
    CHECK_VEC(p.pos, STUB_LANDMARK_POS, 0);
    CHECK_NEAR(g.rainTarget, 0.0, 0);
    CHECK(g.power);
    // Run C: hand-made fields with values the serialiser cannot produce.
    Array<u8> payload = {};
    int w = -1;
    float pos[4] = {NAN, 0, 0, 0};
    float st[13] = {};
    st[9] = 2e8f;  // playTime beyond the plausible range
    add_field(&payload, TSF_WEAPON, &w, 4);
    add_field(&payload, TSF_PLAYER_POS, pos, 16);
    add_field(&payload, TSF_STATS, st, sizeof(st));
    make_save(&s, 1, payload);
    clobber_state();
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(p.weapon, W_FISTS);
    CHECK_VEC(p.pos, STUB_LANDMARK_POS, 0);
    CHECK_NEAR(pd.playTime, 0, 0);
    payload.clear();
    w = W_COUNT + 5;
    add_field(&payload, TSF_WEAPON, &w, 4);
    make_save(&s, 1, payload);
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(p.weapon, W_FISTS);
    payload.free_mem();
    s.free_mem();
}

static void test_save_defaults_unknown_fields() {
    char err[128];
    Array<u8> payload = {}, s = {};
    // Empty payload: everything takes its default.
    make_save(&s, 1, payload);
    clobber_state();
    CHECK(load_save(s, err, sizeof(err)));
    const PlayerData& pd = g.pd;
    const Actor& p = g.actors[PLAYER];
    CHECK_INT(pd.money, 0);
    CHECK_INT(pd.flagCount, 0);
    CHECK(pd.hasWeapon[W_FISTS] && !pd.hasWeapon[W_PISTOL]);
    CHECK_NEAR(p.health, 100, 0);
    CHECK_NEAR(p.armor, 0, 0);
    CHECK_INT(p.weapon, W_FISTS);
    CHECK_VEC(p.pos, STUB_LANDMARK_POS, 0);
    CHECK_NEAR(p.yaw, PI_F, 0);
    CHECK(g.power);
    CHECK_NEAR(g.rainTarget, 0.7f, 0);
    // An older / newer save: known fields load, unknown ids are skipped, short fields ignored.
    int money = 500, phase = 1;
    u8 unknown[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    u16 shortMoney = 7;
    float shortPos[3] = {1, 2, 3};
    add_field(&payload, 200, unknown, sizeof(unknown));
    add_field(&payload, TSF_MONEY, &money, 4);
    add_field(&payload, 0, nullptr, 0);
    add_field(&payload, TSF_PHASE, &phase, 4);
    add_field(&payload, TSF_MISSIONS, &shortMoney, 2);  // too short: ignored
    add_field(&payload, TSF_PLAYER_POS, shortPos, 12);  // too short: ignored
    add_field(&payload, 0xFFFF, unknown, 3);
    bytes_append(&payload, "xyz", 3);  // trailing bytes shorter than a field header are ignored
    make_save(&s, 1, payload);
    clobber_state();
    CHECK(load_save(s, err, sizeof(err)));
    CHECK_INT(pd.money, 500);
    CHECK_INT(pd.phase, 1);
    CHECK_INT(pd.missionsDone, 0);
    CHECK_VEC(p.pos, STUB_LANDMARK_POS, 0);
    payload.free_mem();
    s.free_mem();
}

static void test_save_flag_get_set() {
    reset_game();
    CHECK_INT(flag_get("missing"), 0);
    flag_set("x", 5);
    CHECK_INT(flag_get("x"), 5);
    flag_set("x", 7);  // overwrite in place
    CHECK_INT(flag_get("x"), 7);
    CHECK_INT(g.pd.flagCount, 1);
    flag_set("y", -3);
    CHECK_INT(flag_get("y"), -3);
    CHECK_INT(flag_get("x"), 7);
    flag_set("x", 0);  // zero keeps the slot
    CHECK_INT(g.pd.flagCount, 2);
    CHECK_INT(flag_get("x"), 0);
    CHECK_INT(flag_get("X"), 0);  // case sensitive
    flag_set("abcdefghijklmnopqrstuvw", 9);  // 23 chars: the longest name that fits
    CHECK_INT(flag_get("abcdefghijklmnopqrstuvw"), 9);
    char name[24];
    for (int i = g.pd.flagCount; i < MAX_FLAGS; i++) {
        fmt_str(name, sizeof(name), "fill_%d", i);
        flag_set(name, i);
    }
    CHECK_INT(g.pd.flagCount, MAX_FLAGS);
    flag_set("overflow", 1);  // table full: silently dropped
    CHECK_INT(g.pd.flagCount, MAX_FLAGS);
    CHECK_INT(flag_get("overflow"), 0);
    flag_set("y", 11);  // existing flags still update when full
    CHECK_INT(flag_get("y"), 11);
}

// Known bug: names of 24+ characters are truncated when stored but compared in full,
// so flag_get never finds them and every flag_set appends a duplicate (save.cpp:206-219).
static void test_save_flag_long_names() {
    reset_game();
    const char* longName = "a_flag_name_longer_than_23";
    flag_set(longName, 1);
    CHECK_INT(flag_get(longName), 1);
    flag_set(longName, 2);
    CHECK_INT(flag_get(longName), 2);
    CHECK_INT(g.pd.flagCount, 1);
}

// Known bug: junk after a valid magic is reported as "made by a newer version" because the
// version field is trusted before the size and CRC are validated (save.cpp:105).
static void test_save_junk_after_magic_message() {
    reset_game();
    u8 junk[64];
    Rng r;
    r.seed(15);
    for (int i = 0; i < 64; i++) junk[i] = (u8)r.next();
    memcpy(junk, "FGTO", 4);
    char err[128] = "";
    bool ok = save_deserialize(junk, (int)sizeof(junk), err, sizeof(err));
    out("    observed: accepted=%d message=\"%s\"\n", (int)ok, err);
    CHECK(!ok);
    CHECK(strstr(err, "newer version") == nullptr);
    CHECK(strstr(err, "truncated") != nullptr || strstr(err, "corrupted") != nullptr);
}

// Known bug: the header is not covered by the CRC, so a corrupted version field of an
// otherwise intact save is reported as "made by a newer version" (v0 or v254 here).
static void test_save_flipped_version_message() {
    setup_known_state();
    Array<u8> s = {};
    save_serialize(&s);
    char err[128];
    const u8 masks[2] = {0x01, 0xFF};
    for (int m = 0; m < 2; m++) {
        s.data[4] ^= masks[m];
        bool ok = load_save(s, err, sizeof(err));
        s.data[4] ^= masks[m];
        out("    observed (byte 4 ^ 0x%x): \"%s\"\n", (unsigned)masks[m], err);
        CHECK(!ok);
        CHECK_CONTAINS(err, "corrupted");
    }
    s.free_mem();
}

// Known bug: yaw, armor and the rain target are loaded without a finite check, so a NaN /
// infinity stored in a save (with a valid CRC) reaches the game state (save.cpp:195-202).
static void test_save_nan_fields_sanitized() {
    setup_known_state();
    g.actors[PLAYER].yaw = NAN;
    g.actors[PLAYER].armor = NAN;
    g.rainTarget = NAN;
    Array<u8> s = {};
    save_serialize(&s);
    clobber_state();
    char err[128];
    CHECK(load_save(s, err, sizeof(err)));
    out("    observed: yaw finite=%d armor finite=%d rainTarget finite=%d\n", (int)isfinite(g.actors[PLAYER].yaw),
        (int)isfinite(g.actors[PLAYER].armor), (int)isfinite(g.rainTarget));
    CHECK(isfinite(g.actors[PLAYER].yaw));
    CHECK(isfinite(g.actors[PLAYER].armor));
    CHECK(isfinite(g.rainTarget));
    s.free_mem();
}

// Fills the stack region that the next call will use with a byte pattern.
__attribute__((noinline)) static void dirty_stack(u8 v) {
    volatile u8 junk[16384];
    for (int i = 0; i < (int)sizeof(junk); i++) junk[i] = v;
}

// Known bug: save_serialize writes all 64 bytes of a stack buffer of which fmt_str only
// initialises the summary string, so stale stack bytes end up in every save (save.cpp:78-80).
static void test_save_serialize_deterministic() {
    setup_known_state();
    Array<u8> a = {}, b = {};
    dirty_stack(0xA5);
    save_serialize(&a);
    dirty_stack(0x5A);
    save_serialize(&b);
    CHECK(a.count == b.count && memcmp(a.data, b.data, (size_t)a.count) == 0);
    u32 len = 0;
    const u8* info = find_field(a, TSF_INFO, &len);
    int nonZeroTail = 0;
    if (CHECK(info != nullptr)) {
        int end = (int)strnlen((const char*)info, len);
        for (int i = end; i < (int)len; i++) nonZeroTail += info[i] != 0;
        out("    observed: %d of %d bytes after the summary string are non-zero\n", nonZeroTail, (int)len - end);
    }
    CHECK_INT(nonZeroTail, 0);
    a.free_mem();
    b.free_mem();
}

// ===========================================================================
// src/platform/platform_desktop_common
// ===========================================================================
static const char* g_argv0 = "fighto_tests";
static char g_tmpBase[512];
static char g_userDirPath[600];
static int g_userDirState;  // 0 not tried, 1 ready, -1 failed

static bool path_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void user_full_path(char* out, int cap, const char* name) { fmt_str(out, cap, "%s/%s", g_userDirPath, name); }

// Points FIGHTO_USERDIR at a fresh temporary directory and runs the real path setup.
static bool ensure_user_dir() {
    if (g_userDirState) return g_userDirState > 0;
    g_userDirState = -1;
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !tmp[0]) tmp = "/tmp";
    fmt_str(g_tmpBase, sizeof(g_tmpBase), "%s/fighto_tests_XXXXXX", tmp);
    if (!mkdtemp(g_tmpBase)) {
        out("    mkdtemp failed for %s\n", g_tmpBase);
        g_tmpBase[0] = 0;
        return false;
    }
    // A nested path checks that desktop_init_paths creates the whole directory chain.
    fmt_str(g_userDirPath, sizeof(g_userDirPath), "%s/nested/user", g_tmpBase);
    if (setenv("FIGHTO_USERDIR", g_userDirPath, 1) != 0) return false;
    desktop_init_paths(g_argv0);
    struct stat st;
    if (stat(g_userDirPath, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    g_userDirState = 1;
    return true;
}

static void cleanup_user_dir() {
    if (!g_tmpBase[0]) return;
    DIR* d = opendir(g_userDirPath);
    if (d) {
        while (struct dirent* e = readdir(d)) {
            if (str_eq(e->d_name, ".") || str_eq(e->d_name, "..")) continue;
            char p[1200];
            fmt_str(p, sizeof(p), "%s/%s", g_userDirPath, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    char nested[600];
    fmt_str(nested, sizeof(nested), "%s/nested", g_tmpBase);
    rmdir(g_userDirPath);
    rmdir(nested);
    if (rmdir(g_tmpBase) != 0) out("note: could not remove temporary directory %s\n", g_tmpBase);
}

static void test_plat_user_dir_init() {
    CHECK(ensure_user_dir());
    CHECK(path_exists(g_userDirPath));
    CHECK(!plat_is_mobile());
    // Files written through the API land in FIGHTO_USERDIR.
    CHECK(plat_write_user_file("probe.txt", "x", 1));
    char p[700];
    user_full_path(p, sizeof(p), "probe.txt");
    CHECK(path_exists(p));
}

static void test_plat_user_file_round_trip() {
    if (!CHECK(ensure_user_dir())) return;
    Array<u8> blob = {}, rd = {};
    blob.resize(3000);
    Rng r;
    r.seed(99);
    for (int i = 0; i < blob.count; i++) blob[i] = (u8)r.next();
    blob[0] = 0;
    blob[1] = 0xFF;
    blob[1500] = 0;
    blob[2999] = '\n';
    CHECK(!plat_user_file_exists("blob.bin"));
    CHECK(plat_write_user_file("blob.bin", blob.data, (size_t)blob.count));
    CHECK(plat_user_file_exists("blob.bin"));
    CHECK(plat_read_user_file("blob.bin", &rd));
    CHECK_INT(rd.count, 3000);
    CHECK(rd.count == blob.count && memcmp(rd.data, blob.data, (size_t)blob.count) == 0);
    CHECK(rd.cap > rd.count && rd.data[rd.count] == 0);  // reads append a NUL after the data
    char tmpPath[700];
    user_full_path(tmpPath, sizeof(tmpPath), "blob.bin.tmp");
    CHECK(!path_exists(tmpPath));  // the temp file of the atomic write is renamed away
    // Overwrite with shorter content.
    CHECK(plat_write_user_file("blob.bin", "BB", 2));
    CHECK(plat_read_user_file("blob.bin", &rd));
    CHECK_INT(rd.count, 2);
    CHECK(memcmp(rd.data, "BB", 2) == 0);
    // Text round trip; the terminator makes the buffer usable as a C string.
    const char* text = "hello user dir\nline 2\n";
    CHECK(plat_write_user_file("text.txt", text, strlen(text)));
    CHECK(plat_read_user_file("text.txt", &rd));
    CHECK_STR((const char*)rd.data, text);
    // Empty file.
    CHECK(plat_write_user_file("empty.bin", "", 0));
    CHECK(plat_user_file_exists("empty.bin"));
    CHECK(plat_read_user_file("empty.bin", &rd));
    CHECK_INT(rd.count, 0);
    // Missing file.
    Array<u8> none = {};
    CHECK(!plat_user_file_exists("missing.bin"));
    CHECK(!plat_read_user_file("missing.bin", &none));
    CHECK_INT(none.count, 0);
    blob.free_mem();
    rd.free_mem();
    none.free_mem();
}

static void test_plat_copy_user_file() {
    if (!CHECK(ensure_user_dir())) return;
    u8 data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (u8)(i * 7);
    CHECK(plat_write_user_file("src.bin", data, sizeof(data)));
    CHECK(plat_copy_user_file("src.bin", "copy.bin"));
    Array<u8> rd = {};
    CHECK(plat_read_user_file("copy.bin", &rd));
    CHECK(rd.count == 1000 && memcmp(rd.data, data, 1000) == 0);
    CHECK(plat_write_user_file("src2.bin", "second", 6));
    CHECK(plat_copy_user_file("src2.bin", "copy.bin"));  // replaces an existing file
    CHECK(plat_read_user_file("copy.bin", &rd));
    CHECK(rd.count == 6 && memcmp(rd.data, "second", 6) == 0);
    CHECK(plat_copy_user_file("src.bin", "src.bin"));  // onto itself: content unchanged
    CHECK(plat_read_user_file("src.bin", &rd));
    CHECK(rd.count == 1000 && memcmp(rd.data, data, 1000) == 0);
    CHECK(!plat_copy_user_file("missing.bin", "never.bin"));
    CHECK(!plat_user_file_exists("never.bin"));
    rd.free_mem();
}

static void test_plat_write_failure() {
    if (!CHECK(ensure_user_dir())) return;
    out("    (an expected \"Cannot open\" error is logged below)\n");
    CHECK(!plat_write_user_file("no_such_dir/file.bin", "x", 1));
    CHECK(!plat_user_file_exists("no_such_dir/file.bin"));
    Array<u8> rd = {};
    CHECK(!plat_read_user_file("no_such_dir/file.bin", &rd));
    rd.free_mem();
}

static void test_plat_save_end_to_end() {
    if (!CHECK(ensure_user_dir())) return;
    setup_known_state();
    Array<u8> s = {}, rd = {};
    save_serialize(&s);
    CHECK(plat_write_user_file("slot_test.sav", s.data, (size_t)s.count));
    clobber_state();
    CHECK(plat_read_user_file("slot_test.sav", &rd));
    char err[128];
    CHECK(load_save(rd, err, sizeof(err)));
    CHECK_INT(g.pd.money, 123456);
    CHECK_INT(flag_get("thorne_dead"), 1);
    CHECK_VEC(g.actors[PLAYER].pos, vec3(10.5f, 2.25f, -30.75f), 0);
    // Corruption on disk is detected on the next load.
    rd.data[rd.count / 2] ^= 0x40;
    CHECK(plat_write_user_file("slot_test.sav", rd.data, (size_t)rd.count));
    CHECK(plat_read_user_file("slot_test.sav", &rd));
    CHECK(!load_save(rd, err, sizeof(err)));
    CHECK_STR(err, "Save file is corrupted (checksum mismatch)");
    s.free_mem();
    rd.free_mem();
}

// ===========================================================================
// Registry and driver
// ===========================================================================
struct TestCase {
    const char* name;
    void (*fn)();
    const char* knownBug;  // non-null: documents a defect still present in the code
};

static const TestCase g_tests[] = {
    {"base_str_copy", test_base_str_copy, nullptr},
    {"base_str_eq_starts", test_base_str_eq_starts, nullptr},
    {"base_str_to_int", test_base_str_to_int, nullptr},
    {"base_str_to_float", test_base_str_to_float, nullptr},
    {"base_fmt_integers", test_base_fmt_integers, nullptr},
    {"base_fmt_strings", test_base_fmt_strings, nullptr},
    {"base_fmt_floats", test_base_fmt_floats, nullptr},
    {"base_fmt_width_padding", test_base_fmt_width_padding, nullptr},
    {"base_fmt_truncation_return", test_base_fmt_truncation_return, nullptr},
    {"base_fmt_negative_space_padding", test_base_fmt_negative_space_padding,
     "fmt_strv emits '-' before space padding: %5d of -42 gives \"-  42\" (src/core/base.cpp:40,68)"},
    {"base_fmt_trailing_percent", test_base_fmt_trailing_percent,
     "fmt_strv reads past the terminator when the format ends in a lone '%' (src/core/base.cpp:36)"},
    {"base_hash_str", test_base_hash_str, nullptr},
    {"base_crc32", test_base_crc32, nullptr},
    {"base_array_push_reserve", test_base_array_push_reserve, nullptr},
    {"base_array_resize", test_base_array_resize, nullptr},
    {"base_array_remove", test_base_array_remove, nullptr},
    {"base_array_misc", test_base_array_misc, nullptr},
    {"base_rng", test_base_rng, nullptr},
    {"vmath_quat_vs_matrix", test_vmath_quat_vs_matrix, nullptr},
    {"vmath_quat_compose_conj", test_vmath_quat_compose_conj, nullptr},
    {"vmath_mat4_inverse", test_vmath_mat4_inverse, nullptr},
    {"vmath_mat4_trs", test_vmath_mat4_trs, nullptr},
    {"vmath_normalize", test_vmath_normalize, nullptr},
    {"vmath_wrap_angle", test_vmath_wrap_angle, nullptr},
    {"vmath_dir_from_yaw", test_vmath_dir_from_yaw, nullptr},
    {"vmath_reflect", test_vmath_reflect, nullptr},
    {"vmath_slerp_from_to", test_vmath_slerp_from_to, nullptr},
    {"vmath_projection", test_vmath_projection, nullptr},
    {"vmath_frustum", test_vmath_frustum, nullptr},
    {"vmath_ray_queries", test_vmath_ray_queries, nullptr},
    {"noise_determinism", test_noise_determinism, nullptr},
    {"noise_ranges", test_noise_ranges, nullptr},
    {"noise_tiling", test_noise_tiling, nullptr},
    {"noise_lattice_continuity", test_noise_lattice_continuity, nullptr},
    {"script_basic_tokens", test_script_basic_tokens, nullptr},
    {"script_quotes", test_script_quotes, nullptr},
    {"script_comments_lines", test_script_comments_lines, nullptr},
    {"script_crlf", test_script_crlf, nullptr},
    {"script_max_args", test_script_max_args, nullptr},
    {"script_long_line", test_script_long_line, nullptr},
    {"script_accessors", test_script_accessors, nullptr},
    {"save_round_trip", test_save_round_trip, nullptr},
    {"save_flags_round_trip", test_save_flags_round_trip, nullptr},
    {"save_reject_garbage", test_save_reject_garbage, nullptr},
    {"save_reject_wrong_magic", test_save_reject_wrong_magic, nullptr},
    {"save_reject_truncated_padded", test_save_reject_truncated_padded, nullptr},
    {"save_reject_flipped_bytes", test_save_reject_flipped_bytes, nullptr},
    {"save_reject_future_version", test_save_reject_future_version, nullptr},
    {"save_reject_malformed_fields", test_save_reject_malformed_fields, nullptr},
    {"save_rejection_keeps_state", test_save_rejection_keeps_state, nullptr},
    {"save_clamps_out_of_range", test_save_clamps_out_of_range, nullptr},
    {"save_defaults_unknown_fields", test_save_defaults_unknown_fields, nullptr},
    {"save_flag_get_set", test_save_flag_get_set, nullptr},
    {"save_flag_long_names", test_save_flag_long_names,
     "flag names of 24+ chars are stored truncated but compared in full: lookups fail, duplicates pile up (src/game/save.cpp:206-219)"},
    {"save_junk_after_magic_message", test_save_junk_after_magic_message,
     "version is checked before size/CRC, so junk after \"FGTO\" reads as \"made by a newer version\" (src/game/save.cpp:105)"},
    {"save_flipped_version_message", test_save_flipped_version_message,
     "the header is outside the CRC: a corrupted version byte reads as \"newer version (v0/v254)\" (src/game/save.cpp:90,105)"},
    {"save_nan_fields_sanitized", test_save_nan_fields_sanitized,
     "yaw, armor and rainTarget are restored without a finite check (src/game/save.cpp:195-202)"},
    {"save_serialize_deterministic", test_save_serialize_deterministic,
     "SF_INFO writes 64 bytes of a partly uninitialised stack buffer into every save (src/game/save.cpp:78-80)"},
    {"plat_user_dir_init", test_plat_user_dir_init, nullptr},
    {"plat_user_file_round_trip", test_plat_user_file_round_trip, nullptr},
    {"plat_copy_user_file", test_plat_copy_user_file, nullptr},
    {"plat_write_failure", test_plat_write_failure, nullptr},
    {"plat_save_end_to_end", test_plat_save_end_to_end, nullptr},
};

int main(int argc, char** argv) {
    if (argc > 0 && argv[0]) g_argv0 = argv[0];
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, passed = 0, failed = 0, xfail = 0, xpass = 0;
    for (int i = 0; i < ARRAY_COUNT(g_tests); i++) {
        const TestCase& t = g_tests[i];
        if (filter && !strstr(t.name, filter)) continue;
        run++;
        g_checkFails = 0;
        g_reportCount = 0;
        int before = g_checkCount;
        t.fn();
        fflush(stderr);
        int checks = g_checkCount - before;
        bool ok = g_checkFails == 0 && checks > 0;  // a test that checks nothing is a failure
        if (!t.knownBug) {
            if (ok) {
                passed++;
                out("PASS  %s (%d checks)\n", t.name, checks);
            } else {
                failed++;
                out("FAIL  %s (%d of %d checks failed)\n", t.name, g_checkFails, checks);
            }
        } else if (ok) {
            xpass++;
            out("XPASS %s -- known bug no longer reproduces, promote to a regular test: %s\n", t.name, t.knownBug);
        } else {
            xfail++;
            out("XFAIL %s -- known bug: %s\n", t.name, t.knownBug);
        }
        fflush(stdout);
    }
    cleanup_user_dir();
    if (run == 0) {
        out("No test name contains \"%s\"\n", filter ? filter : "");
        return 2;
    }
    out("\nSUMMARY: %d tests run: %d passed, %d failed, %d known bugs (XFAIL), %d fixed known bugs (XPASS); %d checks\n",
        run, passed, failed, xfail, xpass, g_checkCount);
    out("RESULT: %s\n", failed ? "FAILURE" : "SUCCESS");
    return failed ? 1 : 0;
}
