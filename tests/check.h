#ifndef COD1RELOADED_TESTS_CHECK_H
#define COD1RELOADED_TESTS_CHECK_H

// A dozen lines of test framework: named groups of checks, a pass/fail count,
// and a non-zero exit when anything fails (ctest reads the exit code).

#include <cstdio>

struct TestRun {
    int pass = 0, fail = 0;
    const char* group = "";
    void check(bool ok, const char* what, const char* file, int line) {
        if (ok) { ++pass; return; }
        ++fail;
        printf("FAIL [%s] %s  (%s:%d)\n", group, what, file, line);
    }
};

extern TestRun g_run;

#define CHECK(cond) g_run.check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) g_run.check((cond), msg, __FILE__, __LINE__)

#endif
