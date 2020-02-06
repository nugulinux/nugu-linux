#include <glib.h>
#include <iostream>

#include <base/nugu_log.h>
#include <capability/audio_player_interface.hh>
#include <capability/capability_factory.hh>
#include <capability/system_interface.hh>
#include <capability/text_interface.hh>
#include <capability/tts_interface.hh>
#include <clientkit/nugu_client.hh>

using namespace NuguClientKit;
using namespace NuguCapability;

static std::shared_ptr<NuguClient> nugu_client;
static ITextHandler* text_handler = nullptr;
static ITTSHandler* tts_handler = nullptr;
static ISystemHandler* system_handler = nullptr;
static IAudioPlayerHandler* audio_player_handler = nullptr;

static std::string text_value;

class MyNetwork : public INetworkManagerListener {
public:
    void onStatusChanged(NetworkStatus status)
    {
        switch (status) {
        case NetworkStatus::DISCONNECTED:
            std::cout << "Network disconnected !" << std::endl;
            break;
        case NetworkStatus::CONNECTED:
            std::cout << "Network connected !" << std::endl;

            std::cout << "Send the text command: " << text_value << std::endl;
            text_handler->requestTextInput(text_value);
            break;
        case NetworkStatus::CONNECTING:
            std::cout << "Network connecting..." << std::endl;
            break;
        default:
            break;
        }
    }

    void onError(NetworkError error)
    {
        switch (error) {
        case NetworkError::TOKEN_ERROR:
            std::cout << "Token error !" << std::endl;
            break;
        case NetworkError::UNKNOWN:
            std::cout << "Unknown error !" << std::endl;
            break;
        }
    }
};

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " Text-Command" << std::endl;
        return 0;
    }

    text_value = argv[1];

    std::cout << "Simple text command example!" << std::endl;
    std::cout << " - Text-Command: " << text_value << std::endl;

    /* Turn off the SDK internal log */
    nugu_log_set_system(NUGU_LOG_SYSTEM_NONE);

    nugu_client = std::make_shared<NuguClient>();

    system_handler = CapabilityFactory::makeCapability<SystemAgent, ISystemHandler>();
    text_handler = CapabilityFactory::makeCapability<TextAgent, ITextHandler>();
    tts_handler = CapabilityFactory::makeCapability<TTSAgent, ITTSHandler>();
    audio_player_handler = CapabilityFactory::makeCapability<AudioPlayerAgent, IAudioPlayerHandler>();

    /* Register build-in capabilities */
    nugu_client->getCapabilityBuilder()
        ->add(system_handler)
        ->add(tts_handler)
        ->add(text_handler)
        ->construct();

    if (!nugu_client->initialize()) {
        std::cout << "SDK Initialization failed." << std::endl;
        return -1;
    }

    /* Network manager */
    auto network_manager_listener(std::make_shared<MyNetwork>());

    INetworkManager* network_manager = nugu_client->getNetworkManager();
    network_manager->addListener(network_manager_listener.get());
    network_manager->setToken(getenv("NUGU_TOKEN"));
    network_manager->connect();

    /* Start GMainLoop */
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    std::cout << "Start the eventloop" << std::endl;
    g_main_loop_run(loop);

    /* wait until g_main_loop_quit() */

    g_main_loop_unref(loop);

    nugu_client->deInitialize();

    return 0;
}