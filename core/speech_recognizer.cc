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

#include <endpoint_detector.h>

#include "base/nugu_log.h"
#include "interface/nugu_configuration.hh"

#include "speech_recognizer.hh"

namespace NuguCore {

using namespace NuguInterface;

SpeechRecognizer::SpeechRecognizer()
{
    std::string sample = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_SAMPLERATE);
    std::string format = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_FORMAT);
    std::string channel = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_CHANNEL);

    AudioInputProcessor::init("asr", sample, format, channel);
}

void SpeechRecognizer::sendSyncListeningEvent(ListeningState state)
{
    AudioInputProcessor::sendSyncEvent([&] {
        if (listener)
            listener->onListeningState(state);
    });
}

void SpeechRecognizer::setListener(ISpeechRecognizerListener* listener)
{
    this->listener = listener;
}

void SpeechRecognizer::loop(void)
{
    unsigned char epd_buf[OUT_DATA_SIZE];
    EpdParam epd_param;
    int pcm_size;
    int length;
    int prev_epd_ret = 0;
    bool is_epd_end = false;
    std::string model_file;
    std::string model_path;
    std::string timeout;
    std::string max_duration;
    std::string pause_length;

    timeout = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_TIMEOUT_SEC);
    max_duration = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_MAX_DURATION_SEC);
    pause_length = NuguConfig::getValue(NuguConfig::Key::ASR_EPD_PAUSE_LENGTH_MSEC);

    std::string samplerate = recorder->getSamplerate();
    if (samplerate == "8k")
        epd_param.sample_rate = 8000;
    else if (samplerate == "22k")
        epd_param.sample_rate = 22050;
    else if (samplerate == "32k")
        epd_param.sample_rate = 32000;
    else if (samplerate == "44k")
        epd_param.sample_rate = 44100;
    else
        epd_param.sample_rate = 16000;

    epd_param.max_speech_duration = std::stoi(max_duration);
    epd_param.time_out = std::stoi(timeout);
    epd_param.pause_length = std::stoi(pause_length);

    nugu_dbg("Listening Thread: started");

    model_path = NuguConfig::getValue(NuguConfig::Key::MODEL_PATH);
    if (model_path.size()) {
        nugu_dbg("model path: %s", model_path.c_str());

        model_file = model_path + "/" + EPD_MODEL_FILE;
        nugu_dbg("epd model file: %s", model_file.c_str());
    }

    mutex.lock();
    thread_created = true;
    cond.notify_all();
    mutex.unlock();

    while (g_atomic_int_get(&destroy) == 0) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock);
        lock.unlock();

        if (is_running == false)
            continue;

        nugu_dbg("Listening Thread: asr_is_running=%d", is_running);

        if (epd_client_start(model_file.c_str(), epd_param) < 0) {
            nugu_error("epd_client_start() failed");
            break;
        };

        if (!recorder->start()) {
            nugu_error("recorder->start() failed.");
            break;
        }

        sendSyncListeningEvent(ListeningState::LISTENING);

        prev_epd_ret = 0;
        is_epd_end = false;
        pcm_size = recorder->getAudioFrameSize();

        while (is_running) {
            char pcm_buf[pcm_size];

            if (!recorder->isRecording()) {
                struct timespec ts;

                ts.tv_sec = 0;
                ts.tv_nsec = 10 * 1000 * 1000; /* 10ms */

                nugu_dbg("Listening Thread: not recording state");
                nanosleep(&ts, NULL);
                continue;
            }

            if (recorder->isMute()) {
                nugu_error("nugu recorder is mute");
                sendSyncListeningEvent(ListeningState::FAILED);
                break;
            }

            if (!recorder->getAudioFrame(pcm_buf, &pcm_size, 0)) {
                nugu_error("nugu_recorder_get_frame_timeout() failed");
                sendSyncListeningEvent(ListeningState::FAILED);
                break;
            }

            if (pcm_size == 0) {
                nugu_error("pcm_size result is 0");
                sendSyncListeningEvent(ListeningState::FAILED);
                break;
            }

            length = OUT_DATA_SIZE;
            epd_ret = epd_client_run((char*)epd_buf, &length, (short*)pcm_buf, pcm_size);

            if (epd_ret < 0 || epd_ret > EPD_END_CHECK) {
                nugu_error("epd_client_run() failed: %d", epd_ret);
                sendSyncListeningEvent(ListeningState::FAILED);
                break;
            }

            if (length > 0) {
                /* Invoke the onRecordData callback in thread context */
                if (listener)
                    listener->onRecordData((unsigned char*)epd_buf, length);
            }

            switch (epd_ret) {
            case EPD_END_DETECTED:
                sendSyncListeningEvent(ListeningState::SPEECH_END);
                is_epd_end = true;
                break;
            case EPD_END_DETECTING:
            case EPD_START_DETECTED:
                if (prev_epd_ret == EPD_START_DETECTING) {
                    sendSyncListeningEvent(ListeningState::SPEECH_START);
                }
                break;
            case EPD_TIMEOUT:
                sendSyncListeningEvent(ListeningState::TIMEOUT);
                is_epd_end = true;
                break;

            case EPD_MAXSPEECH:
                sendSyncListeningEvent(ListeningState::SPEECH_END);
                is_epd_end = true;
                break;
            }

            if (is_epd_end)
                break;

            prev_epd_ret = epd_ret;
        }

        recorder->stop();
        epd_client_release();

        if (g_atomic_int_get(&destroy) == 0)
            sendSyncListeningEvent(ListeningState::DONE);
    }

    nugu_dbg("Listening Thread: exited");
}

bool SpeechRecognizer::startListening(void)
{
    return AudioInputProcessor::start([&] {
        if (listener)
            listener->onListeningState(ListeningState::READY);

        epd_ret = 0;
    });
}

void SpeechRecognizer::stopListening(void)
{
    AudioInputProcessor::stop();
}

} // NuguCore
