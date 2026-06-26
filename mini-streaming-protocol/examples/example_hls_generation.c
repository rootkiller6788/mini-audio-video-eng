#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hls_dash.h"

int main(void) {
    printf("=== Example: HLS Playlist Generation and DASH MPD ===\n\n");
    /* Generate HLS media playlist */
    hls_media_playlist_t playlist;
    memset(&playlist, 0, sizeof(playlist));
    playlist.version = 3;
    playlist.target_duration = 6;
    playlist.media_sequence = 100;
    playlist.has_endlist = 1;
    hls_add_segment(&playlist, 5.0, "https://cdn.example.com/live/seg100.ts", "Scene 1");
    hls_add_segment(&playlist, 4.5, "https://cdn.example.com/live/seg101.ts", "Scene 2");
    hls_add_segment(&playlist, 5.2, "https://cdn.example.com/live/seg102.ts", "Scene 3");
    printf("Total playlist duration: %.2f seconds\n\n",
           hls_playlist_duration(&playlist));

    char m3u8_buf[4096];
    int m3u8_len = hls_generate_media_playlist(&playlist, m3u8_buf, sizeof(m3u8_buf));
    printf("Generated HLS playlist (%d bytes):\n%s\n", m3u8_len, m3u8_buf);

    /* Generate DASH MPD */
    dash_mpd_t mpd;
    memset(&mpd, 0, sizeof(mpd));
    strcpy(mpd.id, "example-mpd");
    mpd.min_buffer_time = 2.0;
    strcpy(mpd.profiles, "urn:mpeg:dash:profile:isoff-live:2011");
    strcpy(mpd.type, "dynamic");
    mpd.media_presentation_duration_ms = 60000;
    mpd.max_segment_duration_ms = 4000;
    mpd.min_update_period_ms = 2000;
    mpd.period_count = 1;
    strcpy(mpd.periods[0].id, "p0");
    mpd.periods[0].duration_ms = 60000;
    mpd.periods[0].adaptation_set_count = 1;
    strcpy(mpd.periods[0].adaptation_sets[0].id, "as_video");
    strcpy(mpd.periods[0].adaptation_sets[0].mime_type, "video/mp4");
    strcpy(mpd.periods[0].adaptation_sets[0].codecs, "avc1.4D401E,mp4a.40.2");
    mpd.periods[0].adaptation_sets[0].representation_count = 2;
    /* HD rep */
    strcpy(mpd.periods[0].adaptation_sets[0].representations[0].id, "hd");
    mpd.periods[0].adaptation_sets[0].representations[0].bandwidth = 3000000;
    mpd.periods[0].adaptation_sets[0].representations[0].width = 1920;
    mpd.periods[0].adaptation_sets[0].representations[0].height = 1080;
    mpd.periods[0].adaptation_sets[0].representations[0].seg_info.timescale = 90000;
    mpd.periods[0].adaptation_sets[0].representations[0].seg_info.start_number = 1;
    mpd.periods[0].adaptation_sets[0].representations[0].seg_info.duration_ms = 2000;
    strcpy(mpd.periods[0].adaptation_sets[0].representations[0].seg_info.uri_template,
           "hd-$Number$.m4s");
    /* SD rep */
    strcpy(mpd.periods[0].adaptation_sets[0].representations[1].id, "sd");
    mpd.periods[0].adaptation_sets[0].representations[1].bandwidth = 800000;
    mpd.periods[0].adaptation_sets[0].representations[1].width = 640;
    mpd.periods[0].adaptation_sets[0].representations[1].height = 360;
    mpd.periods[0].adaptation_sets[0].representations[1].seg_info.timescale = 90000;
    mpd.periods[0].adaptation_sets[0].representations[1].seg_info.start_number = 1;
    mpd.periods[0].adaptation_sets[0].representations[1].seg_info.duration_ms = 2000;
    strcpy(mpd.periods[0].adaptation_sets[0].representations[1].seg_info.uri_template,
           "sd-$Number$.m4s");

    char mpd_buf[8192];
    int mpd_len = dash_generate_mpd(&mpd, mpd_buf, sizeof(mpd_buf));
    if (mpd_len > 0) {
        printf("\nGenerated DASH MPD (%d bytes):\n%s\n", mpd_len, mpd_buf);
    }

    /* Demonstrate ABR selection */
    dash_adaptation_set_t *as = &mpd.periods[0].adaptation_sets[0];
    int rep = dash_select_representation(as, 5000000, 0.8);
    if (rep >= 0)
        printf("ABR: Selected representation '%s' at %d bps (measured 5 Mbps)\n",
               as->representations[rep].id, as->representations[rep].bandwidth);

    return 0;
}