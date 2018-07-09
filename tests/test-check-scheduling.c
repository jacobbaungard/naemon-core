#include <check.h>
#include <glib.h>
#include <math.h>
#include "naemon/objects_service.h"
#include "naemon/checks.h"
#include "naemon/broker.h"
#include "naemon/neberrors.h"

/*note: since we work with the live scheduler for the interval tests,
 * we make "approximate" assertions with a tolerance value of 1 second
 * to reduce raciness.*/
#define APPROXIMATION_TOLERANCE_MS 1000
#define assert_approximately_equal(A, B, Epsilon) ck_assert_msg(labs(A - B) <= Epsilon, "Assertion '" #A "≈" #B "'with ε = %ld failed. " #A "=%ld, " #B "=%ld", Epsilon, A, B)
#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"
static gboolean g_service_was_checked = FALSE;
static gboolean g_host_was_checked = FALSE;

int my_broker_host_check(int type, int flags, int attr, host *hst, int check_type, int state, int state_type, struct timeval start_time, struct timeval end_time, char *cmd, double latency, double exectime, int timeout, int early_timeout, int retcode, char *cmdline, char *output, char *long_output, char *perfdata, check_result *cr) {
	if (type == NEBTYPE_HOSTCHECK_INITIATE) {
		ck_assert_str_eq(TARGET_HOST_NAME, hst->name);
		g_host_was_checked = TRUE;
		return NEBERROR_CALLBACKOVERRIDE;
	}
	return 0;
}

int my_broker_service_check(int type, int flags, int attr, service *svc, int check_type, struct timeval start_time, struct timeval end_time, char *cmd, double latency, double exectime, int timeout, int early_timeout, int retcode, char *cmdline, check_result *cr) {
	if (type == NEBTYPE_SERVICECHECK_INITIATE) {
		ck_assert_str_eq(TARGET_SERVICE_NAME, svc->description);
		g_service_was_checked = TRUE;
		return NEBERROR_CALLBACKOVERRIDE;
	}
	return 0;
}

#define broker_host_check my_broker_host_check
#include "naemon/checks_host.c"
#undef broker_host_check

#define broker_service_check my_broker_service_check
#include "naemon/checks_service.c"
#undef broker_service_check

static host *hst;
static service *svc;
static command *cmd;
void setup (void) {

	init_event_queue();
	init_objects_host(1);
	init_objects_service(1);
	init_objects_command(1);

	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	hst->check_command = nm_strdup("something or other");
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	register_service(svc);

}

void teardown (void) {
	destroy_event_queue();
	destroy_objects_command();
	destroy_objects_service();
	destroy_objects_host();
}

START_TEST(service_freshness_checking)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};

	svc->check_options = CHECK_OPTION_FORCE_EXECUTION;
	svc->checks_enabled = FALSE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 1;
	svc->check_interval = 5.0;
	svc->freshness_threshold = 60;

	handle_service_check_event(&ep);
	ck_assert(g_service_was_checked);
}
END_TEST


START_TEST(host_freshness_checking)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};

	hst->check_options = CHECK_OPTION_FORCE_EXECUTION;
	hst->checks_enabled = FALSE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 1;
	hst->check_interval = 5.0;
	hst->freshness_threshold = 60;

	handle_host_check_event(&ep);
	ck_assert(g_host_was_checked);
}
END_TEST


/*
 * For services in a soft non-OK states, we should
 * use the retry interval
 */
START_TEST(service_retry_interval_soft_non_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};

	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	svc->check_options = 0;
	svc->checks_enabled = TRUE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 3;
	svc->current_attempt = 1;
	svc->check_interval = 5.0;
	svc->retry_interval = 30.0;
	svc->state_type = SOFT_STATE;
	svc->current_state = STATE_OK;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = 2;
	handle_service_check_event(&ep);
	handle_async_service_check_result(svc, &cr);
	actual_time_left = get_timed_event_time_left_ms(svc->next_check_event);
	expected_time_left = get_service_retry_interval_s(svc) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

/*
 * For services in hard non-OK states, we should
 * use the check_interval again
 */
START_TEST(service_check_interval_hard_non_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	svc->check_options = 0;
	svc->checks_enabled = TRUE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 3;
	svc->check_interval = 5.0;
	svc->retry_interval = 30.0;
	svc->current_state = STATE_CRITICAL;
	svc->last_hard_state = STATE_CRITICAL;
	svc->state_type = HARD_STATE;
	svc->current_attempt = 3;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_CRITICAL;
	handle_service_check_event(&ep);
	handle_async_service_check_result(svc, &cr);

	actual_time_left = get_timed_event_time_left_ms(svc->next_check_event);
	expected_time_left = get_service_check_interval_s(svc) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

/*
 * For services in OK states, we should
 * use the regular check interval
 */
START_TEST(service_check_interval_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	svc->check_options = 0;
	svc->checks_enabled = TRUE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 3;
	svc->check_interval = 5.0;
	svc->retry_interval = 30.0;
	svc->current_state = STATE_OK;
	svc->state_type = HARD_STATE;
	svc->current_attempt = 3;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_OK;
	handle_service_check_event(&ep);
	handle_async_service_check_result(svc, &cr);

	actual_time_left = get_timed_event_time_left_ms(svc->next_check_event);
	expected_time_left = get_service_check_interval_s(svc) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

/*
 * For hosts in a soft non-OK states, we should
 * use the retry interval
 */
START_TEST(host_retry_interval_soft_non_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	hst->check_options = 0;
	hst->checks_enabled = TRUE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 3;
	hst->check_interval = 5.0;
	hst->retry_interval = 30.0;
	hst->current_attempt = 2;

	init_check_result(&cr);
	cr.object_check_type = HOST_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_CRITICAL;
	handle_host_check_event(&ep);
	handle_async_host_check_result(hst, &cr);

	actual_time_left = get_timed_event_time_left_ms(hst->next_check_event);
	expected_time_left = get_host_retry_interval_s(hst) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

/*
 * For hosts in hard non-OK states, we should
 * use the check_interval again
 */
START_TEST(host_check_interval_hard_non_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	hst->check_options = 0;
	hst->checks_enabled = TRUE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 3;
	hst->check_interval = 5.0;
	hst->retry_interval = 30.0;

	hst->current_state = STATE_DOWN;
	hst->state_type = HARD_STATE;
	hst->current_attempt = 3;

	init_check_result(&cr);
	cr.object_check_type = HOST_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_CRITICAL;
	handle_host_check_event(&ep);
	handle_async_host_check_result(hst, &cr);

	actual_time_left = get_timed_event_time_left_ms(hst->next_check_event);
	expected_time_left = get_host_check_interval_s(hst) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

/*
 * For hosts in OK states, we should
 * use the regular check interval
 */
START_TEST(host_check_interval_OK_states)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr;

	hst->check_options = 0;
	hst->checks_enabled = TRUE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 3;
	hst->check_interval = 5.0;
	hst->retry_interval = 30.0;

	hst->current_state = STATE_UP;
	hst->current_attempt = 1;

	init_check_result(&cr);
	cr.object_check_type = HOST_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_UP;
	handle_host_check_event(&ep);
	handle_async_host_check_result(hst, &cr);


	actual_time_left = get_timed_event_time_left_ms(hst->next_check_event);
	expected_time_left = get_host_check_interval_s(hst) * 1000;
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

START_TEST(test_check_window)
{
	time_t expected_window, actual_window;
	hst->retry_interval = 30.0;
	hst->check_interval = 1.0;
	hst->current_state = STATE_UP;
	hst->state_type = HARD_STATE;
	expected_window = get_host_check_interval_s(hst);
	actual_window = check_window(hst);
	ck_assert_int_eq(expected_window, actual_window);

	hst->current_state = STATE_DOWN;
	hst->state_type = SOFT_STATE;
	expected_window = get_host_retry_interval_s(hst);
	actual_window = check_window(hst);
	ck_assert_int_eq(expected_window, actual_window);

	svc->retry_interval = 1.0;
	svc->check_interval = 30.0;
	svc->current_state = STATE_WARNING;
	svc->state_type = HARD_STATE;
	expected_window = get_service_check_interval_s(svc);
	actual_window = check_window(svc);
	ck_assert_int_eq(expected_window, actual_window);

	svc->current_state = STATE_WARNING;
	svc->state_type = SOFT_STATE;
	expected_window = get_service_retry_interval_s(svc);
	actual_window = check_window(svc);
	ck_assert_int_eq(expected_window, actual_window);
}
END_TEST


/*
 * Ensure that host checks uses their own retry_interval rather than being
 * rescheduled due to the service being in a HARD CRITICAL.
 */
START_TEST(host_retry_interval_on_service_down)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};
	struct nm_event_execution_properties ep2 = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};
	long actual_time_left;
	time_t expected_time_left;
	check_result cr, cr2;

	hst->check_options = 0;
	hst->checks_enabled = TRUE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 3;
	hst->check_interval = 5.0;
	hst->retry_interval = 30.0;
	hst->current_attempt = 2;

	init_check_result(&cr);
	cr.object_check_type = HOST_CHECK;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.return_code = STATE_CRITICAL;
	handle_host_check_event(&ep);
  handle_async_host_check_result(hst, &cr);

	svc->check_options = 0;
	svc->checks_enabled = TRUE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 3;
	svc->check_interval = 5.0;
	svc->retry_interval = 5.0;
	svc->current_state = STATE_CRITICAL;
	svc->last_hard_state = STATE_CRITICAL;
	svc->state_type = HARD_STATE;
	svc->current_attempt = 3;
	svc->host_ptr = hst;

	init_check_result(&cr2);
	cr2.object_check_type = SERVICE_CHECK;
	cr2.check_type = CHECK_TYPE_ACTIVE;
	cr2.return_code = STATE_CRITICAL;
	handle_service_check_event(&ep2);
	handle_async_service_check_result(svc, &cr);

	actual_time_left = get_timed_event_time_left_ms(hst->next_check_event);
	expected_time_left = get_host_retry_interval_s(hst) * 1000;
	printf("actual: %lld expected: %lld\n", (long long) actual_time_left, (long long) expected_time_left);
	assert_approximately_equal(expected_time_left, actual_time_left, APPROXIMATION_TOLERANCE_MS);
}
END_TEST

Suite*
check_scheduling_suite(void)
{
	Suite *s = suite_create("Check scheduling");
	TCase *tc_intervals = tcase_create("Check & retry intervals");
	TCase *tc_freshness_checking = tcase_create("Freshness checking");
	TCase *tc_miscellaneous = tcase_create("Miscellaneous tests");
	tcase_add_checked_fixture(tc_freshness_checking, setup, teardown);
	tcase_add_test(tc_freshness_checking, service_freshness_checking);
	tcase_add_test(tc_freshness_checking, host_freshness_checking);
	suite_add_tcase(s, tc_freshness_checking);

	tcase_add_checked_fixture(tc_intervals, setup, teardown);
	tcase_add_test(tc_intervals, service_check_interval_OK_states);
	tcase_add_test(tc_intervals, service_retry_interval_soft_non_OK_states);
	tcase_add_test(tc_intervals, service_check_interval_hard_non_OK_states);

	tcase_add_test(tc_intervals, host_check_interval_OK_states);
	tcase_add_test(tc_intervals, host_retry_interval_soft_non_OK_states);
	tcase_add_test(tc_intervals, host_check_interval_hard_non_OK_states);
	tcase_add_test(tc_intervals, host_retry_interval_on_service_down);
	suite_add_tcase(s, tc_intervals);

	tcase_add_checked_fixture(tc_miscellaneous, setup, teardown);
	tcase_add_test(tc_miscellaneous, test_check_window);
	suite_add_tcase(s, tc_miscellaneous);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = check_scheduling_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
