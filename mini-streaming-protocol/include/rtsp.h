/**
 * @file rtsp.h
 * @brief RTSP (Real Time Streaming Protocol) — RFC 2326
 *
 * Application-level protocol for controlling delivery of real-time
 * media streams. Provides VCR-like remote control for media servers.
 *
 * Covers L1: RTSP methods, status codes, SDP, URL, transport
 * Covers L2: Session state machine, media control, transport negotiation
 * Covers L3: Session timeout arithmetic, NPT
 * Covers L4: Session state transitions (finite state machine)
 * Covers L5: RTSP parse/format, SDP parse, URL parse
 * Covers L6: SETUP/PLAY/PAUSE/TEARDOWN lifecycle
 *
 * References: RFC 2326, RFC 4566 (SDP), RFC 7826 (RTSP 2.0)
 * @course Stanford EE359, Georgia Tech ECE 6601, CMU 15-441
 */

#ifndef RTSP_H
#define RTSP_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* L1: RTSP Methods */
typedef enum {
    RTSP_METHOD_OPTIONS       = 0,
    RTSP_METHOD_DESCRIBE      = 1,
    RTSP_METHOD_ANNOUNCE      = 2,
    RTSP_METHOD_SETUP         = 3,
    RTSP_METHOD_PLAY          = 4,
    RTSP_METHOD_PAUSE         = 5,
    RTSP_METHOD_TEARDOWN      = 6,
    RTSP_METHOD_GET_PARAMETER  = 7,
    RTSP_METHOD_SET_PARAMETER  = 8,
    RTSP_METHOD_REDIRECT      = 9,
    RTSP_METHOD_RECORD        = 10,
    RTSP_METHOD_UNKNOWN       = 99
} rtsp_method_t;

/* L1: RTSP Status Codes */
typedef enum {
    RTSP_STATUS_OK                      = 200,
    RTSP_STATUS_CREATED                 = 201,
    RTSP_STATUS_LOW_ON_STORAGE          = 250,
    RTSP_STATUS_MULTIPLE_CHOICES        = 300,
    RTSP_STATUS_MOVED_PERMANENTLY       = 301,
    RTSP_STATUS_MOVED_TEMPORARILY       = 302,
    RTSP_STATUS_BAD_REQUEST             = 400,
    RTSP_STATUS_UNAUTHORIZED            = 401,
    RTSP_STATUS_FORBIDDEN               = 403,
    RTSP_STATUS_NOT_FOUND               = 404,
    RTSP_STATUS_METHOD_NOT_ALLOWED      = 405,
    RTSP_STATUS_PROXY_AUTH_REQUIRED     = 407,
    RTSP_STATUS_REQUEST_TIMEOUT         = 408,
    RTSP_STATUS_GONE                    = 410,
    RTSP_STATUS_LENGTH_REQUIRED         = 411,
    RTSP_STATUS_PRECONDITION_FAILED     = 412,
    RTSP_STATUS_UNSUPPORTED_MEDIA_TYPE  = 415,
    RTSP_STATUS_PARAM_NOT_UNDERSTOOD    = 451,
    RTSP_STATUS_SESSION_NOT_FOUND       = 454,
    RTSP_STATUS_METHOD_INVALID_IN_STATE = 455,
    RTSP_STATUS_UNSUPPORTED_TRANSPORT   = 461,
    RTSP_STATUS_INTERNAL_SERVER_ERROR   = 500,
    RTSP_STATUS_NOT_IMPLEMENTED         = 501,
    RTSP_STATUS_SERVICE_UNAVAILABLE     = 503,
    RTSP_STATUS_VERSION_NOT_SUPPORTED   = 505,
    RTSP_STATUS_OPTION_NOT_SUPPORTED    = 551
} rtsp_status_code_t;

/* L1: RTSP Transport */
typedef enum {
    RTSP_TRANSPORT_RTP_UDP           = 0,
    RTSP_TRANSPORT_RTP_TCP           = 1,
    RTSP_TRANSPORT_RAW_UDP           = 2,
    RTSP_TRANSPORT_MULTICAST         = 3,
    RTSP_TRANSPORT_RTP_UDP_MULTICAST = 4
} rtsp_transport_type_t;

typedef struct {
    rtsp_transport_type_t type;
    int client_port_rtp;
    int client_port_rtcp;
    int server_port_rtp;
    int server_port_rtcp;
    int interleaved_channel;
    int ttl;
    char destination[64];
    char source[64];
    int has_client_port;
    int has_server_port;
    int has_interleaved;
} rtsp_transport_t;

/* L1: RTSP URL */
typedef struct {
    char scheme[16];
    char username[64];
    char password[64];
    char host[256];
    int  port;
    char path[512];
    int  has_auth;
} rtsp_url_t;

/* L2: RTSP Session State Machine
 * INIT--SETUP-->READY--PLAY-->PLAYING--PAUSE-->READY
 * *--TEARDOWN-->INIT
 * READY--RECORD-->RECORDING */
typedef enum {
    RTSP_STATE_INIT      = 0,
    RTSP_STATE_READY     = 1,
    RTSP_STATE_PLAYING   = 2,
    RTSP_STATE_RECORDING = 3
} rtsp_session_state_t;

typedef struct {
    char session_id[64];
    rtsp_session_state_t state;
    rtsp_transport_t transport;
    uint32_t timeout_sec;
    time_t last_activity;
    char content_base[512];
    int active_tracks;
    uint32_t cseq;
} rtsp_session_t;

/* L2: RTSP Message */
#define RTSP_MAX_HEADER_SIZE    8192
#define RTSP_MAX_BODY_SIZE      65536
#define RTSP_MAX_URI            2048
#define RTSP_DEFAULT_PORT       554

typedef struct {
    char name[128];
    char value[1024];
} rtsp_header_t;

#define RTSP_MAX_HEADERS  64

typedef struct {
    rtsp_method_t method;
    char uri[RTSP_MAX_URI];
    int major_version;
    int minor_version;
    rtsp_header_t headers[RTSP_MAX_HEADERS];
    int header_count;
    uint8_t *body;
    size_t body_len;
} rtsp_request_t;

typedef struct {
    int major_version;
    int minor_version;
    rtsp_status_code_t status_code;
    char reason_phrase[256];
    uint32_t cseq;
    rtsp_header_t headers[RTSP_MAX_HEADERS];
    int header_count;
    uint8_t *body;
    size_t body_len;
} rtsp_response_t;

/* L2: SDP (Session Description Protocol) */
typedef struct {
    char media_type[32];
    int port;
    char protocol[32];
    char fmt_list[256];
    char rtpmap[32][128];
    int rtpmap_count;
    int payload_types[32];
    int pt_count;
} sdp_media_t;

#define SDP_MAX_MEDIA   8

typedef struct {
    char session_name[256];
    char info[512];
    char uri[512];
    char email[256];
    char phone[256];
    char connection_addr[64];
    int connection_ttl;
    uint32_t session_id;
    uint32_t session_version;
    double start_time;
    double stop_time;
    sdp_media_t media[SDP_MAX_MEDIA];
    int media_count;
    char attributes[32][256];
    int attr_count;
} sdp_description_t;

/* L5: RTSP API */
const char *rtsp_method_str(rtsp_method_t method);
rtsp_method_t rtsp_parse_method_str(const char *str);
const char *rtsp_status_str(rtsp_status_code_t code);

int rtsp_parse_url(const char *url_str, rtsp_url_t *url);
int rtsp_parse_request(const uint8_t *data, size_t len, rtsp_request_t *req);
int rtsp_format_response(const rtsp_response_t *resp, uint8_t *buf, size_t buf_size);
int rtsp_parse_transport(const char *transport_str, rtsp_transport_t *t);
int rtsp_parse_sdp(const char *sdp_text, sdp_description_t *desc);

void rtsp_session_init(rtsp_session_t *session, const char *session_id);
int rtsp_session_transition(rtsp_session_t *session, rtsp_method_t method);
int rtsp_method_allowed_in_state(rtsp_session_state_t state, rtsp_method_t method);
const char *rtsp_find_header(const rtsp_header_t *headers, int header_count,
                              const char *name);

#endif /* RTSP_H */
