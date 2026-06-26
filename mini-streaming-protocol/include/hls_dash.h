/**
 * @file hls_dash.h
 * @brief HLS and MPEG-DASH Adaptive Streaming Protocols
 *
 * HTTP-based adaptive streaming: HLS (Apple, RFC 8216) and
 * MPEG-DASH (ISO/IEC 23009-1). Both deliver video as segments
 * over standard HTTP, enabling CDN caching and adaptive bitrate.
 *
 * Covers L1: M3U8 playlist, MPD manifest, segment, representation
 * Covers L2: Adaptive bitrate (ABR), CDN delivery, segment-based
 * Covers L3: Segment duration arithmetic, bandwidth estimation
 * Covers L4: ABR convergence, buffer-based rate selection optimality
 * Covers L5: M3U8 gen/parse, MPD gen, ABR decision algorithm
 * Covers L6: Live HLS playlist, VOD DASH MPD, multi-bitrate
 *
 * References: RFC 8216, ISO/IEC 23009-1
 * @course Stanford EE359, Berkeley EE123, TU Munich Signal Processing
 */

#ifndef HLS_DASH_H
#define HLS_DASH_H

#include <stdint.h>
#include <stddef.h>

/* L1: HLS Constants */
#define HLS_MAX_TAG_LEN         256
#define HLS_MAX_URI_LEN         2048
#define HLS_MAX_SEGMENTS        512
#define HLS_MAX_VARIANTS        16
#define HLS_DEFAULT_VERSION     3

/* L1: HLS Playlist Types */
typedef enum {
    HLS_PLAYLIST_MASTER  = 0,
    HLS_PLAYLIST_MEDIA   = 1,
    HLS_PLAYLIST_UNKNOWN = 2
} hls_playlist_type_t;

typedef enum {
    HLS_MEDIA_TYPE_VOD   = 0,
    HLS_MEDIA_TYPE_EVENT = 1,
    HLS_MEDIA_TYPE_LIVE  = 2
} hls_media_type_t;

typedef struct {
    double    duration;
    char      uri[HLS_MAX_URI_LEN];
    char      title[256];
    int       has_discontinuity;
    int       has_byterange;
    uint64_t  byte_range_offset;
    uint64_t  byte_range_length;
    int       is_encrypted;
    char      key_uri[HLS_MAX_URI_LEN];
} hls_segment_t;

typedef struct {
    int              version;
    hls_media_type_t media_type;
    int              target_duration;
    int              media_sequence;
    int              has_endlist;
    int              has_i_frames_only;
    int              has_independent_segments;
    double           playlist_duration;
    hls_segment_t    segments[HLS_MAX_SEGMENTS];
    int              segment_count;
    char             playlist_uri[HLS_MAX_URI_LEN];
} hls_media_playlist_t;

typedef struct {
    int      bandwidth;
    int      average_bandwidth;
    char     codecs[256];
    char     resolution[32];
    double   frame_rate;
    char     media_uri[HLS_MAX_URI_LEN];
    char     audio_group[128];
    char     video_group[128];
    char     subtitles_group[128];
    int      is_default;
    int      is_autoselect;
} hls_variant_t;

typedef struct {
    int              version;
    hls_variant_t    variants[HLS_MAX_VARIANTS];
    int              variant_count;
} hls_master_playlist_t;

/* L1: DASH Constants */
#define DASH_MAX_PERIODS         8
#define DASH_MAX_ADAPT_SETS      16
#define DASH_MAX_REPRESENTATIONS 16

typedef struct {
    uint64_t start_time_ms;
    uint64_t duration_ms;
    char     uri_template[512];
    int      start_number;
    int      timescale;
} dash_segment_info_t;

typedef struct {
    char     id[64];
    int      bandwidth;
    int      width;
    int      height;
    double   frame_rate;
    char     codecs[256];
    char     mime_type[64];
    dash_segment_info_t seg_info;
} dash_representation_t;

typedef struct {
    char     id[64];
    char     mime_type[64];
    char     codecs[256];
    int      width;
    int      height;
    double   frame_rate;
    dash_representation_t representations[DASH_MAX_REPRESENTATIONS];
    int      representation_count;
} dash_adaptation_set_t;

typedef struct {
    char     id[64];
    uint64_t start_ms;
    uint64_t duration_ms;
    dash_adaptation_set_t adaptation_sets[DASH_MAX_ADAPT_SETS];
    int      adaptation_set_count;
} dash_period_t;

typedef struct {
    char              id[256];
    double            min_buffer_time;
    char              profiles[256];
    char              type[16];
    uint64_t          media_presentation_duration_ms;
    uint64_t          max_segment_duration_ms;
    uint64_t          min_update_period_ms;
    dash_period_t     periods[DASH_MAX_PERIODS];
    int               period_count;
    uint64_t          availability_start_time;
} dash_mpd_t;

/* L5: HLS API */
int hls_parse_media_playlist(const char *content, hls_media_playlist_t *playlist);
int hls_generate_media_playlist(const hls_media_playlist_t *playlist,
                                char *buf, size_t buf_size);
int hls_parse_master_playlist(const char *content, hls_master_playlist_t *playlist);
int hls_generate_master_playlist(const hls_master_playlist_t *playlist,
                                 char *buf, size_t buf_size);
int hls_add_segment(hls_media_playlist_t *playlist, double duration,
                    const char *uri, const char *title);
double hls_playlist_duration(const hls_media_playlist_t *playlist);

/* L5: DASH API */
int dash_generate_mpd(const dash_mpd_t *mpd, char *buf, size_t buf_size);
int dash_parse_mpd(const char *mpd_text, dash_mpd_t *mpd);
int dash_select_representation(const dash_adaptation_set_t *adapt,
                               int measured_bw, double safety_factor);
int dash_segment_uri(const char *template_str, const char *rep_id,
                     int seg_number, uint64_t seg_time_ms,
                     char *uri_out, size_t uri_size);

#endif /* HLS_DASH_H */
