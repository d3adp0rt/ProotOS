/*
 * ProotOS — host-side unit test entry point.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_strings(void) {
    char buf[8];
    memset(buf, 'x', sizeof(buf));
    assert(buf[0] == 'x' && buf[7] == 'x');
    memcpy(buf, "abc", 3);
    assert(buf[0] == 'a' && buf[2] == 'c');
    assert(strlen("hello") == 5);
    assert(strcmp("a", "a") == 0);
}

int main(void) {
    test_strings();
    puts("prootos_unit_tests: ok");
    return 0;
}
