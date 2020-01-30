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

#include "capability_manager.hh"

#include "capability_manager_helper.hh"

namespace NuguCore {

void CapabilityManagerHelper::addCapability(const std::string& cname, ICapabilityInterface* cap)
{
    CapabilityManager::getInstance()->addCapability(cname, cap);
}

void CapabilityManagerHelper::removeCapability(const std::string& cname)
{
    CapabilityManager::getInstance()->removeCapability(cname);
}

void CapabilityManagerHelper::destroyInstance()
{
    CapabilityManager::destroyInstance();
}

} // NuguCore