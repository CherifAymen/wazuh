/*
 * Copyright (C) 2015-2020, Wazuh Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 *
 * Test corresponding to the scheduling capacities
 * for github Module
 * */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <time.h>

#include "shared.h"
#include "wazuh_modules/wmodules.h"
#include "wazuh_modules/wm_github.h"
#include "wazuh_modules/wm_github.c"

#include "../scheduling/wmodules_scheduling_helpers.h"
#include "../../wrappers/common.h"
#include "../../wrappers/libc/stdlib_wrappers.h"
#include "../../wrappers/wazuh/os_regex/os_regex_wrappers.c"
#include "../../wrappers/wazuh/shared/mq_op_wrappers.h"
#include "../../wrappers/wazuh/wazuh_modules/wmodules_wrappers.h"
#include "../../wrappers/wazuh/shared/url_wrappers.h"
#include "../../wrappers/libc/time_wrappers.h"

void * wm_github_main(wm_github* github_config);

unsigned int __wrap_sleep(unsigned int __seconds) {
    check_expected(__seconds);
    return mock_type(unsigned int);
}

unsigned int __wrap_localtime_r(__attribute__ ((__unused__)) const time_t *t, __attribute__ ((__unused__)) struct tm *tm) {
    return mock_type(unsigned int);
}

////////////////  test wm-github /////////////////

typedef struct test_struct {
    wm_github *github_config;
    curl_response* response;
    char *root_c;
} test_struct_t;

static int setup_conf(void **state) {
    test_struct_t *init_data = NULL;
    os_calloc(1,sizeof(test_struct_t), init_data);
    os_calloc(1, sizeof(wm_github), init_data->github_config);
    test_mode = 1;
    *state = init_data;
    return 0;
}

static int teardown_conf(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    test_mode = 0;
    expect_string(__wrap__mtinfo, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtinfo, formatted_msg, "Module GitHub finished.");
    wm_github_destroy(data->github_config);
    os_free(data->root_c);
    os_free(data);

    return 0;
}

void test_github_main_disabled(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 0;

    expect_string(__wrap__mtinfo, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtinfo, formatted_msg, "Module GitHub disabled.");

    wm_github_main(data->github_config);
}

void test_github_main_fail_StartMQ(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;

    expect_string(__wrap__mtinfo, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtinfo, formatted_msg, "Module GitHub started.");

    expect_string(__wrap__mterror, tag, "wazuh-modulesd:osquery");
    expect_string(__wrap__mterror, formatted_msg, "Can't connect to queue. Closing module.");

    expect_string(__wrap_StartMQ, path, DEFAULTQUEUE);
    expect_value(__wrap_StartMQ, type, WRITE);
    will_return(__wrap_StartMQ, -1);

    wm_github_main(data->github_config);
}

void test_github_main_enable(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->interval = 2;

    expect_string(__wrap_StartMQ, path, DEFAULTQUEUE);
    expect_value(__wrap_StartMQ, type, WRITE);
    will_return(__wrap_StartMQ, 1);

    expect_string(__wrap__mtinfo, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtinfo, formatted_msg, "Module GitHub started.");

    expect_value(__wrap_sleep, __seconds, 2);
    will_return(__wrap_sleep, 0);

    wm_github_main(data->github_config);
}

void test_github_get_next_page_warn(void **state) {
    char *header;

    expect_string(__wrap_OSRegex_Compile, pattern,"<(\\S+)>;\\s*rel=\"next\"");
    will_return(__wrap_OSRegex_Compile, 0);

    expect_string(__wrap__mtwarn, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtwarn, formatted_msg, "Cannot compile regex");

    assert_null(wm_github_get_next_page(header));
}

void test_github_get_next_page_execute(void **state) {
    char *header = "test";

    expect_string(__wrap_OSRegex_Compile, pattern, "<(\\S+)>;\\s*rel=\"next\"");
    will_return(__wrap_OSRegex_Compile, 1);

    expect_string(__wrap_OSRegex_Execute, str, "test");
    will_return(__wrap_OSRegex_Execute, NULL);

    expect_any(__wrap_OSRegex_FreePattern, reg);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No match regex.");

    assert_null(wm_github_get_next_page(header));
}

void test_github_get_next_page_sub_string(void **state) {
    char *header = "test";

    expect_string(__wrap_OSRegex_Compile, pattern, "<(\\S+)>;\\s*rel=\"next\"");
    will_return(__wrap_OSRegex_Compile, 1);

    expect_string(__wrap_OSRegex_Execute, str, "test");
    will_return(__wrap_OSRegex_Execute, "yes");

    expect_any(__wrap_OSRegex_FreePattern, reg);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No next page was captured.");

    assert_null(wm_github_get_next_page(header));
}

void test_github_get_next_page_complete(void **state) {
    wm_github* github_config = *state;
    char *header = "test_1";
    char *next_page = NULL;

    expect_string(__wrap_OSRegex_Compile, pattern, "<(\\S+)>;\\s*rel=\"next\"");
    will_return(__wrap_OSRegex_Compile, 1);

    expect_string(__wrap_OSRegex_Execute, str, "test_1");
    will_return(__wrap_OSRegex_Execute, "yes");

    expect_any(__wrap_OSRegex_FreePattern, reg);

    next_page = wm_github_get_next_page(header);

    assert_string_equal(next_page, "https://api.com/");
    os_free(next_page);
}

void test_github_dump_no_options(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    char *test = "{\"github\":{\"enabled\":\"no\",\"run_on_start\":\"no\",\"only_future_events\":\"no\"}}";

    cJSON *root = wm_github_dump(data->github_config);
    data->root_c = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    assert_string_equal(data->root_c, test);
}

void test_github_dump_yes_options(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->only_future_events = 1;
    data->github_config->interval = 10;
    data->github_config->time_delay = 1;
    os_calloc(1, sizeof(wm_github_auth), data->github_config->auth);
    os_strdup("test_token", data->github_config->auth->api_token);
    os_strdup("test_org", data->github_config->auth->org_name);
    os_strdup("all", data->github_config->event_type);

    char *test = "{\"github\":{\"enabled\":\"yes\",\"run_on_start\":\"yes\",\"only_future_events\":\"yes\",\"interval\":10,\"time_delay\":1,\"api_auth\":[{\"org_name\":\"test_org\",\"api_token\":\"test_token\"}],\"event_type\":\"all\"}}";

    cJSON *root = wm_github_dump(data->github_config);
    data->root_c = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    assert_string_equal(data->root_c, test);
}

void test_github_scan_failure_action_1(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    os_calloc(1, sizeof(wm_github_fail), data->github_config->fails);
    data->github_config->fails->fails = 1;
    os_strdup("test_org", data->github_config->fails->org_name);
    data->github_config->fails->next = NULL;
    char *org_name = "test_org";
    char *error_msg = "test_error";
    int queue_fd = 1;

    wm_github_scan_failure_action(&data->github_config->fails, org_name, error_msg, queue_fd);

    assert_string_equal(data->github_config->fails->org_name, "test_org");
    assert_int_equal(data->github_config->fails->fails, 2);
}

void test_github_scan_failure_action_2(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    os_calloc(1, sizeof(wm_github_fail), data->github_config->fails);
    data->github_config->fails->fails = 2;
    os_strdup("test_org", data->github_config->fails->org_name);
    data->github_config->fails->next = NULL;
    char *org_name = "test_org";
    char *error_msg = "test_error";
    int queue_fd = 1;
    wm_max_eps = 1;

    int result = 0;

    expect_value(__wrap_wm_sendmsg, usec, 1000000);
    expect_value(__wrap_wm_sendmsg, queue, 1);
    expect_string(__wrap_wm_sendmsg, message, "{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\",\"organization\":\"test_org\",\"response\":\"Unknown error\"}}");
    expect_string(__wrap_wm_sendmsg, locmsg, "github");
    expect_value(__wrap_wm_sendmsg, loc, LOCALFILE_MQ);
    will_return(__wrap_wm_sendmsg, result);

    expect_string(__wrap__mtdebug2, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug2, formatted_msg, "Sending GitHub internal message: '{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\",\"organization\":\"test_org\",\"response\":\"Unknown error\"}}'");

    wm_github_scan_failure_action(&data->github_config->fails, org_name, error_msg, queue_fd);

    assert_string_equal(data->github_config->fails->org_name, "test_org");
    assert_int_equal(data->github_config->fails->fails, 3);
}

void test_github_scan_failure_action_error(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    os_calloc(1, sizeof(wm_github_fail), data->github_config->fails);
    data->github_config->fails->fails = 2;
    os_strdup("test_org", data->github_config->fails->org_name);
    data->github_config->fails->next = NULL;
    char *org_name = "test_org";
    char *error_msg = "test_error";
    int queue_fd = 1;
    wm_max_eps = 1;

    int result = -1;

    expect_value(__wrap_wm_sendmsg, usec, 1000000);
    expect_value(__wrap_wm_sendmsg, queue, 1);
    expect_string(__wrap_wm_sendmsg, message, "{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\",\"organization\":\"test_org\",\"response\":\"Unknown error\"}}");
    expect_string(__wrap_wm_sendmsg, locmsg, "github");
    expect_value(__wrap_wm_sendmsg, loc, LOCALFILE_MQ);
    will_return(__wrap_wm_sendmsg, result);

    expect_string(__wrap__mtdebug2, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug2, formatted_msg, "Sending GitHub internal message: '{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\",\"organization\":\"test_org\",\"response\":\"Unknown error\"}}'");

    expect_string(__wrap__mterror, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mterror, formatted_msg, "(1210): Queue 'queue/sockets/queue' not accessible: 'Success'");

    wm_github_scan_failure_action(&data->github_config->fails, org_name, error_msg, queue_fd);

    assert_string_equal(data->github_config->fails->org_name, "test_org");
    assert_int_equal(data->github_config->fails->fails, 3);
}

void test_github_scan_failure_action_org_null(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->fails = NULL;
    char *org_name = "test_org";
    char *error_msg = "test_error";
    int queue_fd = 1;
    wm_max_eps = 1;

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No record for this organization: 'test_org'");

    wm_github_scan_failure_action(&data->github_config->fails, org_name, error_msg, queue_fd);

    assert_string_equal(data->github_config->fails->org_name, "test_org");
    assert_int_equal(data->github_config->fails->fails, 1);
}

void test_github_execute_scan(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->only_future_events = 1;
    data->github_config->interval = 10;
    data->github_config->time_delay = 1;
    os_calloc(1, sizeof(wm_github_auth), data->github_config->auth);
    os_strdup("test_token", data->github_config->auth->api_token);
    os_strdup("test_org", data->github_config->auth->org_name);
    data->github_config->auth->next = NULL;
    os_strdup("all", data->github_config->event_type);

    int initial_scan = 1;

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "Scanning organization: 'test_org'");

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_READ);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:24:56");
    will_return(__wrap_strftime, 20);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:34:56");
    will_return(__wrap_strftime, 20);

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_WRITE);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_any(__wrap__mtdebug1, formatted_msg);

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_WRITE);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No record for this organization: 'test_org'");

    wm_github_execute_scan(data->github_config, initial_scan);
}

void test_github_execute_scan_current_null(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->auth = NULL;

    int initial_scan = 1;

    assert_int_equal(wm_github_execute_scan(data->github_config, initial_scan), 0);
}

void test_github_execute_scan_no_initial_scan(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->only_future_events = 1;
    data->github_config->interval = 10;
    data->github_config->time_delay = 1;
    os_calloc(1, sizeof(wm_github_auth), data->github_config->auth);
    os_strdup("test_token", data->github_config->auth->api_token);
    os_strdup("test_org", data->github_config->auth->org_name);
    data->github_config->auth->next = NULL;
    os_strdup("all", data->github_config->event_type);
    os_calloc(1, sizeof(curl_response), data->response);
    data->response->status_code = 404;
    data->response->body = NULL;

    int initial_scan = 0;

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "Scanning organization: 'test_org'");

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_READ);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:24:56");
    will_return(__wrap_strftime, 20);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:34:56");
    will_return(__wrap_strftime, 20);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_any(__wrap__mtdebug1, formatted_msg);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No record for this organization: 'test_org'");

    expect_any(__wrap_wurl_http_get_with_header, header);
    expect_any(__wrap_wurl_http_get_with_header, url);
    will_return(__wrap_wurl_http_get_with_header, data->response);

    wm_github_execute_scan(data->github_config, initial_scan);

    assert_int_equal(data->github_config->fails->fails, 1);
    assert_string_equal(data->github_config->fails->org_name, "test_org");
}

void test_github_execute_scan_status_code_200(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->only_future_events = 1;
    data->github_config->interval = 10;
    data->github_config->time_delay = 1;
    os_calloc(1, sizeof(wm_github_auth), data->github_config->auth);
    os_strdup("test_token", data->github_config->auth->api_token);
    os_strdup("test_org", data->github_config->auth->org_name);
    data->github_config->auth->next = NULL;
    os_strdup("all", data->github_config->event_type);
    os_calloc(1, sizeof(curl_response), data->response);
    data->response->status_code = 200;
    data->response->body = NULL;

    int initial_scan = 0;

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "Scanning organization: 'test_org'");

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_READ);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:24:56");
    will_return(__wrap_strftime, 20);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:34:56");
    will_return(__wrap_strftime, 20);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_any(__wrap__mtdebug1, formatted_msg);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "Error parsing response body.");

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No record for this organization: 'test_org'");

    expect_any(__wrap_wurl_http_get_with_header, header);
    expect_any(__wrap_wurl_http_get_with_header, url);
    will_return(__wrap_wurl_http_get_with_header, data->response);

    wm_github_execute_scan(data->github_config, initial_scan);

    assert_int_equal(data->github_config->fails->fails, 1);
    assert_string_equal(data->github_config->fails->org_name, "test_org");
}

void test_github_execute_scan_status_code_200_null(void **state) {
    test_struct_t *data  = (test_struct_t *)*state;
    data->github_config->enabled = 1;
    data->github_config->run_on_start = 1;
    data->github_config->only_future_events = 1;
    data->github_config->interval = 10;
    data->github_config->time_delay = 1;
    os_calloc(1, sizeof(wm_github_auth), data->github_config->auth);
    os_strdup("test_token", data->github_config->auth->api_token);
    os_strdup("test_org", data->github_config->auth->org_name);
    data->github_config->auth->next = NULL;
    os_strdup("all", data->github_config->event_type);
    os_calloc(1, sizeof(curl_response), data->response);
    data->response->status_code = 200;
    os_strdup("{\"github\":{\"actor\":\"wazuh\"}}", data->response->body);
    os_strdup("test", data->response->header);

    int initial_scan = 0;

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "Scanning organization: 'test_org'");

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_READ);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, 1);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:24:56");
    will_return(__wrap_strftime, 20);

    will_return(__wrap_localtime_r, 1);

    will_return(__wrap_strftime,"2021-05-07 12:34:56");
    will_return(__wrap_strftime, 20);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_any(__wrap__mtdebug1, formatted_msg);

    expect_any(__wrap_wurl_http_get_with_header, header);
    expect_any(__wrap_wurl_http_get_with_header, url);
    will_return(__wrap_wurl_http_get_with_header, data->response);

    expect_string(__wrap__mtdebug1, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug1, formatted_msg, "No record for this organization: 'test_org'");

    expect_value(__wrap_wm_sendmsg, usec, 1000000);
    expect_value(__wrap_wm_sendmsg, queue, 0);
    expect_string(__wrap_wm_sendmsg, message, "{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\"}}");
    expect_string(__wrap_wm_sendmsg, locmsg, "github");
    expect_value(__wrap_wm_sendmsg, loc, LOCALFILE_MQ);
    will_return(__wrap_wm_sendmsg, 0);

    expect_string(__wrap__mtdebug2, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mtdebug2, formatted_msg, "Sending GitHub log: '{\"github\":{\"actor\":\"wazuh\",\"source\":\"github\"}}'");

    expect_string(__wrap_wm_state_io, tag, "github-test_org");
    expect_value(__wrap_wm_state_io, op, WM_IO_WRITE);
    expect_any(__wrap_wm_state_io, state);
    expect_any(__wrap_wm_state_io, size);
    will_return(__wrap_wm_state_io, -1);

    expect_string(__wrap__mterror, tag, "wazuh-modulesd:github");
    expect_string(__wrap__mterror, formatted_msg, "Couldn't save running state.");

    wm_github_execute_scan(data->github_config, initial_scan);
}

////////////////  test wmodules-github /////////////////

static int setup_test_read(void **state) {
    test_structure *test;
    os_calloc(1, sizeof(test_structure), test);
    os_calloc(1, sizeof(wmodule), test->module);
    *state = test;
    return 0;
}

static int teardown_test_read(void **state) {
    test_structure *test = *state;
    OS_ClearNode(test->nodes);
    OS_ClearXML(&(test->xml));
    if((wm_github*)test->module->data){
        if(((wm_github*)test->module->data)->auth){
            os_free(((wm_github*)test->module->data)->auth->org_name);
            os_free(((wm_github*)test->module->data)->auth->api_token);
            if(((wm_github*)test->module->data)->auth->next) {
                os_free(((wm_github*)test->module->data)->auth->next->org_name);
                os_free(((wm_github*)test->module->data)->auth->next->api_token);
                os_free(((wm_github*)test->module->data)->auth->next->next);
            }
            os_free(((wm_github*)test->module->data)->auth->next);
            os_free(((wm_github*)test->module->data)->auth);
        }
        os_free(((wm_github*)test->module->data)->event_type);
    }
    os_free(test->module->data);
    os_free(test->module->tag);
    os_free(test->module);
    os_free(test);
    return 0;
}

void test_read_configuration(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->enabled, 0);
    assert_int_equal(module_data->run_on_start, 1);
    assert_int_equal(module_data->interval, 600);
    assert_int_equal(module_data->time_delay, 1);
    assert_int_equal(module_data->only_future_events, 0);
    assert_string_equal(module_data->auth->org_name, "Wazuh");
    assert_string_equal(module_data->auth->api_token, "Wazuh_token");
    assert_string_equal(module_data->event_type, "git");
}

void test_read_configuration_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_auth>"
            "<org_name>Wazuh1</org_name>"
            "<api_token>Wazuh_token1</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->enabled, 0);
    assert_int_equal(module_data->run_on_start, 1);
    assert_int_equal(module_data->interval, 600);
    assert_int_equal(module_data->time_delay, 1);
    assert_int_equal(module_data->only_future_events, 0);
    assert_string_equal(module_data->auth->org_name, "Wazuh");
    assert_string_equal(module_data->auth->api_token, "Wazuh_token");
    assert_string_equal(module_data->auth->next->org_name, "Wazuh1");
    assert_string_equal(module_data->auth->next->api_token, "Wazuh_token1");
    assert_string_equal(module_data->event_type, "git");
}

void test_read_default_configuration(void **state) {
    const char *string =
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->enabled, 1);
    assert_int_equal(module_data->run_on_start, 1);
    assert_int_equal(module_data->interval, 600);
    assert_int_equal(module_data->time_delay, 1);
    assert_int_equal(module_data->only_future_events, 1);
    assert_string_equal(module_data->auth->org_name, "Wazuh");
    assert_string_equal(module_data->auth->api_token, "Wazuh_token");
    assert_string_equal(module_data->event_type, "all");
}

void test_read_interval(void **state) {
    const char *string =
        "<enabled>yes</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>10</interval>\n"
        "<time_delay>10</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->interval, 10);
}

void test_read_interval_s(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>50s</interval>\n"
        "<time_delay>10</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->interval, 50);
}

void test_read_interval_m(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>1m</interval>\n"
        "<time_delay>10</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->interval, 60);
}

void test_read_interval_h(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>2h</interval>\n"
        "<time_delay>10</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->interval, 7200);
}

void test_read_interval_d(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>3d</interval>\n"
        "<time_delay>10</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->interval, 259200);
}

void test_repeatd_tag(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
        "<api_parameters>"
            "<event_type>git</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),0);
    wm_github *module_data = (wm_github*)test->module->data;
    assert_int_equal(module_data->enabled, 0);
    assert_int_equal(module_data->run_on_start, 1);
    assert_int_equal(module_data->interval, 600);
    assert_int_equal(module_data->time_delay, 1);
    assert_int_equal(module_data->only_future_events, 0);
    assert_string_equal(module_data->auth->org_name, "Wazuh");
    assert_string_equal(module_data->auth->api_token, "Wazuh_token");
    assert_string_equal(module_data->event_type, "git");
}

void test_fake_tag(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>yes</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
        "<fake-tag>ASD</fake-tag>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "No such tag 'fake-tag' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_content_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>invalid</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'run_on_start' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_content_2(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>yes</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>invalid</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'event_type' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_content_3(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>invalid</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'only_future_events' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_content_4(void **state) {
    const char *string =
        "<enabled>invalid</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>yes</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'enabled' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_content_5(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>invalid</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>yes</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'interval' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_time_delay_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>-1</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>invalid</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'time_delay' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_invalid_time_delay_2(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1y</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>invalid</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Invalid content for tag 'time_delay' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_api_auth(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Empty content for tag 'api_auth' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_api_auth_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<invalid>Wazuh</invalid>"
            "<invalid>Wazuh_token</invalid>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "No such tag 'invalid' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_org_name(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name></org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Empty content for tag 'org_name' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_org_name_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "'org_name' is missing at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_api_token(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token></api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "Empty content for tag 'api_token' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_api_token_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
        "</api_auth>"
        "<api_parameters>"
            "<event_type>all</event_type>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "'api_token' is missing at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

void test_error_event_type_1(void **state) {
    const char *string =
        "<enabled>no</enabled>\n"
        "<run_on_start>no</run_on_start>\n"
        "<interval>10m</interval>\n"
        "<time_delay>1s</time_delay>"
        "<only_future_events>no</only_future_events>"
        "<api_auth>"
            "<org_name>Wazuh</org_name>"
            "<api_token>Wazuh_token</api_token>"
        "</api_auth>"
        "<api_parameters>"
            "<invalid>all</invalid>"
        "</api_parameters>"
    ;
    test_structure *test = *state;
    expect_string(__wrap__merror, formatted_msg, "No such tag 'invalid' at module 'github'.");
    test->nodes = string_to_xml_node(string, &(test->xml));
    assert_int_equal(wm_github_read(&(test->xml), test->nodes, test->module),-1);
}

int main(void) {

    const struct CMUnitTest tests_with_startup[] = {
        cmocka_unit_test_setup_teardown(test_github_main_disabled, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_main_fail_StartMQ, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_main_enable, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_get_next_page_warn, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_get_next_page_execute, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_get_next_page_sub_string, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_get_next_page_complete, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_dump_no_options, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_dump_yes_options, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_scan_failure_action_1, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_scan_failure_action_2, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_scan_failure_action_error, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_scan_failure_action_org_null, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_execute_scan_current_null, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_execute_scan, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_execute_scan_no_initial_scan, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_execute_scan_status_code_200, setup_conf, teardown_conf),
        cmocka_unit_test_setup_teardown(test_github_execute_scan_status_code_200_null, setup_conf, teardown_conf),
    };
    const struct CMUnitTest tests_without_startup[] = {
        cmocka_unit_test_setup_teardown(test_read_configuration, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_configuration_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_default_configuration, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_interval, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_interval_s, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_interval_m, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_interval_h, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_read_interval_d, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_repeatd_tag, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_fake_tag, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_content_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_content_2, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_content_3, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_content_4, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_content_5, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_time_delay_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_invalid_time_delay_2, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_api_auth, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_api_auth_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_org_name, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_org_name_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_api_token, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_api_token_1, setup_test_read, teardown_test_read),
        cmocka_unit_test_setup_teardown(test_error_event_type_1, setup_test_read, teardown_test_read),
    };
    int result;
    result = cmocka_run_group_tests(tests_with_startup, NULL, NULL);
    result += cmocka_run_group_tests(tests_without_startup, NULL, NULL);
    return result;
}