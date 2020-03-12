/*
 * Copyright (c) 2019 SK Telecom Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "json/json.h"

#include "base/nugu_directive.h"
#include "base/nugu_equeue.h"
#include "base/nugu_log.h"

#include "dg_types.h"

#include "directives_parser.h"
#include "http2_request.h"
#include "multipart_parser.h"

#define FILTER_CONTENT_TYPE "content-type: multipart/related; boundary="
#define FILTER_BODY_CONTENT_TYPE "Content-Type: "
#define FILTER_JSON_TYPE "application/json"
#define FILTER_OPUS_TYPE "audio/opus"

enum content_type {
    CONTENT_TYPE_UNKNOWN,
    CONTENT_TYPE_JSON,
    CONTENT_TYPE_OPUS
};

enum kvstatus {
    KEY,
    VALUE_WAIT,
    VALUE
};

struct _dir_parser {
    MultipartParser* m_parser;

    enum content_type ctype;
    char* parent_msg_id;
    int is_end;
    size_t body_size;
};

static void on_parsing_header(MultipartParser* parser, const char* data,
    size_t length, void* userdata)
{
    struct _dir_parser* dp = (struct _dir_parser*)multipart_parser_get_data(parser);
    const char* pos;
    const char* start = data;
    const char* end = data + length;
    int status = KEY;
    std::string key;
    std::string value;
    bool found = false;

    nugu_log_print(NUGU_LOG_MODULE_NETWORK_TRACE, NUGU_LOG_LEVEL_INFO, NULL,
        NULL, -1, "body header: %s", data);

    for (pos = data; pos < end; pos++) {
        switch (status) {
        case KEY:
            if (*pos == ':') {
                key.assign(start, pos - start);
                status = VALUE_WAIT;
            }
            break;
        case VALUE_WAIT:
            if (*pos == ' ')
                break;
            else {
                start = pos;
                status = VALUE;
            }
            break;
        case VALUE:
            if (*pos == '\r') {
                value.assign(start, pos - start);
            } else if (*pos == '\n') {
                status = KEY;
                start = pos + 1;
                found = true;
            }
            break;
        }

        if (found == false)
            continue;

        if (key.compare("Content-Type") == 0) {
            if (value.compare(0, strlen(FILTER_JSON_TYPE), FILTER_JSON_TYPE) == 0) {
                dp->ctype = CONTENT_TYPE_JSON;
            } else if (value.compare(0, strlen(FILTER_OPUS_TYPE), FILTER_OPUS_TYPE) == 0) {
                dp->ctype = CONTENT_TYPE_OPUS;
            } else {
                dp->ctype = CONTENT_TYPE_UNKNOWN;
                nugu_error("unknown Content-Type");
            }
        } else if (key.compare("Content-Length") == 0) {
            dp->body_size = (size_t)strtoumax(value.c_str(), nullptr, 10);
        } else if (key.compare("Parent-Message-Id") == 0) {
            if (dp->parent_msg_id)
                free(dp->parent_msg_id);
            dp->parent_msg_id = strdup(value.c_str());
        } else if (key.compare("Filename") == 0) {
            if (value.find("end") != std::string::npos)
                dp->is_end = 1;
            else
                dp->is_end = 0;
        }

        found = false;
        key.clear();
        value.clear();
    }
}

static void _body_json(const char* data, size_t length)
{
    Json::Value root;
    Json::Value dir_list;
    Json::Reader reader;
    Json::StyledWriter writer;
    std::string dump;
    std::string group;
    char group_buf[32];

    if (!reader.parse(data, root)) {
        nugu_error("parsing error: '%s'", data);
        return;
    }

    dump = writer.write(root);
    nugu_log_protocol_recv(NUGU_LOG_LEVEL_INFO, "Directives\n%s", dump.c_str());

    dir_list = root["directives"];

    group = "{ \"directives\": [";
    for (Json::ArrayIndex i = 0; i < dir_list.size(); ++i) {
        Json::Value dir = dir_list[i];
        Json::Value h;

        h = dir["header"];

        if (i > 0)
            group.append(",");

        snprintf(group_buf, sizeof(group_buf), "\"%s.%s\"",
            h["namespace"].asCString(), h["name"].asCString());
        group.append(group_buf);
    }
    group.append("] }");

    nugu_dbg("group=%s", group.c_str());

    for (Json::ArrayIndex i = 0; i < dir_list.size(); ++i) {
        Json::Value dir = dir_list[i];
        Json::Value h;
        NuguDirective* ndir;
        std::string referrer;
        std::string p;

        h = dir["header"];

        p = writer.write(dir["payload"]);

        if (!h["referrerDialogRequestId"].empty())
            referrer = h["referrerDialogRequestId"].asString();

        ndir = nugu_directive_new(h["namespace"].asCString(),
            h["name"].asCString(), h["version"].asCString(),
            h["messageId"].asCString(), h["dialogRequestId"].asCString(),
            referrer.c_str(), p.c_str(), group.c_str());
        if (!ndir)
            continue;

        if (nugu_equeue_push(NUGU_EQUEUE_TYPE_NEW_DIRECTIVE, ndir) < 0) {
            nugu_error("nugu_equeue_push() failed.");
            nugu_directive_unref(ndir);
        }
    }
}

static void _body_opus(const char* parent_msg_id, int is_end, const char* data,
    size_t length)
{
    struct equeue_data_attachment* item;

    item = (struct equeue_data_attachment*)malloc(sizeof(struct equeue_data_attachment));
    if (!item) {
        nugu_error_nomem();
        return;
    }

    item->parent_msg_id = strdup(parent_msg_id);
    item->media_type = strdup("audio/opus");
    item->is_end = is_end;
    item->length = length;
    item->data = (unsigned char*)malloc(length);
    memcpy(item->data, data, length);

    nugu_log_print(NUGU_LOG_MODULE_NETWORK, NUGU_LOG_LEVEL_DEBUG, NULL,
        NULL, -1, "<-- Attachment: parent=%s (%zd bytes, is_end=%d)",
        parent_msg_id, length, is_end);

    nugu_equeue_push(NUGU_EQUEUE_TYPE_NEW_ATTACHMENT, item);
}

static void _on_parsing_body(MultipartParser* parser, const char* data,
    size_t length, void* userdata)
{
    struct _dir_parser* dp;

    dp = (struct _dir_parser*)multipart_parser_get_data(parser);
    if (!dp)
        return;

    if (dp->ctype == CONTENT_TYPE_JSON)
        _body_json(data, length);
    else if (dp->ctype == CONTENT_TYPE_OPUS)
        _body_opus(dp->parent_msg_id, dp->is_end, data, length);
}

DirParser* dir_parser_new(void)
{
    DirParser* parser;

    parser = (struct _dir_parser*)calloc(1, sizeof(struct _dir_parser));
    if (!parser) {
        nugu_error_nomem();
        return NULL;
    }

    parser->m_parser = multipart_parser_new();
    if (!parser->m_parser) {
        nugu_error_nomem();
        free(parser);
        return NULL;
    }

    parser->ctype = CONTENT_TYPE_UNKNOWN;

    multipart_parser_set_data(parser->m_parser, parser);

    return parser;
}

void dir_parser_free(DirParser* parser)
{
    g_return_if_fail(parser != NULL);

    multipart_parser_free(parser->m_parser);

    if (parser->parent_msg_id)
        free(parser->parent_msg_id);

    free(parser);
}

int dir_parser_add_header(DirParser* parser, const char* buffer,
    size_t buffer_len)
{
    const char* pos;

    g_return_val_if_fail(parser != NULL, -1);
    g_return_val_if_fail(buffer != NULL, -1);

    if (buffer_len == 0)
        return -1;

    if (strncmp(buffer, FILTER_CONTENT_TYPE, strlen(FILTER_CONTENT_TYPE)) != 0)
        return -1;

    pos = buffer + strlen(FILTER_CONTENT_TYPE);

    /* strlen(pos) - 2('\r\n') */
    multipart_parser_set_boundary(parser->m_parser, pos, strlen(pos) - 2);

    return 0;
}

int dir_parser_parse(DirParser* parser, const char* buffer, size_t buffer_len)
{
    g_return_val_if_fail(parser != NULL, -1);
    g_return_val_if_fail(buffer != NULL, -1);

    if (buffer_len == 0)
        return 0;

    multipart_parser_parse(parser->m_parser, buffer, buffer_len,
        on_parsing_header, _on_parsing_body, NULL);

    return 0;
}