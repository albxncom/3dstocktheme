/* For host/test_main_badges.c and host/test_design.c only (their Makefile rules put this folder first on the include
 * path): test_main.c, which those files include, ends with its own main and includes <zlib.h> after its "#undef
 * main"; here that main is renamed, so they can have their own and reuse all of test_main.c's simulated console. Way
 * out: once test_main.c's fakes and helpers live in a header of their own, include that and delete this folder. */
#include_next <zlib.h>
#ifndef main
#define main test_main_main
#endif
