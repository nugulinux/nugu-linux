# OAuth2 example
ADD_SUBDIRECTORY(oob_setup)

# C/C++ based SDK examples
ADD_SUBDIRECTORY(standalone)
ADD_SUBDIRECTORY(simple_text)
ADD_SUBDIRECTORY(simple_asr)
ADD_SUBDIRECTORY(simple_play)
ADD_SUBDIRECTORY(simple_tts)
ADD_SUBDIRECTORY(profiling)
ADD_SUBDIRECTORY(capability_injection)
ADD_SUBDIRECTORY(response_filter)

# A shell script that executes a program using the token information
# generated in the OAuth2 example.
INSTALL(PROGRAMS nugusdk_start_sample.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME nugusdk_start_sample)
