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
#include <string.h>

#include "base/nugu_log.h"
#include "base/nugu_network_manager.h"
#include "core/capability_manager.hh"

#include "system_agent.hh"

#define DEFAULT_INACTIVITY_TIMEOUT (60 * 60) /* 1 hour */

namespace NuguCapability {

static const char* CAPABILITY_NAME = "System";
static const char* CAPABILITY_VERSION = "1.1";

SystemAgent::SystemAgent()
    : Capability(CAPABILITY_NAME, CAPABILITY_VERSION)
    , system_listener(nullptr)
    , timer(nullptr)
{
    nugu_network_manager_set_handoff_status_callback(
        [](NuguNetworkHandoffStatus status, void* userdata) {
            SystemAgent* sa = static_cast<SystemAgent*>(userdata);

            switch (status) {
            case NUGU_NETWORK_HANDOFF_FAILED:
                nugu_error("handoff failed");
                break;
            case NUGU_NETWORK_HANDOFF_READY:
                nugu_dbg("handoff ready. send 'Disconnect' event");
                sa->sendEventDisconnect();
                break;
            case NUGU_NETWORK_HANDOFF_COMPLETED:
                nugu_dbg("handoff completed. send 'SynchronizeState' event");
                sa->synchronizeState();
                break;
            default:
                nugu_error("invalid status: %d", status);
                break;
            }
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
    if (system_listener) {
        bool charging = false;
        int battery = -1;

        if (system_listener->requestDeviceCharging(charging))
            root["charging"] = charging;

        if (system_listener->requestDeviceBattery(battery)) {
            if (battery <= 0)
                battery = 1;

            if (battery > 100)
                battery = 100;

            root["battery"] = battery;
        }

        nugu_dbg("charging: %d, battery: %d", charging, battery);
    }
    ctx[getName()] = root;
}

void SystemAgent::setCapabilityListener(ICapabilityListener* clistener)
{
    if (clistener)
        system_listener = dynamic_cast<ISystemListener*>(clistener);
}

void SystemAgent::receiveCommand(const std::string& from, const std::string& command, const std::string& param)
{
    std::string convert_command;
    convert_command.resize(command.size());
    std::transform(command.cbegin(), command.cend(), convert_command.begin(), ::tolower);

    if (!convert_command.compare("activity")) {
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

    if (root["hostname"].isString()) {
        const char* tmp = root["hostname"].asCString();
        if (tmp) {
            int len = strlen(tmp);
            memcpy(policy.hostname, tmp, (len > NUGU_NETWORK_MAX_ADDRESS) ? NUGU_NETWORK_MAX_ADDRESS : len);
        }
    } else {
        nugu_error("can't find 'hostname' string attribute");
        return;
    }

    if (root["address"].isString()) {
        const char* tmp = root["address"].asCString();
        if (tmp) {
            int len = strlen(tmp);
            memcpy(policy.address, tmp, (len > NUGU_NETWORK_MAX_ADDRESS) ? NUGU_NETWORK_MAX_ADDRESS : len);
        }
    } else {
        nugu_error("can't find 'address' string attribute");
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

    if (!reader.parse(message, root)) {
        nugu_error("parsing error");
        return;
    }

    code = root["code"].asString();
    description = root["description"].asString();

    nugu_error("Exception: code='%s', description='%s'", code.c_str(), description.c_str());

    SystemException exception = SystemException::INTERNAL_SERVICE_EXCEPTION;
    if (code == "UNAUTHORIZED_REQUEST_EXCEPTION")
        exception = SystemException::UNAUTHORIZED_REQUEST_EXCEPTION;
    else if (code == "PLAY_ROUTER_PROCESSING_EXCEPTION")
        exception = SystemException::PLAY_ROUTER_PROCESSING_EXCEPTION;
    else if (code == "ASR_RECOGNIZING_EXCEPTION")
        exception = SystemException::ASR_RECOGNIZING_EXCEPTION;
    else if (code == "TTS_SPEAKING_EXCEPTION")
        exception = SystemException::TTS_SPEAKING_EXCEPTION;

    if (exception == SystemException::PLAY_ROUTER_PROCESSING_EXCEPTION) {
        nugu_info("Release ASR focus due to 'not found play' state");
        CapabilityManager* cmanager = CapabilityManager::getInstance();
        cmanager->releaseFocus("asr");
    }

    if (system_listener)
        system_listener->onException(exception);
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
    CapabilityManager::getInstance()->sendCommand("System", "ASR", "releasefocus", dialog_id);
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

} // NuguCapability