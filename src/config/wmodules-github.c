/* Copyright (C) 2015-2021, Wazuh Inc.
 * All right reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
*/
#if defined (WIN32) || (__linux__) || defined (__MACH__)

#include "wazuh_modules/wmodules.h"

static const char *XML_ENABLED = "enabled";
static const char *XML_RUN_ON_START = "run_on_start";
static const char *XML_INTERVAL = "interval";
static const char *XML_TIME_DELAY = "time_delay";
static const char *XML_ONLY_FUTURE_EVENTS = "only_future_events";

static const char *XML_API_AUTH = "api_auth";
static const char *XML_ORG_NAME = "org_name";
static const char *XML_API_TOKEN = "api_token";

static const char *XML_API_PARAMETERS = "api_parameters";
static const char *XML_EVENT_TYPE = "event_type";

static const char *EVENT_TYPE_ALL = "all";
static const char *EVENT_TYPE_GIT = "git";
static const char *EVENT_TYPE_WEB = "web";

time_t time_convert(const char *time_c) {
    char *endptr;
    time_t time_i = strtoul(time_c, &endptr, 0);

    if (time_i <= 0 || time_i >= INT_MAX) {
        return OS_INVALID;
    }

    switch (*endptr) {
    case 'd':
        time_i *= 86400;
        break;
    case 'h':
        time_i *= 3600;
        break;
    case 'm':
        time_i *= 60;
        break;
    case 's':
        break;
    case '\0':
        break;
    default:
        return OS_INVALID;
    }
    return time_i;
}

// Parse XML
int wm_github_read(const OS_XML *xml, xml_node **nodes, wmodule *module) {

    int i = 0;
    int j = 0;
    xml_node **children = NULL;
    wm_github* github_config = NULL;
    wm_github_auth *github_auth = NULL;

    if (!module->data) {
        // Default initialization
        module->context = &WM_GITHUB_CONTEXT;
        module->tag = strdup(module->context->name);
        os_calloc(1, sizeof(wm_github), github_config);
        github_config->enabled =            WM_GITHUB_DEFAULT_ENABLED;
        github_config->run_on_start =       WM_GITHUB_DEFAULT_RUN_ON_START;
        github_config->only_future_events = WM_GITHUB_DEFAULT_ONLY_FUTURE_EVENTS;
        github_config->interval =           WM_GITHUB_DEFAULT_INTERVAL;
        github_config->time_delay =         WM_GITHUB_DEFAULT_DELAY;
        os_strdup(EVENT_TYPE_ALL, github_config->event_type);
        module->data = github_config;
    } else {
        github_config = module->data;
    }

    if (!nodes) {
        return OS_INVALID;
    }

    for (i = 0; nodes[i]; i++) {
        if (!nodes[i]->element) {
            merror(XML_ELEMNULL);
            return OS_INVALID;
        } else if (!strcmp(nodes[i]->element, XML_ENABLED)) {
            if (!strcmp(nodes[i]->content, "yes"))
                github_config->enabled = 1;
            else if (!strcmp(nodes[i]->content, "no"))
                github_config->enabled = 0;
            else {
                merror("Invalid content for tag '%s' at module '%s'.", XML_ENABLED, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_RUN_ON_START)) {
            if (!strcmp(nodes[i]->content, "yes"))
                github_config->run_on_start = 1;
            else if (!strcmp(nodes[i]->content, "no"))
                github_config->run_on_start = 0;
            else {
                merror("Invalid content for tag '%s' at module '%s'.", XML_RUN_ON_START, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_INTERVAL)) {
            github_config->interval = time_convert(nodes[i]->content);
            if (github_config->interval == OS_INVALID) {
                merror("Invalid content for tag '%s' at module '%s'.", XML_INTERVAL, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_TIME_DELAY)) {
            github_config->time_delay = time_convert(nodes[i]->content);
            if (github_config->time_delay == OS_INVALID) {
                merror("Invalid content for tag '%s' at module '%s'.", XML_TIME_DELAY, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_ONLY_FUTURE_EVENTS)) {
            if (!strcmp(nodes[i]->content, "yes"))
                github_config->only_future_events = 1;
            else if (!strcmp(nodes[i]->content, "no"))
                github_config->only_future_events = 0;
            else {
                merror("Invalid content for tag '%s' at module '%s'.", XML_ONLY_FUTURE_EVENTS, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_API_AUTH)) {
            // Create auth node
            if (github_auth) {
                os_calloc(1, sizeof(wm_github_auth), github_auth->next);
                github_auth = github_auth->next;
            } else {
                // First github_auth
                os_calloc(1, sizeof(wm_github_auth), github_auth);
                github_config->auth = github_auth;
            }

            if (!(children = OS_GetElementsbyNode(xml, nodes[i]))) {
                continue;
            }
            for (j = 0; children[j]; j++) {
                if (!strcmp(children[j]->element, XML_ORG_NAME)) {
                    if (strlen(children[j]->content) == 0) {
                        merror("Empty content for tag '%s' at module '%s'.", XML_ORG_NAME, WM_GITHUB_CONTEXT.name);
                        OS_ClearNode(children);
                        return OS_INVALID;
                    }
                    os_free(github_auth->org_name);
                    os_strdup(children[j]->content, github_auth->org_name);
                } else if (!strcmp(children[j]->element, XML_API_TOKEN)) {
                    if (strlen(children[j]->content) == 0) {
                        merror("Empty content for tag '%s' at module '%s'.", XML_API_TOKEN, WM_GITHUB_CONTEXT.name);
                        OS_ClearNode(children);
                        return OS_INVALID;
                    }
                    os_free(github_auth->api_token);
                    os_strdup(children[j]->content, github_auth->api_token);
                } else {
                    merror("No such tag '%s' at module '%s'.", children[j]->element, WM_GITHUB_CONTEXT.name);
                    OS_ClearNode(children);
                    return OS_INVALID;
                }
            }
            OS_ClearNode(children);

            if(github_auth->org_name == NULL) {
                merror("'%s' is missing at module '%s'.", XML_ORG_NAME, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            } else if(github_auth->api_token == NULL) {
                merror("'%s' is missing at module '%s'.", XML_API_TOKEN, WM_GITHUB_CONTEXT.name);
                return OS_INVALID;
            }
        } else if (!strcmp(nodes[i]->element, XML_API_PARAMETERS)) {
            if (!(children = OS_GetElementsbyNode(xml, nodes[i]))) {
                continue;
            }
            for (j = 0; children[j]; j++) {
                if (!strcmp(children[j]->element, XML_EVENT_TYPE)) {
                    if (strcmp(children[j]->content, EVENT_TYPE_ALL) && strcmp(children[j]->content, EVENT_TYPE_GIT) && strcmp(children[j]->content, EVENT_TYPE_WEB)) {
                        merror("Invalid content for tag '%s' at module '%s'.", XML_EVENT_TYPE, WM_GITHUB_CONTEXT.name);
                        OS_ClearNode(children);
                        return OS_INVALID;
                    }
                    os_free(github_config->event_type);
                    os_strdup(children[j]->content, github_config->event_type);
                } else {
                    merror("No such tag '%s' at module '%s'.", children[j]->element, WM_GITHUB_CONTEXT.name);
                    OS_ClearNode(children);
                    return OS_INVALID;
                }
            }
            OS_ClearNode(children);
        } else {
            merror("No such tag '%s' at module '%s'.", nodes[i]->element, WM_GITHUB_CONTEXT.name);
            return OS_INVALID;
        }
    }

    if (!github_auth) {
        merror("Empty content for tag '%s' at module '%s'.", XML_API_AUTH, WM_GITHUB_CONTEXT.name);
        return OS_INVALID;
    }
    return OS_SUCCESS;
}
#endif
