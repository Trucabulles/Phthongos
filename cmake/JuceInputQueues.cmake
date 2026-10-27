# Local, opt-in JUCE patch: background threads must not share GUI input queues.
# Preserve this guard when configuring another JUCE dependency checkout.
set(_pht_threads "${CMAKE_CURRENT_LIST_DIR}/../vendor/JUCE/modules/juce_core/native/juce_Threads_windows.cpp")
file(READ "${_pht_threads}" _pht_content)
if(NOT _pht_content MATCHES "PHT_WINDOWS_ISOLATE_INPUT_QUEUES")
    set(_pht_original "    if (juce_messageWindowHandle != nullptr)\n        AttachThreadInput (GetWindowThreadProcessId (juce_messageWindowHandle, nullptr),\n                           GetCurrentThreadId(), TRUE);")
    string(FIND "${_pht_content}" "${_pht_original}" _pht_position)
    if(_pht_position EQUAL -1)
        message(FATAL_ERROR "JUCE thread startup changed: review the Phthongos input-queue patch.")
    endif()
    string(REPLACE "${_pht_original}" "   #if ! PHT_WINDOWS_ISOLATE_INPUT_QUEUES\n${_pht_original}\n   #endif" _pht_content "${_pht_content}")
    file(WRITE "${_pht_threads}" "${_pht_content}")
endif()
