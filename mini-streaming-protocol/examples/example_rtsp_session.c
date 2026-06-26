#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtsp.h"

int main(void) {
    printf("=== Example: RTSP Session Lifecycle ===\n\n");
    /* Initialize RTSP session */
    rtsp_session_t session;
    rtsp_session_init(&session, "d4e5f6-audio-video");
    printf("Session ID: %s\n", session.session_id);
    printf("Initial state: %s\n\n",
           session.state == RTSP_STATE_INIT ? "INIT" :
           session.state == RTSP_STATE_READY ? "READY" :
           session.state == RTSP_STATE_PLAYING ? "PLAYING" : "RECORDING");

    /* Parse RTSP URL */
    rtsp_url_t url;
    if (rtsp_parse_url("rtsp://media.example.com:8554/live/stream1", &url) == 0) {
        printf("RTSP URL parsed:\n");
        printf("  Host: %s\n", url.host);
        printf("  Port: %d\n", url.port);
        printf("  Path: %s\n\n", url.path);
    }

    /* Transition through states */
    const char *states[] = {"INIT", "READY", "PLAYING", "RECORDING"};
    printf("State transitions:\n");
    printf("  %s --SETUP--> ", states[session.state]);
    rtsp_session_transition(&session, RTSP_METHOD_SETUP);
    printf("%s\n", states[session.state]);
    printf("  %s --PLAY-->  ", states[session.state]);
    rtsp_session_transition(&session, RTSP_METHOD_PLAY);
    printf("%s\n", states[session.state]);
    printf("  %s --PAUSE--> ", states[session.state]);
    rtsp_session_transition(&session, RTSP_METHOD_PAUSE);
    printf("%s\n", states[session.state]);
    printf("  %s --TEARDOWN--> ", states[session.state]);
    rtsp_session_transition(&session, RTSP_METHOD_TEARDOWN);
    printf("%s\n", states[session.state]);
    return 0;
}