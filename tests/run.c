/* Runs every suite. Hermetic: no Bluetooth, no network, no files written. */
#include "check.h"

int ed_checks, ed_failures;

int main(void)
{
	test_frame();
	test_model();
	test_decode();
	printf("%d checks, %d failures\n", ed_checks, ed_failures);
	return ed_failures == 0 ? 0 : 1;
}
