#ifndef _TEST_TRIPLE_BUFFER_SUITE
#define _TEST_TRIPLE_BUFFER_SUITE


// see if this works for us:
// http://remis-thoughts.blogspot.co.at/2012/01/triple-buffering-as-concurrency_30.html
#include <triple-buffer.h>

TB_FLAG_FAST(tb);
int tb_buf[3];

START_TEST(test_triple_buffer)
{
    rtapi_tb_init(&tb);

    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 0);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);  // no new data

    /* Test 1 */
    tb_buf[rtapi_tb_write(&tb)] = 3;
    rtapi_tb_flip_writer(&tb);   // commit 3

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // flipped - new data available
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 3);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);     // no new data

    /* Test 2 */
    tb_buf[rtapi_tb_write(&tb)] = 4;
    rtapi_tb_flip_writer(&tb);                           // commit 4
    tb_buf[rtapi_tb_write(&tb)] = 5;                     // 5 not committed

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 4);     // equals last committed, 4
    rtapi_tb_flip_writer(&tb);                           // commit 5
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 4);     // still 4 since no new snap

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 5);     // must be 5 now since new snap

    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 6;                     // 6 but not committed
    rtapi_tb_flip_writer(&tb);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // must be new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 6);     // must be 6 now since new snap

    tb_buf[rtapi_tb_write(&tb)] = 7;
    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 8;
    rtapi_tb_flip_writer(&tb);
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 6); // must be still 6 since no new snap

    tb_buf[rtapi_tb_write(&tb)] = 7;
    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 8;
    rtapi_tb_flip_writer(&tb);

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // must be new data, 8
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 8);

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);      // no new data, 8
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 8);
}
END_TEST

#define N_TB_OPS 10000

static bool tb_start;
enum {
    TB_WRITE_FLIP,
    TB_SNAP_READ,

};
struct tb_test {
    hal_u8_t *tbflag;
    int *tb_buf;
    int op;
    int count;
    int value;
    int snapped;
    char *name;
};

static void *test_tbop(void * arg)
{
    struct tb_test *t = arg;
    if (delta) aff_iterate(&a);
    while (!tb_start);
        {
	WITH_THREAD_CPUTIME_N(t->name, t->count, RES_NS);
	while (t->value < t->count) {
	    switch (t->op) {

	    case TB_WRITE_FLIP:
		t->tb_buf[rtapi_tb_write(t->tbflag)] = t->value;
		rtapi_tb_flip_writer(t->tbflag);
		t->value++;
		break;

	    case TB_SNAP_READ:
		if (rtapi_tb_new_snap(t->tbflag)) {
		    t->snapped++;
		    int v = t->tb_buf[rtapi_tb_snap(t->tbflag)];
		    // expect nondecreasing sequence of values
		    ck_assert(v >= t->value);
		    t->value = v;
		}
		break;
	    }
	    if (hop && (t->value % hop == 0))
		aff_iterate(&a);
	}
    }
    return NULL;
}

START_TEST(test_triple_buffer_threaded)
{
    int i;
   struct tb_test tbt[] = {
       {
	   .tbflag = &tb,
	   .tb_buf = tb_buf,
	   .op = TB_WRITE_FLIP,
	   .count =  N_TB_OPS,
	   .value = 0,
	   .name = "write_flip"
       },
       {
	   .tbflag = &tb,
	   .tb_buf = tb_buf,
	   .op = TB_SNAP_READ,
	   .count =  N_TB_OPS-1, // termination condition
	   .value = 0,
	   .snapped = 0,
	   .name = "snap_read"
       },
    };
    rtapi_tb_init(&tb);
    pthread_t tids[2];
    tb_start = false;
    for(i = 0; i < 2; i++){
	pthread_create(&(tids[i]), NULL, test_tbop, &tbt[i]);
    }
    {
	WITH_PROCESS_CPUTIME_N("triple buffer roundtrips", N_TB_OPS , RES_NS);
	tb_start = true;
	for(i = 0; i < 2; i++){
	    pthread_join(tids[i], NULL);
	}
    }
    // last value read must be last value written, flipped and snapped
    ck_assert_int_eq(tbt[0].value - 1, tbt[1].value);
    ck_assert(tbt[1].snapped > 0);
    ck_assert(tbt[1].snapped <=  N_TB_OPS);
    printf("snapped = %d out of %d\n", tbt[1].snapped,  N_TB_OPS);
}
END_TEST

Suite * triple_buffer_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("triple buffer tests");

    tc_core = tcase_create("triple buffer");
    tcase_add_test(tc_core, test_triple_buffer);
    tcase_add_test(tc_core, test_triple_buffer_threaded);
    suite_add_tcase(s, tc_core);

    return s;
}

#endif
