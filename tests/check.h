/* Minimal test helpers: CHECK records a failure and carries on. */
#ifndef ED_CHECK_H
#define ED_CHECK_H

#include <stdio.h>

extern int ed_checks, ed_failures;

#define CHECK(cond)                                                              \
	do {                                                                     \
		ed_checks++;                                                     \
		if (!(cond)) {                                                   \
			ed_failures++;                                           \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                \
	} while (0)

void test_frame(void);
void test_model(void);
void test_decode(void);

#endif
