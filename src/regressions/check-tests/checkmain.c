#include <stdlib.h>
#include <check.h>

static void setup(void)
{
    //int rc = system("DEBUG=5 FLAVOR=posix realtime start");
    //ck_assert_int_eq(rc, 0);
    return;
}


static void teardown(void)
{
    mark_point();
    //int rc = system("DEBUG=5 FLAVOR=posix realtime stop");
    // ck_assert_int_eq(rc, 0);
    return;
}

#include "helloworld-suite.h"
#include "machinetalk-suite.h"
#include "hal-suite.h"
#include "atomic-suite.h"



int main(int argc, char **argv)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = machinetalk_suite();
    sr = srunner_create(s);
    srunner_add_suite(sr, hal_suite());
    srunner_add_suite(sr, atomic_suite());
    srunner_add_suite(sr, hello_world_suite());

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
