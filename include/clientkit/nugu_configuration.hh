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

#ifndef __NUGU_CONFIGURATION_H__
#define __NUGU_CONFIGURATION_H__

#include <map>
#include <string>

namespace NuguClientKit {

/**
 * @file nugu_configuration.hh
 */
class NuguConfig {
public:
    enum class Key {
        WAKEUP_WITH_LISTENING,
        WAKEUP_WORD,
        ASR_EPD_TYPE,
        ASR_EPD_MAX_DURATION_SEC,
        ASR_EPD_PAUSE_LENGTH_MSEC,
        ASR_EPD_TIMEOUT_SEC,
        ASR_ENCODING,
        ASR_EPD_SAMPLERATE,
        ASR_EPD_FORMAT,
        ASR_EPD_CHANNEL,
        KWD_SAMPLERATE,
        KWD_FORMAT,
        KWD_CHANNEL,
        SERVER_RESPONSE_TIMEOUT_MSEC,
        MODEL_PATH,
        TTS_ENGINE
    };

public:
    NuguConfig() = delete;

    static void loadDefaultValue();
    static const std::string getValue(Key key);
    static void setValue(Key key, const std::string& value);

private:
    static std::map<Key, std::string> configs;
};

} // NuguClientKit

#endif /* __NUGU_CONFIGURATION__ */