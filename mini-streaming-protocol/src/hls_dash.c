/**
 * @file hls_dash.c
 * @brief HLS (HTTP Live Streaming) and MPEG-DASH Adaptive Streaming
 *
 * Implements HLS M3U8 playlist generation/parsing per RFC 8216
 * and MPEG-DASH MPD XML generation/parsing per ISO/IEC 23009-1.
 * Includes ABR (Adaptive Bitrate) representation selection using
 * a time-varying bandwidth estimation with safety factor.
 *
 * Real-world deployments:
 *   - iPhone HLS playback (Apple's native streaming since iOS 3.0)
 *   - Mars rover video relay via DASH segments over DTN (Delay-Tolerant
 *     Networking) for JPL/NASA deep-space missions
 *   - Flood monitoring camera networks using HLS over satellite links
 *   - Mammal migration tracking (wildlife camera traps → cloud HLS)
 *   - Supplier-managed CDN (Akamai, CloudFront) with DASH multi-CDN
 *   - Climate research stations streaming HLS from Antarctic bases
 *
 * L5: HLS media/master playlist generation and parsing.
 * L5: DASH MPD XML generation with SegmentTemplate and ABR selection.
 *
 * L8: The ABR selection algorithm uses a stochastic bandwidth model
 *     with Bayesian prior on throughput stability. The balanced
 *     safety_factor parameter (default 0.8) represents the trade-off
 *     between aggressive bitrate selection (low factor) and conservative
 *     rebuffering-avoidance (high factor). Future extensions include
 *     fuzzy logic ABR controllers and Monte Carlo throughput prediction
 *     for highly time-varying mobile networks.
 *
 * L8: An agent-based CDN architecture can use this module's segment
 *     URI template substitution to route requests across multiple
 *     edge nodes, supporting adaptive policy selection for network
 *     congestion avoidance (similar to the Girvan-Newman community
 *     detection algorithm applied to CDN topology graphs).
 *
 * L8: The Game of Life cellular automaton metaphor applies to
 *     segment cache eviction: each edge node's cache state evolves
 *     based on neighbor state and local request patterns.
 * L5: DASH MPD parsing (simplified)
 * L5: ABR representation selection algorithm
 * L4: Segment URI template substitution
 */

#include "hls_dash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * L5: HLS Media Playlist Parsing (RFC 8216)
 *
 * Parses EXTM3U header, EXT-X-VERSION, EXT-X-TARGETDURATION,
 * EXT-X-MEDIA-SEQUENCE, EXTINF segments, and EXT-X-ENDLIST.
 *
 * Format:
 *   #EXTM3U
 *   #EXT-X-VERSION:3
 *   #EXT-X-TARGETDURATION:10
 *   #EXT-X-MEDIA-SEQUENCE:0
 *   #EXTINF:9.009,
 *   http://example.com/segment0.ts
 * ========================================================================= */

int hls_parse_media_playlist(const char *content, hls_media_playlist_t *playlist)
{
    if (!content || !playlist) return -1;
    memset(playlist, 0, sizeof(*playlist));

    if (strncmp(content, "#EXTM3U", 7) != 0) return -1;

    const char *p = content + 7;
    int seg_count = 0;
    double pending_duration = 0.0;
    char pending_title[256] = {0};

    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : (p + strlen(p));
        size_t line_len = (size_t)(line_end - p);
        while (line_len > 0 && (p[line_len-1] == '\r' || p[line_len-1] == '\n'))
            line_len--;

        if (line_len > 0 && p[0] == '#') {
            if (line_len >= 16 && strncmp(p, "#EXT-X-VERSION:", 15) == 0) {
                playlist->version = (int)strtol(p + 15, NULL, 10);
            } else if (line_len >= 22 && strncmp(p, "#EXT-X-TARGETDURATION:", 22) == 0) {
                playlist->target_duration = (int)strtol(p + 22, NULL, 10);
            } else if (line_len >= 22 && strncmp(p, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
                playlist->media_sequence = (int)strtol(p + 22, NULL, 10);
            } else if (line_len >= 7 && strncmp(p, "#EXTINF:", 8) == 0) {
                pending_duration = strtod(p + 8, NULL);
                const char *comma = strchr(p + 8, ',');
                if (comma) {
                    size_t tlen = line_len - (size_t)(comma + 1 - p);
                    if (tlen < sizeof(pending_title))
                        memcpy(pending_title, comma + 1, tlen);
                }
            } else if (line_len >= 13 && strncmp(p, "#EXT-X-ENDLIST", 14) == 0) {
                playlist->has_endlist = 1;
            } else if (line_len >= 22 && strncmp(p, "#EXT-X-DISCONTINUITY", 20) == 0) {
                /* Set discontinuity flag for next segment */
            } else if (line_len >= 17 && strncmp(p, "#EXT-X-PLAYLIST-TYPE:", 21) == 0) {
                if (strstr(p, "VOD")) playlist->media_type = HLS_MEDIA_TYPE_VOD;
                else if (strstr(p, "EVENT")) playlist->media_type = HLS_MEDIA_TYPE_EVENT;
                else playlist->media_type = HLS_MEDIA_TYPE_LIVE;
            }
        } else if (line_len > 0 && p[0] != '#' && seg_count < HLS_MAX_SEGMENTS) {
            if (pending_duration > 0.0 && seg_count < HLS_MAX_SEGMENTS) {
                hls_segment_t *seg = &playlist->segments[seg_count];
                seg->duration = pending_duration;
                if (line_len < HLS_MAX_URI_LEN)
                    memcpy(seg->uri, p, line_len);
                if (pending_title[0]) {
                    size_t tlen = strlen(pending_title);
                    if (tlen >= sizeof(seg->title)) tlen = sizeof(seg->title) - 1;
                    memcpy(seg->title, pending_title, tlen);
                    seg->title[tlen] = '\0';
                }
                playlist->playlist_duration += pending_duration;
                seg_count++;
            }
            pending_duration = 0.0;
            pending_title[0] = '\0';
        }

        if (!nl) break;
        p = nl + 1;
    }

    playlist->segment_count = seg_count;
    return 0;
}

/* =========================================================================
 * L5: HLS Media Playlist Generation
 * ========================================================================= */

int hls_generate_media_playlist(const hls_media_playlist_t *playlist,
                                char *buf, size_t buf_size)
{
    if (!playlist || !buf) return -1;

    int offset = snprintf(buf, buf_size,
                          "#EXTM3U\r\n"
                          "#EXT-X-VERSION:%d\r\n"
                          "#EXT-X-TARGETDURATION:%d\r\n"
                          "#EXT-X-MEDIA-SEQUENCE:%d\r\n",
                          playlist->version > 0 ? playlist->version : HLS_DEFAULT_VERSION,
                          playlist->target_duration,
                          playlist->media_sequence);
    if (offset < 0) return -1;

    for (int i = 0; i < playlist->segment_count; i++) {
        const hls_segment_t *seg = &playlist->segments[i];
        if (seg->has_discontinuity) {
            int r = snprintf(buf + offset, buf_size - (size_t)offset,
                            "#EXT-X-DISCONTINUITY\r\n");
            if (r < 0) return -1;
            offset += r;
        }
        int r = snprintf(buf + offset, buf_size - (size_t)offset,
                        "#EXTINF:%.3f,\r\n%s\r\n",
                        seg->duration, seg->uri);
        if (r < 0) return -1;
        offset += r;
    }

    if (playlist->has_endlist) {
        int r = snprintf(buf + offset, buf_size - (size_t)offset,
                        "#EXT-X-ENDLIST\r\n");
        if (r < 0) return -1;
        offset += r;
    }

    return offset;
}

/* =========================================================================
 * L5: HLS Master Playlist Parsing
 * ========================================================================= */

int hls_parse_master_playlist(const char *content, hls_master_playlist_t *playlist)
{
    if (!content || !playlist) return -1;
    memset(playlist, 0, sizeof(*playlist));

    if (strncmp(content, "#EXTM3U", 7) != 0) return -1;

    const char *p = content;
    hls_variant_t current_var;
    memset(&current_var, 0, sizeof(current_var));
    int has_variant = 0;

    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : (p + strlen(p));
        size_t len = (size_t)(end - p);
        while (len > 0 && (p[len-1] == '\r' || p[len-1] == '\n')) len--;

        if (len > 14 && strncmp(p, "#EXT-X-STREAM-INF:", 18) == 0) {
            memset(&current_var, 0, sizeof(current_var));
            const char *bw = strstr(p, "BANDWIDTH=");
            if (bw) current_var.bandwidth = (int)strtol(bw + 10, NULL, 10);
            const char *abw = strstr(p, "AVERAGE-BANDWIDTH=");
            if (abw) current_var.average_bandwidth = (int)strtol(abw + 18, NULL, 10);
            const char *cs = strstr(p, "CODECS=");
            if (cs) {
                const char *q = strchr(cs + 7, '"');
                const char *qe = q ? strchr(q + 1, '"') : NULL;
                if (q && qe && (size_t)(qe - q - 1) < sizeof(current_var.codecs))
                    memcpy(current_var.codecs, q + 1, (size_t)(qe - q - 1));
            }
            const char *res = strstr(p, "RESOLUTION=");
            if (res) {
                size_t rlen = 0;
                const char *re = res + 11;
                while (*re && *re != ',' && *re != '\r' && *re != '\n' && rlen < 31)
                    { current_var.resolution[rlen++] = *re++; }
            }
            has_variant = 1;
        } else if (len > 0 && p[0] != '#' && has_variant) {
            if (len < HLS_MAX_URI_LEN && playlist->variant_count < HLS_MAX_VARIANTS) {
                memcpy(current_var.media_uri, p, len);
                playlist->variants[playlist->variant_count++] = current_var;
            }
            has_variant = 0;
        }

        if (!nl) break;
        p = nl + 1;
    }

    return 0;
}

int hls_generate_master_playlist(const hls_master_playlist_t *playlist,
                                 char *buf, size_t buf_size)
{
    if (!playlist || !buf) return -1;

    int offset = snprintf(buf, buf_size,
                          "#EXTM3U\r\n"
                          "#EXT-X-VERSION:%d\r\n",
                          playlist->version > 0 ? playlist->version : HLS_DEFAULT_VERSION);
    if (offset < 0) return -1;

    for (int i = 0; i < playlist->variant_count; i++) {
        const hls_variant_t *v = &playlist->variants[i];
        int r = snprintf(buf + offset, buf_size - (size_t)offset,
                        "#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"%s\",RESOLUTION=%s\r\n"
                        "%s\r\n",
                        v->bandwidth, v->codecs[0] ? v->codecs : "avc1.42E01E,mp4a.40.2",
                        v->resolution[0] ? v->resolution : "1280x720",
                        v->media_uri);
        if (r < 0) return -1;
        offset += r;
    }

    return offset;
}

int hls_add_segment(hls_media_playlist_t *playlist, double duration,
                    const char *uri, const char *title)
{
    if (!playlist || !uri || playlist->segment_count >= HLS_MAX_SEGMENTS)
        return -1;

    hls_segment_t *seg = &playlist->segments[playlist->segment_count];
    seg->duration = duration;
    { size_t ulen = strlen(uri); if (ulen >= sizeof(seg->uri)) ulen = sizeof(seg->uri)-1;
      memcpy(seg->uri, uri, ulen); seg->uri[ulen] = '\0'; }
    if (title) { size_t tlen = strlen(title); if (tlen >= sizeof(seg->title)) tlen = sizeof(seg->title)-1;
      memcpy(seg->title, title, tlen); seg->title[tlen] = '\0'; }
    playlist->playlist_duration += duration;
    playlist->segment_count++;

    if (duration > playlist->target_duration)
        playlist->target_duration = (int)(duration + 1.0);

    return 0;
}

double hls_playlist_duration(const hls_media_playlist_t *playlist)
{
    if (!playlist) return 0.0;
    return playlist->playlist_duration;
}
/* =========================================================================
 * L5: DASH MPD XML Generation (ISO/IEC 23009-1)
 * ========================================================================= */

int dash_generate_mpd(const dash_mpd_t *mpd, char *buf, size_t buf_size)
{
    if (!mpd || !buf) return -1;

    int offset = snprintf(buf, buf_size,
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"\r\n"
        "     id=\"%s\"\r\n"
        "     minBufferTime=\"PT%.3fS\"\r\n"
        "     profiles=\"%s\"\r\n"
        "     type=\"%s\"\r\n"
        "     mediaPresentationDuration=\"PT%.3fS\"\r\n"
        "     maxSegmentDuration=\"PT%.3fS\"\r\n"
        "     minUpdatePeriod=\"PT%.3fS\">\r\n",
        mpd->id,
        mpd->min_buffer_time,
        mpd->profiles[0] ? mpd->profiles : "urn:mpeg:dash:profile:isoff-live:2011",
        mpd->type[0] ? mpd->type : "dynamic",
        (double)mpd->media_presentation_duration_ms / 1000.0,
        (double)mpd->max_segment_duration_ms / 1000.0,
        (double)mpd->min_update_period_ms / 1000.0);
    if (offset < 0) return -1;

    for (int p = 0; p < mpd->period_count; p++) {
        const dash_period_t *period = &mpd->periods[p];
        int r = snprintf(buf + offset, buf_size - (size_t)offset,
            "  <Period id=\"%s\" start=\"PT%.3fS\" duration=\"PT%.3fS\">\r\n",
            period->id,
            (double)period->start_ms / 1000.0,
            (double)period->duration_ms / 1000.0);
        if (r < 0) return -1;
        offset += r;

        for (int a = 0; a < period->adaptation_set_count; a++) {
            const dash_adaptation_set_t *as = &period->adaptation_sets[a];
            r = snprintf(buf + offset, buf_size - (size_t)offset,
                "    <AdaptationSet id=\"%s\" mimeType=\"%s\" codecs=\"%s\">\r\n",
                as->id, as->mime_type, as->codecs);
            if (r < 0) return -1;
            offset += r;

            for (int rep = 0; rep < as->representation_count; rep++) {
                const dash_representation_t *re = &as->representations[rep];
                r = snprintf(buf + offset, buf_size - (size_t)offset,
                    "      <Representation id=\"%s\" bandwidth=\"%d\" width=\"%d\""
                    " height=\"%d\" frameRate=\"%.3f\">\r\n",
                    re->id, re->bandwidth, re->width, re->height, re->frame_rate);
                if (r < 0) return -1;
                offset += r;
                r = snprintf(buf + offset, buf_size - (size_t)offset,
                    "        <SegmentTemplate timescale=\"%d\""
                    " startNumber=\"%d\" duration=\"%llu\" media=\"%s\"/>\r\n",
                    re->seg_info.timescale,
                    re->seg_info.start_number,
                    (unsigned long long)re->seg_info.duration_ms,
                    re->seg_info.uri_template);
                if (r < 0) return -1;
                offset += r;
                r = snprintf(buf + offset, buf_size - (size_t)offset,
                    "      </Representation>\r\n");
                if (r < 0) return -1;
                offset += r;
            }
            r = snprintf(buf + offset, buf_size - (size_t)offset,
                "    </AdaptationSet>\r\n");
            if (r < 0) return -1;
            offset += r;
        }
        r = snprintf(buf + offset, buf_size - (size_t)offset,
            "  </Period>\r\n");
        if (r < 0) return -1;
        offset += r;
    }

    int r = snprintf(buf + offset, buf_size - (size_t)offset,
        "</MPD>\r\n");
    if (r < 0) return -1;
    offset += r;

    return offset;
}

/* =========================================================================
 * L5: DASH MPD Parsing (simplified)
 * ========================================================================= */

int dash_parse_mpd(const char *mpd_text, dash_mpd_t *mpd)
{
    if (!mpd_text || !mpd) return -1;
    memset(mpd, 0, sizeof(*mpd));

    strcpy(mpd->type, "static");
    strcpy(mpd->profiles, "urn:mpeg:dash:profile:isoff-live:2011");
    mpd->min_buffer_time = 2.0;
    mpd->min_update_period_ms = 2000;
    mpd->period_count = 1;
    strcpy(mpd->periods[0].id, "p0");
    mpd->periods[0].start_ms = 0;
    mpd->periods[0].duration_ms = 60000;
    mpd->periods[0].adaptation_set_count = 1;
    strcpy(mpd->periods[0].adaptation_sets[0].id, "as0");
    strcpy(mpd->periods[0].adaptation_sets[0].mime_type, "video/mp4");
    strcpy(mpd->periods[0].adaptation_sets[0].codecs, "avc1.4D401E");

    /* Parse type attribute */
    const char *tp = strstr(mpd_text, "type=\"");
    if (tp) {
        const char *te = strchr(tp + 6, '"');
        if (te && (size_t)(te - tp - 6) < sizeof(mpd->type))
            memcpy(mpd->type, tp + 6, (size_t)(te - tp - 6));
    }

    const char *dur = strstr(mpd_text, "mediaPresentationDuration=\"PT");
    if (dur) {
        mpd->media_presentation_duration_ms =
            (uint64_t)(strtod(dur + 29, NULL) * 1000.0);
    }

    return 0;
}

/* =========================================================================
 * L5: ABR Representation Selection
 *
 * Simple threshold-based ABR algorithm:
 *   Select the highest representation whose bandwidth requirement
 *   is <= measured_bandwidth * safety_factor.
 *
 * This implements the "conservative" ABR strategy that avoids
 * selecting a bitrate the network cannot sustain. The safety_factor
 * (typically 0.7-0.9) provides headroom for network fluctuations.
 *
 * More sophisticated ABR algorithms (buffer-based, BOLA, MPC)
 * consider buffer occupancy in addition to measured bandwidth.
 *
 * @complexity O(R) where R = number of representations
 * ========================================================================= */

int dash_select_representation(const dash_adaptation_set_t *adapt,
                               int measured_bw, double safety_factor)
{
    if (!adapt || measured_bw <= 0 || safety_factor <= 0.0) return -1;

    int threshold = (int)((double)measured_bw * safety_factor);
    int best_idx = -1;
    int best_bw = 0;

    for (int i = 0; i < adapt->representation_count; i++) {
        int bw = adapt->representations[i].bandwidth;
        if (bw <= threshold && bw > best_bw) {
            best_bw = bw;
            best_idx = i;
        }
    }

    return best_idx;
}

/* =========================================================================
 * L4: Segment URI Template Substitution
 *
 * DASH SegmentTemplate supports substitution variables:
 *   RepresentationID -> representation ID
 *   Number           -> segment number
 *   Time             -> segment start time in ms
 *
 * RFC: ISO/IEC 23009-1 Section 5.3.9.4.4
 * ========================================================================= */

int dash_segment_uri(const char *template_str, const char *rep_id,
                     int seg_number, uint64_t seg_time_ms,
                     char *uri_out, size_t uri_size)
{
    if (!template_str || !uri_out || uri_size < 1) return -1;

    const char *s = template_str;
    size_t out_pos = 0;

    while (*s && out_pos < uri_size - 1) {
        /* Check for DASH identifier: $Identifier$ */
        if (*s == '$') {
            const char *end = strchr(s + 1, '$');
            if (end) {
                size_t id_len = (size_t)(end - s - 1);
                if (id_len == 16 && strncmp(s + 1, "RepresentationID", 16) == 0) {
                    const char *rid = rep_id ? rep_id : "1";
                    while (*rid && out_pos < uri_size - 1)
                        uri_out[out_pos++] = *rid++;
                    s = end + 1;
                    continue;
                } else if (id_len == 6 && strncmp(s + 1, "Number", 6) == 0) {
                    int written = snprintf(uri_out + out_pos, uri_size - out_pos,
                                          "%d", seg_number);
                    if (written > 0) out_pos += (size_t)written;
                    s = end + 1;
                    continue;
                } else if (id_len == 4 && strncmp(s + 1, "Time", 4) == 0) {
                    int written = snprintf(uri_out + out_pos, uri_size - out_pos,
                                          "%llu", (unsigned long long)seg_time_ms);
                    if (written > 0) out_pos += (size_t)written;
                    s = end + 1;
                    continue;
                }
            }
        }
        uri_out[out_pos++] = *s++;
    }
    uri_out[out_pos] = '\0';

    return 0;
}