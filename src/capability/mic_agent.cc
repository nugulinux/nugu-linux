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

#include <string.h>

#include "base/nugu_log.h"
#include "mic_agent.hh"

namespace NuguCapability {

static const char* CAPABILITY_NAME = "Mic";
static const char* CAPABILITY_VERSION = "1.0";

MicAgent::MicAgent()
    : Capability(CAPABILITY_NAME, CAPABILITY_VERSION)
    , mic_listener(nullptr)
    , cur_status(MicStatus::ON)
    , pre_status(MicStatus::ON)
    , ps_id("")
{
}

MicAgent::~MicAgent()
{
}

void MicAgent::parsingDirective(const char* dname, const char* message)
{
    nugu_dbg("message: %s", message);

    // directive name check
    if (!strcmp(dname, "SetMic"))
        parsingSetMic(message);
    else {
        nugu_warn("%s[%s] is not support %s directive", getName().c_str(), getVersion().c_str(), dname);
    }
}

void MicAgent::updateInfoForContext(Json::Value& ctx)
{
    Json::Value mic;

    mic["version"] = getVersion();
    if (pre_status == MicStatus::ON)
        mic["micStatus"] = "ON";
    else
        mic["micStatus"] = "OFF";

    ctx[getName()] = mic;
}

void MicAgent::setCapabilityListener(ICapabilityListener* clistener)
{
    if (clistener)
        mic_listener = dynamic_cast<IMicListener*>(clistener);
}

void MicAgent::enable()
{
    control(true);
}

void MicAgent::disable()
{
    control(false);
}

void MicAgent::sendEventSetMicSucceeded()
{
    sendEventCommon("SetMicSucceeded");
}

void MicAgent::sendEventSetMicFailed()
{
    sendEventCommon("SetMicFailed");
}

void MicAgent::sendEventCommon(const std::string& ename)
{
    std::string payload = "";
    Json::Value root;
    Json::StyledWriter writer;

    root["playServiceId"] = ps_id;
    if (cur_status == MicStatus::ON)
        root["micStatus"] = "ON";
    else
        root["micStatus"] = "OFF";
    payload = writer.write(root);

    sendEvent(ename, getContextInfo(), payload);
}

void MicAgent::parsingSetMic(const char* message)
{
    Json::Value root;
    Json::Reader reader;
    std::string mic_status;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    ps_id = root["playServiceId"].asString();
    mic_status = root["MicStatus"].asString();

    if (ps_id.size() == 0 || mic_status.size() == 0) {
        nugu_error("There is no mandatory data in directive message");
        return;
    }

    control(mic_status == "ON", true);
}

void MicAgent::control(bool enable, bool send_event)
{
    if (enable)
        cur_status = MicStatus::ON;
    else
        cur_status = MicStatus::OFF;

    if (cur_status == pre_status)
        return;

    capa_helper->setMute(cur_status == MicStatus::OFF);

    if (send_event)
        sendEventSetMicSucceeded();

    if (mic_listener)
        mic_listener->micStatusChanged(cur_status);

    pre_status = cur_status;
}

} // NuguCapability
