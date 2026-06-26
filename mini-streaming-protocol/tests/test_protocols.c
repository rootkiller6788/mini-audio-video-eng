/**
 * @file test_protocols.c
 * @brief Comprehensive test suite for mini-streaming-protocol
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "rtp.h"
#include "rtcp.h"
#include "rtsp.h"
#include "mpeg_ts.h"
#include "hls_dash.h"
#include "session.h"
#include "jitter_buffer.h"

static int passed = 0, failed = 0;
#define T(n) printf("  TEST: %s ... ", n)
#define P() do{printf("PASS\n");passed++;}while(0)
#define F(m) do{printf("FAIL: %s\n",m);failed++;return;}while(0)
#define C(c,m) do{if(!(c)){F(m);}}while(0)

static void test_rtp_seq(void){
  T("rtp_seq_lt normal"); C(rtp_seq_lt(1,2),"1<2"); C(!rtp_seq_lt(2,1),"!2<1"); P();
  T("rtp_seq_lt wrap"); C(rtp_seq_lt(65534,2),"65534<2 wrap"); P();
  T("rtp_seq_delta"); C(rtp_seq_delta(0,10)==10,"delta=10"); P();
}
static void test_rtp_hdr(void){
  T("rtp_header_init"); rtp_header_t h; rtp_header_init(&h,96,100,12345,0xDEADBEEF);
  C(h.version==2,"v2"); C(h.payload_type==96,"pt"); C(h.sequence==100,"seq"); P();
}
static void test_rtp_serde(void){
  T("rtp_serialize+parse roundtrip");
  rtp_packet_t pi; memset(&pi,0,sizeof(pi));
  pi.header.version=2; pi.header.payload_type=96; pi.header.sequence=42;
  pi.header.timestamp=98765; pi.header.ssrc=0x12345678;
  pi.payload=(const uint8_t*)"Hello"; pi.payload_len=5;
  uint8_t buf[512]; int l=rtp_serialize(&pi,buf,sizeof(buf)); C(l>0,"ser");
  rtp_packet_t po; C(rtp_parse(buf,(size_t)l,&po)==0,"parse");
  C(po.header.sequence==42,"seq"); C(po.payload_len==5,"plen"); P();
}
static void test_rtp_pt(void){
  T("rtp_pt_clock_rate"); C(rtp_pt_clock_rate(RTP_PT_PCMU)==8000,"PCMU 8k");
  C(rtp_pt_clock_rate(RTP_PT_MPV)==90000,"MPV 90k"); P();
}
static void test_rtp_ntp(void){
  T("rtp_ts_to_ntp"); uint32_t s,f; rtp_ts_to_ntp(90000,90000,&s,&f);
  C(s==1,"sec"); C(f==0,"frac"); P();
}
static void test_rtp_fua(void){
  T("rtp_fua_fragment+reassemble");
  uint8_t nal[200]; nal[0]=0x65;
  for(int i=1;i<200;i++) nal[i]=(uint8_t)i;
  uint8_t *frags[10]; size_t flens[10];
  int n=rtp_h264_fua_fragment(nal,200,100,frags,flens,10);
  C(n>1,"multi frag"); uint8_t rb[400]; size_t rl=sizeof(rb);
  C(rtp_h264_fua_reassemble((const uint8_t*const*)frags,flens,n,rb,&rl)==0,"reassemble");
  C(rl==200,"len"); C(memcmp(rb,nal,200)==0,"data");
  for(int i=0;i<n;i++) { free(frags[i]); } P();
}
static void test_rtp_stap(void){
  T("rtp_stap_a"); uint8_t n1[]={0x67,0x42,0x00,0x0A},n2[]={0x68,0xCE,0x01};
  const uint8_t *ns[]={n1,n2}; size_t nl[]={4,3};
  uint8_t buf[256]; int t=rtp_h264_stap_a_aggregate(ns,nl,2,buf,sizeof(buf));
  C(t>0,"agg"); const uint8_t *ex[4]; size_t el[4];
  int c=rtp_h264_stap_a_deaggregate(buf+1,(size_t)(t-1),ex,el,4);
  C(c==2,"2 nals"); P();
}
static void test_rtcp_jit(void){
  T("rtcp_jitter"); double j=0; rtcp_jitter_init(&j);
  double nj=rtcp_jitter_update(&j,80.0); C(nj>0&&nj<80,"damped"); P();
}
static void test_rtcp_td(void){
  T("rtcp_transit_diff"); double d=rtcp_transit_diff(0,100,0.0,200.0);
  C(fabs(d-100.0)<0.001,"td=100"); P();
}
static void test_rtcp_sr(void){
  T("rtcp_build+parse SR"); rtcp_sr_block_t sr; memset(&sr,0,sizeof(sr));
  sr.ntp_sec=0x83ABCDEF; sr.ntp_frac=0x12345678; sr.rtp_timestamp=3600000;
  sr.sender_packet_count=1500; sr.sender_octet_count=2000000;
  uint8_t buf[512]; int l=rtcp_build_sr(&sr,0xAAAA5555,"user@host",buf,sizeof(buf));
  C(l>0,"build"); rtcp_compound_t cp;
  C(rtcp_parse(buf,(size_t)l,&cp)==0,"parse"); C(cp.is_sr==1,"is SR");
  C(strcmp(cp.cname,"user@host")==0,"cname"); P();
}
static void test_rtcp_fl(void){
  T("rtcp_fraction_lost"); C(rtcp_fraction_lost(0,100)==0,"0%");
  C(rtcp_fraction_lost(50,100)==128,"50%"); P();
}
static void test_rtcp_rtt(void){
  T("rtcp_compute_rtt"); double r=rtcp_compute_rtt(1000,500,100);
  C(r>0.0,"valid"); C(rtcp_compute_rtt(100,0,100)==-1.0,"LSR=0"); P();
}
static void test_rtsp_methods(void){
  T("rtsp_method_str"); C(strcmp(rtsp_method_str(RTSP_METHOD_SETUP),"SETUP")==0,"SETUP");
  C(rtsp_parse_method_str("DESCRIBE")==RTSP_METHOD_DESCRIBE,"DESCRIBE"); P();
}
static void test_rtsp_url(void){
  T("rtsp_parse_url"); rtsp_url_t u;
  C(rtsp_parse_url("rtsp://example.com:8554/stream",&u)==0,"parse");
  C(strcmp(u.host,"example.com")==0,"host"); C(u.port==8554,"port"); P();
}
static void test_rtsp_transport(void){
  T("rtsp_parse_transport"); rtsp_transport_t t;
  C(rtsp_parse_transport("RTP/AVP;unicast;client_port=5000-5001",&t)==0,"parse");
  C(t.client_port_rtp==5000,"rtp"); C(t.client_port_rtcp==5001,"rtcp"); P();
}
static void test_rtsp_req(void){
  T("rtsp_parse_request"); const char *r="DESCRIBE rtsp://srv/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n";
  rtsp_request_t req; C(rtsp_parse_request((const uint8_t*)r,strlen(r),&req)==0,"parse");
  C(req.method==RTSP_METHOD_DESCRIBE,"method"); P();
}
static void test_rtsp_resp(void){
  T("rtsp_format_response"); rtsp_response_t resp; memset(&resp,0,sizeof(resp));
  resp.major_version=1; resp.status_code=RTSP_STATUS_OK; resp.cseq=1;
  uint8_t buf[512]; C(rtsp_format_response(&resp,buf,sizeof(buf))>0,"format"); P();
}
static void test_rtsp_state(void){
  T("rtsp_state_machine"); rtsp_session_t s; rtsp_session_init(&s,"s1");
  C(s.state==RTSP_STATE_INIT,"INIT");
  C(rtsp_session_transition(&s,RTSP_METHOD_SETUP)==0,"SETUP"); C(s.state==RTSP_STATE_READY,"READY");
  C(rtsp_session_transition(&s,RTSP_METHOD_PLAY)==0,"PLAY"); C(s.state==RTSP_STATE_PLAYING,"PLAYING");
  C(rtsp_session_transition(&s,RTSP_METHOD_PAUSE)==0,"PAUSE"); C(s.state==RTSP_STATE_READY,"READY");
  C(rtsp_session_transition(&s,RTSP_METHOD_TEARDOWN)==0,"TEARDOWN"); C(s.state==RTSP_STATE_INIT,"INIT");
  C(rtsp_session_transition(&s,RTSP_METHOD_PLAY)==-1,"PLAY from INIT invalid"); P();
}
static void test_rtsp_header_find(void){
  T("rtsp_find_header"); rtsp_header_t h[]={{"CSeq","1"},{"Session","abc"}};
  C(strcmp(rtsp_find_header(h,2,"Session"),"abc")==0,"find");
  C(rtsp_find_header(h,2,"cseq")!=NULL,"case-insensitive"); P();
}
static void test_ts_hdr(void){
  T("ts_parse_header"); uint8_t pk[188]={TS_SYNC_BYTE,0x40,0x10,0x10};
  ts_header_t h; C(ts_parse_header(pk,&h)==0,"parse");
  C(h.sync_byte==TS_SYNC_BYTE,"sync"); C(h.pid==0x0010,"pid"); P();
}
static void test_ts_pts(void){
  T("ts_pts_to_seconds"); C(fabs(ts_pts_to_seconds(90000)-1.0)<0.001,"1s");
  C(fabs(ts_pts_to_seconds(45000)-0.5)<0.001,"0.5s"); P();
}
static void test_ts_dtspts(void){
  T("ts_dts_pts_invariant"); C(ts_check_dts_pts_invariant(100,200)==1,"dts<pts valid");
  C(ts_check_dts_pts_invariant(200,100)==0,"dts>pts invalid"); P();
}
static void test_hls_gen(void){
  T("hls_generate_media_playlist"); static hls_media_playlist_t pl; memset(&pl,0,sizeof(pl));
  pl.version=3; pl.target_duration=10; pl.has_endlist=1;
  hls_add_segment(&pl,9.0,"http://ex.com/s0.ts",NULL);
  hls_add_segment(&pl,8.5,"http://ex.com/s1.ts",NULL);
  char buf[4096]; int l=hls_generate_media_playlist(&pl,buf,sizeof(buf));
  C(l>0,"gen"); C(strstr(buf,"#EXTM3U")!=NULL,"EXTM3U"); P();
}
static void test_hls_parse(void){
  T("hls_parse_media_playlist");
  const char *m="#EXTM3U\r\n#EXT-X-VERSION:3\r\n#EXT-X-TARGETDURATION:10\r\n#EXTINF:9.0,\r\nhttp://x.com/s0.ts\r\n#EXT-X-ENDLIST\r\n";
  static hls_media_playlist_t pl; C(hls_parse_media_playlist(m,&pl)==0,"parse");
  C(pl.version==3,"v3"); C(pl.segment_count==1,"1seg"); P();
}
static void test_hls_dur(void){
  T("hls_playlist_duration"); static hls_media_playlist_t pl; memset(&pl,0,sizeof(pl));
  hls_add_segment(&pl,5.0,"a.ts",NULL); hls_add_segment(&pl,3.0,"b.ts",NULL);
  C(fabs(hls_playlist_duration(&pl)-8.0)<0.01,"8s"); P();
}
static void test_dash_sel(void){
  T("dash_select_representation"); dash_adaptation_set_t as; memset(&as,0,sizeof(as));
  as.representation_count=3; as.representations[0].bandwidth=500000;
  as.representations[1].bandwidth=1500000; as.representations[2].bandwidth=3000000;
  C(dash_select_representation(&as,2000000,0.8)==1,"1.5M"); P();
}
static void test_dash_uri(void){
  T("dash_segment_uri"); char u[256];
  dash_segment_uri("seg-$Number$.m4s","v",42,0,u,sizeof(u));
  C(strcmp(u,"seg-42.m4s")==0,"num"); P();
}
static void test_session(void){
  T("session_init+add_track"); streaming_session_t s; session_init(&s,"s1");
  C(session_add_track(&s,TRACK_VIDEO,CODEC_H264,NULL)==0,"video");
  C(session_add_track(&s,TRACK_AUDIO,CODEC_AAC,NULL)==1,"audio");
  C(s.track_count==2,"2tracks"); C(session_start(&s)==0,"start"); C(s.is_active==1,"active");
  session_update_sent(&s,0,1200); session_update_sent(&s,0,1100);
  const track_stats_t *st=session_get_track_stats(&s,0);
  C(st->packets_sent==2,"2pkts"); C(session_stop(&s)==0,"stop"); P();
}
static void test_session_codec(void){
  T("session_codec_mapping"); C(session_codec_to_pt(CODEC_PCMA)==RTP_PT_PCMA,"PCMA");
  C(session_codec_clock_rate(CODEC_H264)==90000,"H264 90k"); P();
}
static void test_jb(void){
  T("jitter_buffer"); jitter_buffer_t jb;
  C(jb_init(&jb,8,60.0,1.0/16.0,1.0/32.0)==0,"init");
  uint8_t d[]={0,1,2,3}; C(jb_insert(&jb,0,0,1000000,d,4)==0,"insert");
  const uint8_t *o; size_t ol;
  C(jb_extract(&jb,2000000,&o,&ol)==0,"extract"); C(ol==4,"len"); C(memcmp(o,d,4)==0,"data");
  C(jb_extract(&jb,3000000,&o,&ol)==-2,"empty underrun");
  double l=0.0,dl,ls,ur; jb_get_stats(&jb,&l,&dl,&ls,&ur); C(fabs(l)<0.001,"init jit=0");
  jb_reset(&jb); P();
}

int main(void){
  printf("\n=== mini-streaming-protocol Test Suite ===\n\n");
  test_rtp_seq(); test_rtp_hdr(); test_rtp_serde(); test_rtp_pt(); test_rtp_ntp();
  test_rtp_fua(); test_rtp_stap();
  test_rtcp_jit(); test_rtcp_td(); test_rtcp_sr(); test_rtcp_fl(); test_rtcp_rtt();
  test_rtsp_methods(); test_rtsp_url(); test_rtsp_transport();
  test_rtsp_req(); test_rtsp_resp(); test_rtsp_state(); test_rtsp_header_find();
  test_ts_hdr(); test_ts_pts(); test_ts_dtspts();
  test_hls_gen(); test_hls_parse(); test_hls_dur();
  test_dash_sel(); test_dash_uri();
  test_session(); test_session_codec();
  test_jb();
  printf("\n=== Results: %d passed, %d failed ===\n\n",passed,failed);
  return failed>0?1:0;
}