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

#ifndef __HTTP2_DIRECTIVES_PARSER_H__
#define __HTTP2_DIRECTIVES_PARSER_H__

#include "http2/http2_request.h"
#include "base/nugu_directive.h"
#include "base/nugu_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dir_parser_type {
	DIR_PARSER_TYPE_SERVER_INITIATIVE,
	DIR_PARSER_TYPE_EVENT_RESPONSE
};

typedef struct _dir_parser DirParser;

typedef void (*DirParserDirectiveCallback)(DirParser *dp, NuguDirective *dir,
					   void *userdata);
typedef void (*DirParserJsonCallback)(DirParser *dp, const char *json,
					  void *userdata);
typedef void (*DirParserEndCallback)(DirParser *dp, void *userdata);

DirParser *dir_parser_new(enum dir_parser_type type);
void dir_parser_free(DirParser *dp);

int dir_parser_add_header(DirParser *dp, const char *buffer, size_t buffer_len);
int dir_parser_parse(DirParser *dp, const char *buffer, size_t buffer_len);
int dir_parser_set_debug_message(DirParser *dp, const char *msg);

int dir_parser_set_directive_callback(DirParser *dp,
				      DirParserDirectiveCallback cb,
				      void *userdata);
int dir_parser_set_json_callback(DirParser *dp,
				     DirParserJsonCallback cb,
				     void *userdata);
int dir_parser_set_end_callback(DirParser *dp, DirParserEndCallback cb,
				void *userdata);

int dir_parser_set_json_buffer(DirParser *dp, NuguBuffer *buf);
NuguBuffer *dir_parser_get_json_buffer(DirParser *dp);

#ifdef __cplusplus
}
#endif

#endif
