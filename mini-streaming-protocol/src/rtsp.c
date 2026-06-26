/**
 * @file rtsp.c
 * @brief RTSP Implementation - RFC 2326
 *
 * Real Time Streaming Protocol: URL parsing, message parsing,
 * response formatting, SDP parsing, transport negotiation,
 * and session state machine management.
 *
 * L1: RTSP methods and status code string mapping
 * L5: URL parsing (rtsp:// scheme)
 * L5: Request parsing (request-line + headers + body)
 * L5: Response formatting (status-line + headers + body)
 * L5: Transport header parsing
 * L5: SDP session description parsing
 * L2: Session state machine (INIT/READY/PLAYING/RECORDING)
 * L4: State transition validation
 */

#include "rtsp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

const char *rtsp_method_str(rtsp_method_t method)
{
    switch (method) {
    case RTSP_METHOD_OPTIONS:       return "OPTIONS";
    case RTSP_METHOD_DESCRIBE:      return "DESCRIBE";
    case RTSP_METHOD_ANNOUNCE:      return "ANNOUNCE";
    case RTSP_METHOD_SETUP:         return "SETUP";
    case RTSP_METHOD_PLAY:          return "PLAY";
    case RTSP_METHOD_PAUSE:         return "PAUSE";
    case RTSP_METHOD_TEARDOWN:      return "TEARDOWN";
    case RTSP_METHOD_GET_PARAMETER:  return "GET_PARAMETER";
    case RTSP_METHOD_SET_PARAMETER:  return "SET_PARAMETER";
    case RTSP_METHOD_REDIRECT:      return "REDIRECT";
    case RTSP_METHOD_RECORD:        return "RECORD";
    default:                        return "UNKNOWN";
    }
}

rtsp_method_t rtsp_parse_method_str(const char *str)
{
    if (!str) return RTSP_METHOD_UNKNOWN;
    if (strcmp(str, "OPTIONS") == 0)       return RTSP_METHOD_OPTIONS;
    if (strcmp(str, "DESCRIBE") == 0)      return RTSP_METHOD_DESCRIBE;
    if (strcmp(str, "ANNOUNCE") == 0)      return RTSP_METHOD_ANNOUNCE;
    if (strcmp(str, "SETUP") == 0)         return RTSP_METHOD_SETUP;
    if (strcmp(str, "PLAY") == 0)          return RTSP_METHOD_PLAY;
    if (strcmp(str, "PAUSE") == 0)         return RTSP_METHOD_PAUSE;
    if (strcmp(str, "TEARDOWN") == 0)      return RTSP_METHOD_TEARDOWN;
    if (strcmp(str, "GET_PARAMETER") == 0)  return RTSP_METHOD_GET_PARAMETER;
    if (strcmp(str, "SET_PARAMETER") == 0)  return RTSP_METHOD_SET_PARAMETER;
    if (strcmp(str, "REDIRECT") == 0)      return RTSP_METHOD_REDIRECT;
    if (strcmp(str, "RECORD") == 0)        return RTSP_METHOD_RECORD;
    return RTSP_METHOD_UNKNOWN;
}

const char *rtsp_status_str(rtsp_status_code_t code)
{
    switch (code) {
    case RTSP_STATUS_OK:            return "OK";
    case RTSP_STATUS_CREATED:       return "Created";
    case RTSP_STATUS_BAD_REQUEST:   return "Bad Request";
    case RTSP_STATUS_UNAUTHORIZED:  return "Unauthorized";
    case RTSP_STATUS_NOT_FOUND:     return "Not Found";
    case RTSP_STATUS_METHOD_NOT_ALLOWED:    return "Method Not Allowed";
    case RTSP_STATUS_SESSION_NOT_FOUND:     return "Session Not Found";
    case RTSP_STATUS_UNSUPPORTED_TRANSPORT: return "Unsupported Transport";
    case RTSP_STATUS_INTERNAL_SERVER_ERROR: return "Internal Server Error";
    case RTSP_STATUS_NOT_IMPLEMENTED:       return "Not Implemented";
    case RTSP_STATUS_VERSION_NOT_SUPPORTED: return "RTSP Version Not Supported";
    case RTSP_STATUS_METHOD_INVALID_IN_STATE: return "Method Not Valid In This State";
    default: return "Unknown Status";
    }
}

int rtsp_parse_url(const char *url_str, rtsp_url_t *url)
{
    if (!url_str || !url) return -1;
    memset(url, 0, sizeof(*url));
    url->port = RTSP_DEFAULT_PORT;

    if (strncmp(url_str, "rtsp://", 7) != 0) return -1;
    strcpy(url->scheme, "rtsp");
    const char *p = url_str + 7;

    const char *at_sign = strchr(p, '@');
    const char *slash = strchr(p, '/');

    if (at_sign && (!slash || at_sign < slash)) {
        const char *colon = memchr(p, ':', (size_t)(at_sign - p));
        if (colon) {
            size_t ulen = (size_t)(colon - p);
            if (ulen >= sizeof(url->username)) return -1;
            memcpy(url->username, p, ulen);
            url->username[ulen] = '\0';
            size_t plen = (size_t)(at_sign - colon - 1);
            if (plen >= sizeof(url->password)) return -1;
            memcpy(url->password, colon + 1, plen);
            url->password[plen] = '\0';
        } else {
            size_t ulen = (size_t)(at_sign - p);
            if (ulen >= sizeof(url->username)) return -1;
            memcpy(url->username, p, ulen);
            url->username[ulen] = '\0';
        }
        url->has_auth = 1;
        p = at_sign + 1;
    }

    const char *colon = strchr(p, ':');
    const char *host_end = slash;
    if (!host_end) host_end = strchr(p, '\0');

    if (colon && (!host_end || colon < host_end)) {
        const char *port_start = colon + 1;
        size_t host_len = (size_t)(colon - p);
        if (host_len >= sizeof(url->host)) return -1;
        memcpy(url->host, p, host_len);
        url->host[host_len] = '\0';
        url->port = (int)strtol(port_start, NULL, 10);
        if (url->port <= 0 || url->port > 65535) url->port = RTSP_DEFAULT_PORT;
    } else {
        const char *e = host_end ? host_end : (p + strlen(p));
        size_t host_len = (size_t)(e - p);
        if (host_len >= sizeof(url->host)) return -1;
        memcpy(url->host, p, host_len);
        url->host[host_len] = '\0';
    }

    if (slash) {
        size_t path_len = strlen(slash);
        if (path_len >= sizeof(url->path)) return -1;
        strcpy(url->path, slash);
    }

    return 0;
}
int rtsp_parse_request(const uint8_t *data, size_t len, rtsp_request_t *req)
{
    if (!data || !req || len < 8) return -1;
    memset(req, 0, sizeof(*req));

    const char *text = (const char *)data;
    char method_str[32] = {0};
    int n = 0;
    /* Parse method */
    for (size_t i = 0; i < len && n < 31; i++) {
        if (text[i] == ' ' || text[i] == '\r' || text[i] == '\n') break;
        if (isprint((unsigned char)text[i])) method_str[n++] = text[i];
    }
    method_str[n] = '\0';
    req->method = rtsp_parse_method_str(method_str);

    const char *url_start = strchr(text, ' ');
    if (!url_start) return -1;
    url_start++;
    const char *url_end = strchr(url_start, ' ');
    if (!url_end) return -1;
    size_t url_len = (size_t)(url_end - url_start);
    if (url_len >= RTSP_MAX_URI) return -1;
    memcpy(req->uri, url_start, url_len);
    req->uri[url_len] = '\0';

    /* Parse version: RTSP/1.0 */
    const char *ver = strstr(url_end, "RTSP/");
    if (!ver) return -1;
    req->major_version = (int)(ver[5] - '0');
    req->minor_version = (ver[7] >= '0' && ver[7] <= '9') ? (int)(ver[7] - '0') : 0;

    /* Parse headers */
    const char *hdr_start = strstr(url_end, "\r\n");
    if (!hdr_start) return -1;
    hdr_start += 2;

    const char *body_sep = strstr(hdr_start, "\r\n\r\n");
    const char *hdr_end = body_sep ? body_sep : (text + len);

    const char *line = hdr_start;
    while (line < hdr_end && req->header_count < RTSP_MAX_HEADERS) {
        const char *nl = strstr(line, "\r\n");
        if (!nl || nl >= hdr_end) break;
        if (nl == line) { line += 2; continue; }

        const char *colon = memchr(line, ':', (size_t)(nl - line));
        if (colon) {
            size_t name_len = (size_t)(colon - line);
            if (name_len < sizeof(req->headers[0].name)) {
                memcpy(req->headers[req->header_count].name, line, name_len);
                req->headers[req->header_count].name[name_len] = '\0';

                const char *val = colon + 1;
                while (val < nl && (*val == ' ' || *val == '\t')) val++;
                size_t val_len = (size_t)(nl - val);
                if (val_len < sizeof(req->headers[0].value)) {
                    memcpy(req->headers[req->header_count].value, val, val_len);
                    req->headers[req->header_count].value[val_len] = '\0';
                }
                req->header_count++;
            }
        }
        line = nl + 2;
    }

    /* Parse body */
    if (body_sep) {
        const char *body_start = body_sep + 4;
        size_t body_len = len - (size_t)(body_start - text);
        req->body = (uint8_t *)body_start;
        req->body_len = body_len;
    }

    return 0;
}

int rtsp_format_response(const rtsp_response_t *resp, uint8_t *buf, size_t buf_size)
{
    if (!resp || !buf) return -1;

    const char *status_text = rtsp_status_str(resp->status_code);
    int written = snprintf((char *)buf, buf_size,
                           "RTSP/%d.%d %d %s\r\n"
                           "CSeq: %u\r\n",
                           resp->major_version, resp->minor_version,
                           (int)resp->status_code, status_text,
                           resp->cseq);
    if (written < 0) return -1;
    size_t offset = (size_t)written;

    for (int i = 0; i < resp->header_count && offset < buf_size; i++) {
        int hdr_written = snprintf((char *)buf + offset, buf_size - offset,
                                   "%s: %s\r\n",
                                   resp->headers[i].name,
                                   resp->headers[i].value);
        if (hdr_written < 0) return -1;
        offset += (size_t)hdr_written;
    }

    if (resp->body && resp->body_len > 0) {
        int body_hdr = snprintf((char *)buf + offset, buf_size - offset,
                                "Content-Length: %zu\r\n\r\n", resp->body_len);
        if (body_hdr < 0) return -1;
        offset += (size_t)body_hdr;
        if (offset + resp->body_len > buf_size) return -1;
        memcpy(buf + offset, resp->body, resp->body_len);
        offset += resp->body_len;
    } else {
        if (offset + 2 > buf_size) return -1;
        buf[offset++] = '\r';
        buf[offset++] = '\n';
    }

    return (int)offset;
}
int rtsp_parse_transport(const char *transport_str, rtsp_transport_t *t)
{
    if (!transport_str || !t) return -1;
    memset(t, 0, sizeof(*t));

    if (strstr(transport_str, "RTP/AVP")) {
        if (strstr(transport_str, "TCP") || strstr(transport_str, "tcp"))
            t->type = RTSP_TRANSPORT_RTP_TCP;
        else if (strstr(transport_str, "multicast"))
            t->type = RTSP_TRANSPORT_RTP_UDP_MULTICAST;
        else
            t->type = RTSP_TRANSPORT_RTP_UDP;
    } else if (strstr(transport_str, "RAW/RAW/UDP") || strstr(transport_str, "RAW")) {
        t->type = RTSP_TRANSPORT_RAW_UDP;
    }

    const char *cp = strstr(transport_str, "client_port=");
    if (cp) {
        t->has_client_port = 1;
        cp += 12;
        char *dash = strchr(cp, '-');
        if (dash) {
            t->client_port_rtp = (int)strtol(cp, NULL, 10);
            t->client_port_rtcp = (int)strtol(dash + 1, NULL, 10);
        } else {
            t->client_port_rtp = (int)strtol(cp, NULL, 10);
            t->client_port_rtcp = t->client_port_rtp + 1;
        }
    }

    const char *sp = strstr(transport_str, "server_port=");
    if (sp) {
        t->has_server_port = 1;
        sp += 12;
        char *dash = strchr(sp, '-');
        if (dash) {
            t->server_port_rtp = (int)strtol(sp, NULL, 10);
            t->server_port_rtcp = (int)strtol(dash + 1, NULL, 10);
        }
    }

    const char *ic = strstr(transport_str, "interleaved=");
    if (ic) {
        t->has_interleaved = 1;
        ic += 12;
        t->interleaved_channel = (int)strtol(ic, NULL, 10);
    }

    return 0;
}

int rtsp_parse_sdp(const char *sdp_text, sdp_description_t *desc)
{
    if (!sdp_text || !desc) return -1;
    memset(desc, 0, sizeof(*desc));

    const char *p = sdp_text;
    int media_idx = -1;

    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : (p + strlen(p));
        size_t line_len = (size_t)(line_end - p);
        while (line_len > 0 && (p[line_len-1] == '\r' || p[line_len-1] == '\n'))
            line_len--;

        if (line_len >= 2 && p[1] == '=') {
            char type = (char)toupper((unsigned char)p[0]);
            const char *val = p + 2;

            switch (type) {
            case 'S':
                if (line_len - 2 < (size_t)sizeof(desc->session_name))
                    memcpy(desc->session_name, val, line_len - 2);
                break;
            case 'I':
                if (line_len - 2 < (size_t)sizeof(desc->info))
                    memcpy(desc->info, val, line_len - 2);
                break;
            case 'U':
                if (line_len - 2 < (size_t)sizeof(desc->uri))
                    memcpy(desc->uri, val, line_len - 2);
                break;
            case 'M':
                if (desc->media_count < SDP_MAX_MEDIA) {
                    media_idx = desc->media_count++;
                    sdp_media_t *m = &desc->media[media_idx];
                    /* Parse: media port proto fmt */
                    sscanf(val, "%31s %d %31s %255s",
                           m->media_type, &m->port,
                           m->protocol, m->fmt_list);
                    /* Parse payload types from fmt_list */
                    const char *ft = m->fmt_list;
                    while (*ft && m->pt_count < 32) {
                        char *end;
                        int pt = (int)strtol(ft, &end, 10);
                        if (end != ft) {
                            m->payload_types[m->pt_count++] = pt;
                            ft = end;
                        }
                        while (*ft == ' ' || *ft == ',') ft++;
                        if (end == ft) break;
                    }
                }
                break;
            case 'A':
                if (media_idx >= 0 && strncmp(val, "rtpmap:", 7) == 0) {
                    sdp_media_t *m = &desc->media[media_idx];
                    if (m->rtpmap_count < 32) {
                        size_t rlen = line_len - 9;
                        if (rlen < sizeof(m->rtpmap[0]))
                            memcpy(m->rtpmap[m->rtpmap_count++], val + 7, rlen);
                    }
                } else if (desc->attr_count < 32) {
                    size_t alen = line_len - 2;
                    if (alen < sizeof(desc->attributes[0]))
                        memcpy(desc->attributes[desc->attr_count++], val, alen);
                }
                break;
            }
        }

        if (!nl) break;
        p = nl + 1;
    }

    return 0;
}
void rtsp_session_init(rtsp_session_t *session, const char *session_id)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->state = RTSP_STATE_INIT;
    session->timeout_sec = 60;
    session->last_activity = time(NULL);
    if (session_id) {
        strncpy(session->session_id, session_id, sizeof(session->session_id) - 1);
    }
}

int rtsp_method_allowed_in_state(rtsp_session_state_t state, rtsp_method_t method)
{
    switch (state) {
    case RTSP_STATE_INIT:
        if (method == RTSP_METHOD_SETUP) return 1;
        break;
    case RTSP_STATE_READY:
        if (method == RTSP_METHOD_PLAY ||
            method == RTSP_METHOD_RECORD ||
            method == RTSP_METHOD_TEARDOWN ||
            method == RTSP_METHOD_SETUP) return 1;
        break;
    case RTSP_STATE_PLAYING:
        if (method == RTSP_METHOD_PAUSE ||
            method == RTSP_METHOD_TEARDOWN ||
            method == RTSP_METHOD_PLAY) return 1;
        break;
    case RTSP_STATE_RECORDING:
        if (method == RTSP_METHOD_PAUSE ||
            method == RTSP_METHOD_TEARDOWN) return 1;
        break;
    }
    if (method == RTSP_METHOD_OPTIONS ||
        method == RTSP_METHOD_DESCRIBE ||
        method == RTSP_METHOD_GET_PARAMETER ||
        method == RTSP_METHOD_SET_PARAMETER) return 1;
    return 0;
}

int rtsp_session_transition(rtsp_session_t *session, rtsp_method_t method)
{
    if (!session) return -1;

    if (!rtsp_method_allowed_in_state(session->state, method))
        return -1;

    switch (method) {
    case RTSP_METHOD_SETUP:
        if (session->state == RTSP_STATE_INIT || session->state == RTSP_STATE_READY)
            session->state = RTSP_STATE_READY;
        break;
    case RTSP_METHOD_PLAY:
        if (session->state == RTSP_STATE_READY ||
            session->state == RTSP_STATE_PLAYING)
            session->state = RTSP_STATE_PLAYING;
        break;
    case RTSP_METHOD_RECORD:
        if (session->state == RTSP_STATE_READY)
            session->state = RTSP_STATE_RECORDING;
        break;
    case RTSP_METHOD_PAUSE:
        if (session->state == RTSP_STATE_PLAYING ||
            session->state == RTSP_STATE_RECORDING)
            session->state = RTSP_STATE_READY;
        break;
    case RTSP_METHOD_TEARDOWN:
        session->state = RTSP_STATE_INIT;
        break;
    default:
        break;
    }

    session->last_activity = time(NULL);
    return 0;
}

const char *rtsp_find_header(const rtsp_header_t *headers, int header_count,
                              const char *name)
{
    if (!headers || !name) return NULL;
    for (int i = 0; i < header_count; i++) {
        /* Case-insensitive comparison */
        const char *a = headers[i].name;
        const char *b = name;
        while (*a && *b) {
            if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0')
            return headers[i].value;
    }
    return NULL;
}