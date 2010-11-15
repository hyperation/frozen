#include <test_10_buffer.c>
#include <test_20_data.c>
#include <test_21_data_integer.c>
#include <test_40_hash.c>
#include <test_70_backend.c>
#include <test_71_backend_file.c>
#include <test_72_backend_nullproxy.c>
#include <test_74_backend_list.c>

void test_list(void){
	tcase_add_test(tc_core, test_buffer);
	tcase_add_test(tc_core, test_data);
	tcase_add_test(tc_core, test_data_integer);
	tcase_add_test(tc_core, test_hash);
	tcase_add_test(tc_core, test_backends);
	tcase_add_test(tc_core, test_backend_file);
	tcase_add_test(tc_core, test_backends_two_chains);
	tcase_add_test(tc_core, test_backend_list);
}
