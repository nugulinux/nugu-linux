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

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "capability_manager.hh"
#include "nugu_config.h"
#include "nugu_log.h"
#include "nugu_network_manager.h"
#include "system_agent.hh"

#define DEFAULT_INACTIVITY_TIMEOUT (60 * 60) /* 1 hour */

namespace NuguCore {

static const std::string capability_version = "1.0";

SystemAgent::SystemAgent()
    : Capability(CapabilityType::System, capability_version)
    , system_listener(nullptr)
    , battery(0)
    , timer(nullptr)
{
    nugu_network_manager_set_handoff_status_callback(
        [](NuguNetworkHandoffStatus status, void* userdata) {
            if (status != NUGU_NETWORK_HANDOFF_SUCCESS)
                return;

            SystemAgent* sa = static_cast<SystemAgent*>(userdata);

            nugu_info("handoff ok. send synchronizeState() event");
            sa->synchronizeState();
        },
        this);
}

SystemAgent::~SystemAgent()
{
    if (timer) {
        nugu_timer_delete(timer);
        timer = nullptr;
    }

    nugu_network_manager_set_handoff_status_callback(NULL, NULL);
}

void SystemAgent::parsingDirective(const char* dname, const char* message)
{
    if (!strcmp(dname, "ResetUserInactivity"))
        parsingResetUserInactivity(message);
    else if (!strcmp(dname, "HandoffConnection"))
        parsingHandoffConnection(message);
    else if (!strcmp(dname, "TurnOff"))
        parsingTurnOff(message);
    else if (!strcmp(dname, "UpdateState"))
        parsingUpdateState(message);
    else if (!strcmp(dname, "Exception"))
        parsingException(message);
    else if (!strcmp(dname, "NoDirectives"))
        parsingNoDirectives(message);
    else if (!strcmp(dname, "Revoke"))
        parsingRevoke(message);
    else
        nugu_warn("%s[%s] is not support %s directive", getName().c_str(), getVersion().c_str(), dname);
}

void SystemAgent::updateInfoForContext(Json::Value& ctx)
{
    Json::Value root;

    root["version"] = getVersion();
    if (battery)
        root["battery"] = battery;
    ctx[getName()] = root;
}

void SystemAgent::setCapabilityListener(ICapabilityListener* clistener)
{
    if (clistener)
        system_listener = dynamic_cast<ISystemListener*>(clistener);
}

void SystemAgent::receiveCommand(CapabilityType from, std::string command, const std::string& param)
{
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    if (!command.compare("activity")) {
        nugu_dbg("update timer");
        if (timer)
            nugu_timer_start(timer);
    }
}

void SystemAgent::synchronizeState(void)
{
    if (!timer) {
        timer = nugu_timer_new(DEFAULT_INACTIVITY_TIMEOUT * 1000, 1);
        nugu_timer_set_callback(
            timer, [](void* userdata) {
                SystemAgent* sa = static_cast<SystemAgent*>(userdata);

                nugu_dbg("inactivity timeout");
                sa->sendEventUserInactivityReport(DEFAULT_INACTIVITY_TIMEOUT);
            },
            this);
    }

    nugu_timer_start(timer);

    sendEventSynchronizeState();
}

void SystemAgent::disconnect(void)
{
    if (timer)
        nugu_timer_stop(timer);

    sendEventDisconnect();
}

void SystemAgent::updateUserActivity(void)
{
    if (timer)
        nugu_timer_start(timer);
}

void SystemAgent::sendEventSynchronizeState(void)
{
    std::string ename = "SynchronizeState";
    std::string payload = "";

    sendEvent(ename, CapabilityManager::getInstance()->makeAllContextInfo(), payload);
}

void SystemAgent::sendEventUserInactivityReport(int seconds)
{
    std::string ename = "UserInactivityReport";
    std::string payload = "";
    char buf[64];

    snprintf(buf, 64, "{\"inactiveTimeInSeconds\": %d}", seconds);
    payload = buf;

    sendEvent(ename, getContextInfo(), payload);
}

void SystemAgent::sendEventDisconnect(void)
{
    std::string ename = "Disconnect";
    std::string payload = "";

    sendEvent(ename, getContextInfo(), payload);
}

void SystemAgent::sendEventEcho(void)
{
    std::string ename = "Echo";
    std::string payload = "";

    sendEvent(ename, getContextInfo(), payload);
}

void SystemAgent::parsingResetUserInactivity(const char* message)
{
    if (timer)
        nugu_timer_start(timer);
}

void SystemAgent::parsingHandoffConnection(const char* message)
{
    Json::Value root;
    Json::Reader reader;
    NuguNetworkServerPolicy policy;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    memset(&policy, 0, sizeof(NuguNetworkServerPolicy));

    if (root["protocol"].isString()) {
        const char* tmp = root["protocol"].asCString();
        if (g_ascii_strcasecmp(tmp, "H2C") == 0)
            policy.protocol = NUGU_NETWORK_PROTOCOL_H2C;
        else if (g_ascii_strcasecmp(tmp, "H2") == 0)
            policy.protocol = NUGU_NETWORK_PROTOCOL_H2;
        else {
            nugu_error("unknown protocol: '%s'", tmp);
            policy.protocol = NUGU_NETWORK_PROTOCOL_UNKNOWN;
        }
    } else {
        nugu_error("can't find 'protocol' string attribute");
        return;
    }

    if (root["domain"].isString()) {
        const char* tmp = root["domain"].asCString();
        if (tmp) {
            int len = strlen(tmp);
            memcpy(policy.hostname, tmp, (len > NUGU_NETWORK_MAX_ADDRESS) ? NUGU_NETWORK_MAX_ADDRESS : len);
        }
    } else {
        nugu_error("can't find 'domain' string attribute");
        return;
    }

    if (root["hostname"].isString()) {
        const char* tmp = root["hostname"].asCString();
        if (tmp) {
            int len = strlen(tmp);
            memcpy(policy.address, tmp, (len > NUGU_NETWORK_MAX_ADDRESS) ? NUGU_NETWORK_MAX_ADDRESS : len);
        }
    } else {
        nugu_error("can't find 'hostname' string attribute");
        return;
    }

    if (root["port"].isNumeric()) {
        policy.port = root["port"].asInt();
    } else {
        nugu_error("can't find 'port' number attribute");
        return;
    }

    if (root["retryCountLimit"].isNumeric()) {
        policy.retry_count_limit = root["retryCountLimit"].asInt();
    } else {
        nugu_error("can't find 'retryCountLimit' number attribute");
        return;
    }

    if (root["connectionTimeout"].isNumeric()) {
        policy.connection_timeout_ms = root["connectionTimeout"].asInt();
    } else {
        nugu_error("can't find 'connectionTimeout' number attribute");
        return;
    }

    if (root["charge"].isString()) {
        if (g_ascii_strcasecmp(root["charge"].asCString(), "FREE") == 0)
            policy.is_charge = 0;
        else
            policy.is_charge = 1;
    } else {
        nugu_error("can't find 'charge' attribute");
    }

    nugu_dbg("%s:%d (%s:%d, protocol=%d)", policy.hostname, policy.port,
        policy.address, policy.port, policy.protocol);
    nugu_dbg(" - retryCountLimit: %d, connectionTimeout: %d, charge: %d",
        policy.retry_count_limit, policy.connection_timeout_ms, policy.is_charge);

    nugu_network_manager_handoff(&policy);
}

void SystemAgent::parsingTurnOff(const char* message)
{
    if (system_listener)
        system_listener->onTurnOff();
}

void SystemAgent::parsingUpdateState(const char* message)
{
    sendEventSynchronizeState();
}

void SystemAgent::parsingException(const char* message)
{
    Json::Value root;
    Json::Reader reader;
    std::string code;
    std::string description;
    size_t pos_response;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    code = root["code"].asString();
    description = root["description"].asString();

    nugu_error("Exception: code='%s', description='%s'", code.c_str(), description.c_str());

    /**
     * FIXME: Force release ASR SPK resource on error case
     */
    pos_response = description.find("response:");
    if (code == "PLAY_ROUTER_PROCESSING_EXCEPTION" && pos_response > 0) {
        Json::Value resp_root;
        std::string response = description.substr(pos_response + strlen("response:"));

        if (reader.parse(response, resp_root)) {
            int state;

            state = resp_root["state"].asInt();
            if (state == 1003) {
                nugu_info("Release ASR focus due to 'not found play' state");
                CapabilityManager* cmanager = CapabilityManager::getInstance();
                cmanager->releaseFocus("asr", NUGU_FOCUS_RESOURCE_SPK);
                cmanager->releaseFocus("asr", NUGU_FOCUS_RESOURCE_MIC);

                if (system_listener)
                    system_listener->onSystemMessageReport(SystemMessage::ROUTE_ERROR_NOT_FOUND_PLAY);
            }
        }
    }
    notifyObservers(SYSTEM_INTERNAL_SERVICE_EXCEPTION, nullptr);
}

void SystemAgent::parsingNoDirectives(const char* message)
{
    Json::Value root;
    Json::Reader reader;
    const char* dialog_id;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    dialog_id = nugu_directive_peek_dialog_id(getNuguDirective());
    CapabilityManager::getInstance()->sendCommand(CapabilityType::System, CapabilityType::ASR, "releasefocus", dialog_id);
}

void SystemAgent::parsingRevoke(const char* message)
{
    Json::Value root;
    Json::Reader reader;
    std::string reason;

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    reason = root["reason"].asString();
    if (reason.size() == 0) {
        nugu_error("There is no mandatory data in directive message");
        return;
    }

    nugu_dbg("reason: %s", reason.c_str());

    if (reason.compare("REVOKED_DEVICE") == 0) {
        if (system_listener)
            system_listener->onRevoke(RevokeReason::REVOKED_DEVICE);
    } else {
        nugu_error("invalid reason");
    }
}

} // NuguCore
