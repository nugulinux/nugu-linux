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

#ifndef __NUGU_CLIENT_IMPL_H__
#define __NUGU_CLIENT_IMPL_H__

#include <map>
#include <memory>

#include "capability/capability_interface.hh"
#include "clientkit/media_player_interface.hh"
#include "clientkit/network_manager_interface.hh"
#include "clientkit/nugu_client_listener.hh"
#include "clientkit/nugu_configuration.hh"
#include "clientkit/wakeup_interface.hh"

namespace NuguClientKit {

class NuguClientImpl : public INetworkManagerListener {
public:
    NuguClientImpl();
    virtual ~NuguClientImpl();

    void setConfig(NuguConfig::Key key, const std::string& value);
    void setListener(INuguClientListener* listener);
    INuguClientListener* getListener();
    void registerCapability(const std::string& cname, std::pair<ICapabilityInterface*, ICapabilityListener*> capability);
    int create(void);
    bool initialize(void);
    void deInitialize(void);

    ICapabilityInterface* getCapabilityHandler(const std::string& cname);
    IWakeupHandler* getWakeupHandler();
    INetworkManager* getNetworkManager();
    IMediaPlayer* createMediaPlayer();

    // overriding INetworkManagerListener
    void onStatusChanged(NetworkStatus status) override;

private:
    int createCapabilities(void);

    using CapabilityMap = std::map<std::string, std::pair<ICapabilityInterface*, ICapabilityListener*>>;

    CapabilityMap icapability_map;
    INuguClientListener* listener = nullptr;
    IWakeupHandler* wakeup_handler = nullptr;
    std::unique_ptr<INetworkManager> network_manager = nullptr;
    bool initialized = false;
};

} // NuguClientKit

#endif /* __NUGU_CLIENT_IMPL_H__ */