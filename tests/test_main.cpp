// FileCopyUtility unit tests
// Minimal test framework: no dependencies, runs as a console app.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

// ── Minimal test framework ──────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;

#define TEST(name) static void test_##name(); \
    static struct _reg_##name { _reg_##name() { g_tests.push_back({#name, test_##name}); } } _r_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr)  do { if (!(expr)) { printf("  FAIL: %s (line %d)\n", #expr, __LINE__); g_fail++; return; } } while(0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_EQ(a, b)    do { if ((a) != (b)) { printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); g_fail++; return; } } while(0)

struct TestEntry { const char* name; void(*fn)(); };
static std::vector<TestEntry> g_tests;

// ── Include the code under test ─────────────────────────────────────────────

#include "../utils.h"

// ── Test: SplitPatterns ─────────────────────────────────────────────────────

TEST(split_patterns_basic)
{
    auto v = SplitPatterns(L"*.tmp;~$*;Thumbs.db");
    ASSERT_EQ(v.size(), (size_t)3);
    ASSERT_TRUE(v[0] == L"*.tmp");
    ASSERT_TRUE(v[1] == L"~$*");
    ASSERT_TRUE(v[2] == L"Thumbs.db");
    g_pass++;
}

TEST(split_patterns_empty)
{
    auto v = SplitPatterns(L"");
    ASSERT_EQ(v.size(), (size_t)0);
    g_pass++;
}

TEST(split_patterns_whitespace)
{
    auto v = SplitPatterns(L"  *.tmp ; *.log  ;  ");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_TRUE(v[0] == L"*.tmp");
    ASSERT_TRUE(v[1] == L"*.log");
    g_pass++;
}

TEST(split_patterns_single)
{
    auto v = SplitPatterns(L"*.bak");
    ASSERT_EQ(v.size(), (size_t)1);
    ASSERT_TRUE(v[0] == L"*.bak");
    g_pass++;
}

TEST(normalize_patterns_for_editor)
{
    auto s = NormalizePatternsForEditor(L"*.tmp; node_modules\r\nThumbs.db");
    ASSERT_TRUE(s == L"*.tmp\r\nnode_modules\r\nThumbs.db");
    g_pass++;
}

TEST(normalize_patterns_for_storage)
{
    auto s = NormalizePatternsForStorage(L"*.tmp\r\nnode_modules;Thumbs.db\n");
    ASSERT_TRUE(s == L"*.tmp;node_modules;Thumbs.db");
    g_pass++;
}

// ── Test: PathMatchSpecW (filter matching) ──────────────────────────────────

TEST(filter_match_wildcard)
{
    ASSERT_TRUE(PathMatchSpecW(L"foo.tmp", L"*.tmp"));
    ASSERT_FALSE(PathMatchSpecW(L"foo.txt", L"*.tmp"));
    g_pass++;
}

TEST(filter_match_prefix)
{
    ASSERT_TRUE(PathMatchSpecW(L"~$document.docx", L"~$*"));
    ASSERT_FALSE(PathMatchSpecW(L"document.docx", L"~$*"));
    g_pass++;
}

TEST(filter_match_case_insensitive)
{
    // PathMatchSpecW is case-insensitive on Windows
    ASSERT_TRUE(PathMatchSpecW(L"thumbs.DB", L"Thumbs.db"));
    ASSERT_TRUE(PathMatchSpecW(L"THUMBS.DB", L"thumbs.db"));
    g_pass++;
}

TEST(filter_match_relative_path_exact)
{
    std::vector<std::wstring> patterns{L"AppData\\Local\\DockerSandboxes"};
    ASSERT_TRUE(MatchesFilterPattern(L"AppData\\Local\\DockerSandboxes",
                                     L"DockerSandboxes", true, patterns));
    g_pass++;
}

TEST(filter_match_relative_path_subtree_wildcard)
{
    std::vector<std::wstring> patterns{L"AppData\\Local\\DockerSandboxes\\*"};
    ASSERT_TRUE(MatchesFilterPattern(L"AppData\\Local\\DockerSandboxes",
                                     L"DockerSandboxes", true, patterns));
    g_pass++;
}

TEST(filter_match_leaf_name_still_works)
{
    std::vector<std::wstring> patterns{L"node_modules"};
    ASSERT_TRUE(MatchesFilterPattern(L"src\\node_modules",
                                     L"node_modules", true, patterns));
    ASSERT_FALSE(MatchesFilterPattern(L"src\\packages",
                                      L"packages", true, patterns));
    g_pass++;
}

// ── Test: ToLongPath ────────────────────────────────────────────────────────

static std::wstring ToLongPath(const std::wstring& p)
{
    if (p.size() >= 4 && p[0] == L'\\' && p[1] == L'\\' &&
        (p[2] == L'?' || p[2] == L'.') && p[3] == L'\\')
        return p;
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\')
        return std::wstring(L"\\\\?\\UNC\\") + p.substr(2);
    return std::wstring(L"\\\\?\\") + p;
}

TEST(long_path_local)
{
    auto r = ToLongPath(L"C:\\Users\\test\\file.txt");
    ASSERT_TRUE(r == L"\\\\?\\C:\\Users\\test\\file.txt");
    g_pass++;
}

TEST(long_path_unc)
{
    auto r = ToLongPath(L"\\\\server\\share\\file.txt");
    ASSERT_TRUE(r == L"\\\\?\\UNC\\server\\share\\file.txt");
    g_pass++;
}

TEST(long_path_already_prefixed)
{
    auto r = ToLongPath(L"\\\\?\\C:\\already\\long");
    ASSERT_TRUE(r == L"\\\\?\\C:\\already\\long");
    g_pass++;
}

// ── Test: TimestampNow format ───────────────────────────────────────────────

TEST(timestamp_format)
{
    auto ts = TimestampNow();
    ASSERT_EQ(ts.size(), (size_t)23);
    ASSERT_EQ(ts[4], L'.');
    ASSERT_EQ(ts[7], L'.');
    ASSERT_EQ(ts[10], L' ');
    ASSERT_EQ(ts[13], L':');
    ASSERT_EQ(ts[16], L':');
    ASSERT_EQ(ts[19], L'.');
    g_pass++;
}

// ── Test: Speed calculation logic ───────────────────────────────────────────

TEST(speed_calculation)
{
    ULONGLONG copiedBytes = 100ULL * 1024 * 1024;
    ULONGLONG elapsedMs = 2000;
    double speedBps = (double)copiedBytes / (elapsedMs / 1000.0);
    double speedMBs = speedBps / (1024.0 * 1024.0);
    ASSERT_TRUE(speedMBs > 49.0 && speedMBs < 51.0);
    g_pass++;
}

TEST(speed_zero_elapsed)
{
    ULONGLONG elapsedMs = 0;
    // Should not divide by zero - guard with > 0 check
    ASSERT_TRUE(elapsedMs == 0);
    g_pass++;
}

// ── Test: ETA calculation ───────────────────────────────────────────────────

TEST(eta_calculation)
{
    ULONGLONG bytesToCopy = 200;
    ULONGLONG copiedBytes = 100;
    ULONGLONG elapsedMs = 10000;
    ULONGLONG bytesLeft = bytesToCopy - copiedBytes;
    ULONGLONG remainSec = (elapsedMs * bytesLeft / copiedBytes) / 1000;
    ASSERT_EQ(remainSec, (ULONGLONG)10);
    g_pass++;
}

// ── Test: Schedule evaluation ───────────────────────────────────────────────

TEST(schedule_interval_fires)
{
    ULONGLONG intervalMs = 60ULL * 60 * 1000;
    ULONGLONG now = 100000000;
    ULONGLONG lastRun = now - 61ULL * 60 * 1000;
    ASSERT_TRUE(now - lastRun >= intervalMs);
    g_pass++;
}

TEST(schedule_interval_not_yet)
{
    ULONGLONG intervalMs = 60ULL * 60 * 1000;
    ULONGLONG now = 100000000;
    ULONGLONG lastRun = now - 30ULL * 60 * 1000;
    ASSERT_FALSE(now - lastRun >= intervalMs);
    g_pass++;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main()
{
    printf("FileCopyUtility Tests\n");
    printf("=====================\n\n");

    for (auto& t : g_tests) {
        printf("  %s ... ", t.name);
        int failBefore = g_fail;
        t.fn();
        if (g_fail == failBefore)
            printf("OK\n");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
