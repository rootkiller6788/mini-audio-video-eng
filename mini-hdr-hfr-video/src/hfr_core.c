#include "hfr_core.h"
#include <stdio.h>
#include <math.h>

hfr_frame_t *hfr_frame_alloc(int width, int height, int channels)
{
    if (width <= 0 || height <= 0 || channels <= 0) return NULL;
    hfr_frame_t *frame = (hfr_frame_t *)calloc(1, sizeof(*frame));
    if (!frame) return NULL;
    size_t total = (size_t)width * (size_t)height * (size_t)channels;
    frame->data = (double *)calloc(total, sizeof(double));
    if (!frame->data) { free(frame); return NULL; }
    frame->width = width; frame->height = height;
    frame->bit_depth = 10; frame->channels = channels;
    frame->type = HFR_FRAME_ORIGINAL; frame->scan = HFR_SCAN_PROGRESSIVE;
    frame->shutter_angle = 180.0;
    return frame;
}

void hfr_frame_free(hfr_frame_t *frame)
{
    if (!frame) return;
    if (frame->data) { free(frame->data); frame->data = NULL; }
    free(frame);
}

int hfr_frame_copy(const hfr_frame_t *src, hfr_frame_t *dst)
{
    if (!src || !dst || !src->data || !dst->data) return -1;
    if (src->width != dst->width || src->height != dst->height || src->channels != dst->channels) return -1;
    size_t total = (size_t)src->width * (size_t)src->height * (size_t)src->channels;
    memcpy(dst->data, src->data, total * sizeof(double));
    dst->pts = src->pts; dst->duration = src->duration;
    dst->frame_index = src->frame_index; dst->timestamp_sec = src->timestamp_sec;
    dst->type = src->type; dst->scan = src->scan; dst->shutter_angle = src->shutter_angle;
    return 0;
}

void hfr_frame_fill(hfr_frame_t *frame, double value)
{
    if (!frame || !frame->data) return;
    size_t total = (size_t)frame->width * (size_t)frame->height * (size_t)frame->channels;
    for (size_t i = 0; i < total; i++) frame->data[i] = value;
}

double hfr_frame_pixel_get(const hfr_frame_t *frame, int x, int y, int channel)
{
    if (!frame || !frame->data) return 0.0;
    if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return 0.0;
    if (channel < 0 || channel >= frame->channels) return 0.0;
    size_t idx = ((size_t)y * (size_t)frame->width + (size_t)x) * (size_t)frame->channels + (size_t)channel;
    return frame->data[idx];
}

void hfr_frame_pixel_set(hfr_frame_t *frame, int x, int y, int channel, double value)
{
    if (!frame || !frame->data) return;
    if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;
    if (channel < 0 || channel >= frame->channels) return;
    size_t idx = ((size_t)y * (size_t)frame->width + (size_t)x) * (size_t)frame->channels + (size_t)channel;
    frame->data[idx] = value;
}

hfr_frame_buffer_t *hfr_buffer_create(int cap, int width, int height)
{
    if (cap < 1 || width <= 0 || height <= 0) return NULL;
    hfr_frame_buffer_t *buf = (hfr_frame_buffer_t *)calloc(1, sizeof(*buf));
    if (!buf) return NULL;
    size_t fs = (size_t)width * (size_t)height * sizeof(double);
    buf->frames = (double *)calloc((size_t)cap, fs);
    if (!buf->frames) { free(buf); return NULL; }
    buf->allocated_frames = cap; buf->num_frames = 0;
    buf->width = width; buf->height = height;
    return buf;
}

void hfr_buffer_destroy(hfr_frame_buffer_t *buf)
{
    if (!buf) return;
    if (buf->frames) { free(buf->frames); buf->frames = NULL; }
    free(buf);
}

int hfr_buffer_push(hfr_frame_buffer_t *buf, const double *data)
{
    if (!buf || !data) return -1;
    if (buf->num_frames >= buf->allocated_frames) {
        int nc = buf->allocated_frames * 2;
        size_t fs = (size_t)buf->width * (size_t)buf->height * sizeof(double);
        double *nf = (double *)realloc(buf->frames, (size_t)nc * fs);
        if (!nf) return -1;
        buf->frames = nf; buf->allocated_frames = nc;
    }
    size_t off = (size_t)buf->num_frames * (size_t)buf->width * (size_t)buf->height;
    size_t fs = (size_t)buf->width * (size_t)buf->height * sizeof(double);
    memcpy(buf->frames + off, data, fs);
    buf->num_frames++;
    return 0;
}

const double *hfr_buffer_get(const hfr_frame_buffer_t *buf, int idx)
{
    if (!buf || idx < 0 || idx >= buf->num_frames) return NULL;
    return buf->frames + (size_t)idx * (size_t)buf->width * (size_t)buf->height;
}

int hfr_buffer_size(const hfr_frame_buffer_t *buf) { return buf ? buf->num_frames : 0; }

void hfr_conversion_config_init(hfr_conversion_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->framerate_in = 24.0; cfg->framerate_out = 60.0;
    cfg->suppress_judder = 1; cfg->motion_blur_amount = 0.5;
    cfg->cadence_threshold = 0.02; cfg->detect_cadence = 1; cfg->adaptive_blend = 1;
}

double hfr_compute_conversion_ratio(double fin, double fout)
{
    if (fin <= 0.0 || fout <= 0.0) return 0.0;
    return fout / fin;
}

int hfr_find_conversion_offset(double fin, double fout, int fi)
{
    if (fin <= 0.0 || fout <= 0.0) return -1;
    return (int)floor((double)fi / fin * fout);
}

double hfr_compute_frame_difference_mad(const hfr_frame_t *a, const hfr_frame_t *b)
{
    if (!a || !b || !a->data || !b->data) return 0.0;
    if (a->width != b->width || a->height != b->height) return 0.0;
    size_t total = (size_t)a->width * (size_t)a->height * (size_t)a->channels;
    double sum = 0.0;
    for (size_t i = 0; i < total; i++) sum += fabs(a->data[i] - b->data[i]);
    return sum / (double)total;
}

int hfr_detect_original_cadence(const double *diffs, int num_frames, double threshold)
{
    if (!diffs || num_frames < 4) return -1;
    if (threshold <= 0.0) threshold = 0.02;
    int max_repeat = 1;
    for (int i = 1; i < num_frames; i++) {
        int found = 0;
        for (int p = 0; p < max_repeat; p++) {
            if (fabs(diffs[i] - diffs[i % (p + 1)]) > threshold) continue;
            found = 1; break;
        }
        if (!found && max_repeat < 5) max_repeat++;
    }
    return max_repeat;
}

double hfr_interp_lerp(double a, double b, double t) { return a + (b - a) * t; }

void hfr_frame_blend(const hfr_frame_t *prev, const hfr_frame_t *next, double w, hfr_frame_t *out)
{
    if (!prev || !next || !out) return;
    if (prev->width != out->width || prev->height != out->height) return;
    if (w < 0.0) w = 0.0;
    if (w > 1.0) w = 1.0;
    size_t n = (size_t)out->width * out->height * out->channels;
    for (size_t i = 0; i < n; i++) out->data[i] = hfr_interp_lerp(prev->data[i], next->data[i], w);
    out->type = HFR_FRAME_MERGED;
}

void hfr_frame_duplicate(const hfr_frame_t *src, hfr_frame_t *dst)
{
    hfr_frame_copy(src, dst);
    if (dst) dst->type = HFR_FRAME_MIXED;
}

int hfr_interpolate_frames(const hfr_frame_t *prev, const hfr_frame_t *next, hfr_frame_t *out, hfr_interp_method_t m, double w)
{
    if (!prev || !next || !out) return -1;
    if (w < 0.0) w = 0.0;
    if (w > 1.0) w = 1.0;
    if (m == HFR_INTERP_FRAME_BLEND || m == HFR_INTERP_MOTION_COMP_MCFI || m == HFR_INTERP_OPTICAL_FLOW)
        hfr_frame_blend(prev, next, w, out);
    else if (m == HFR_INTERP_FRAME_DUPLICATE) {
        if (w < 0.5) hfr_frame_copy(prev, out); else hfr_frame_copy(next, out);
        out->type = HFR_FRAME_MIXED;
    } else hfr_frame_blend(prev, next, w, out);
    return 0;
}

int hfr_pulldown_pattern_length(hfr_pulldown_t p)
{
    switch(p){case HFR_PULLDOWN_2_3:return 5;case HFR_PULLDOWN_2_3_3_2:return 10;
    case HFR_PULLDOWN_24_TO_60:return 10;case HFR_PULLDOWN_25_TO_50:return 2;
    case HFR_PULLDOWN_EURO:return 50;default:return 0;}
}

void hfr_pulldown_apply_2_3(const hfr_frame_t *in, int inc, hfr_frame_t *out, int *outc)
{
    if(!in||!out||!outc||inc<1)return;
    int rep[]={1,0,1,0,0}, o=0, ii=0;
    while(ii<inc && o<*outc){hfr_frame_copy(&in[ii],&out[o]);out[o].type=HFR_FRAME_ORIGINAL;o++;if(rep[o%5]==0)ii++;}
    *outc=o;
}

void hfr_pulldown_inverse_telecine(const hfr_frame_t *in, int inc, hfr_frame_t *out, int *outc)
{
    if(!in||!out||!outc||inc<5)return;
    int o=0;
    for(int i=0;i+4<inc && o+3<*outc;i+=5){hfr_frame_copy(&in[i],&out[o++]);hfr_frame_copy(&in[i+2],&out[o++]);hfr_frame_copy(&in[i+3],&out[o++]);hfr_frame_copy(&in[i+4],&out[o++]);}
    *outc=o;
}

int hfr_detect_pulldown_pattern(const hfr_frame_t *frames, int nf)
{
    if(!frames||nf<5)return HFR_PULLDOWN_COUNT;
    int n=(nf-1<50)?nf-1:50;
    double *d=(double*)malloc((size_t)n*sizeof(double)),avg=0.0;
    if(!d)return HFR_PULLDOWN_COUNT;
    for(int i=0;i<n;i++){d[i]=hfr_compute_frame_difference_mad(&frames[i],&frames[i+1]);avg+=d[i];}
    avg/=(double)n;
    int small=0,large=0;
    for(int i=0;i<n;i++){if(d[i]<avg*0.3)large++;else if(d[i]<avg*1.5)small++;}
    free(d);
    if(large==0)return HFR_PULLDOWN_EURO;
    double r=(double)small/(double)large;
    if(r>2.5&&r<3.5)return HFR_PULLDOWN_2_3_3_2;
    if(r>1.2&&r<2.0)return HFR_PULLDOWN_2_3;
    return HFR_PULLDOWN_COUNT;
}

double *hfr_compute_frame_diff(const hfr_frame_t *a, const hfr_frame_t *b)
{
    if(!a||!b||!a->data||!b->data)return NULL;
    if(a->width!=b->width||a->height!=b->height)return NULL;
    size_t t=(size_t)a->width*a->height*a->channels;
    double *d=(double*)malloc(t*sizeof(double));
    if(!d)return NULL;
    for(size_t i=0;i<t;i++)d[i]=a->data[i]-b->data[i];
    return d;
}

void hfr_temporal_median_3(const hfr_frame_t *prev, const hfr_frame_t *curr, const hfr_frame_t *next, hfr_frame_t *out)
{
    if(!prev||!curr||!next||!out)return;
    size_t t=(size_t)out->width*out->height*out->channels;
    for(size_t i=0;i<t;i++){
        double p=prev->data[i],c=curr->data[i],n=next->data[i],tmp;
        if(p>c){tmp=p;p=c;c=tmp;}if(c>n){tmp=c;c=n;n=tmp;}if(p>c){tmp=p;p=c;c=tmp;}
        out->data[i]=c;
    }
    out->type=HFR_FRAME_MERGED;
}

void hfr_temporal_denoise_simple(hfr_frame_buffer_t *buf, int fidx, int radius, hfr_frame_t *out)
{
    if(!buf||!out||fidx<0||fidx>=buf->num_frames)return;
    if(radius<1)radius=1;
    int s=fidx-radius,e=fidx+radius;
    if(s<0)s=0;
    if(e>=buf->num_frames)e=buf->num_frames-1;
    size_t t=(size_t)out->width*out->height*out->channels;
    for(size_t i=0;i<t;i++){
        double sum=0.0;int cnt=0;
        for(int f=s;f<=e;f++){const double *fd=hfr_buffer_get(buf,f);if(fd){sum+=fd[i];cnt++;}}
        out->data[i]=(cnt>0)?sum/(double)cnt:0.0;
    }
    out->type=HFR_FRAME_MERGED;
}

void hfr_deinterlace_weave(const hfr_frame_t *top, const hfr_frame_t *bot, hfr_frame_t *out)
{
    if(!top||!bot||!out)return;
    int w=out->width,ch=out->channels;
    for(int y=0;y<out->height;y++){
        int fy=y/2;
        const hfr_frame_t *field=(y%2==0)?top:bot;
        memcpy(out->data+(size_t)y*w*ch,field->data+(size_t)fy*w*ch,(size_t)w*ch*sizeof(double));
    }
    out->scan=HFR_SCAN_PROGRESSIVE;
}

void hfr_deinterlace_bob(const hfr_frame_t *field, hfr_frame_t *out, int is_top)
{
    if(!field||!out)return;
    int w=out->width,h=out->height,ch=out->channels,fh=field->height;
    for(int y=0;y<h;y++){
        int sy=is_top?y/2:y/2,ny=sy+1;if(ny>=fh)ny=fh-1;
        for(int x=0;x<w;x++)for(int c=0;c<ch;c++){
            double v1=hfr_frame_pixel_get(field,x,sy,c),v2=hfr_frame_pixel_get(field,x,ny,c);
            hfr_frame_pixel_set(out,x,y,c,(y%2==0)?v1:0.5*(v1+v2));
        }
    }
    out->scan=HFR_SCAN_PROGRESSIVE;out->type=HFR_FRAME_INTERPOLATED;
}

void hfr_deinterlace_yadif(const hfr_frame_t *prev,const hfr_frame_t *curr,const hfr_frame_t *next,hfr_frame_t *out,int parity)
{
    if(!prev||!curr||!next||!out)return;
    int w=out->width,h=out->height,ch=out->channels;
    for(int y=0;y<h;y++){
        int sy=y/2;
        for(int x=0;x<w;x++)for(int c=0;c<ch;c++){
            if((y+parity)%2==0)hfr_frame_pixel_set(out,x,y,c,hfr_frame_pixel_get(curr,x,sy,c));
            else{
                double cur=hfr_frame_pixel_get(curr,x,sy,c);
                double up=hfr_frame_pixel_get(curr,x,sy+1,c);
                double pv=hfr_frame_pixel_get(prev,x,sy,c);
                double nv=hfr_frame_pixel_get(next,x,sy,c);
                double sp=fabs(pv-nv),st=fabs(up-cur);
                hfr_frame_pixel_set(out,x,y,c,(sp<st)?0.5*(pv+nv):0.5*(cur+up));
            }
        }
    }
    out->scan=HFR_SCAN_PROGRESSIVE;out->type=HFR_FRAME_INTERPOLATED;
}

double hfr_motion_detect_pixel(const hfr_frame_t *prev, const hfr_frame_t *curr, int x, int y)
{
    if(!prev||!curr)return 0.0;
    double sum=0.0;int cnt=0;
    for(int c=0;c<prev->channels&&c<curr->channels;c++){
        sum+=fabs(hfr_frame_pixel_get(prev,x,y,c)-hfr_frame_pixel_get(curr,x,y,c));cnt++;
    }
    return (cnt>0)?sum/(double)cnt:0.0;
}

void hfr_motion_map_compute(const hfr_frame_t *prev, const hfr_frame_t *curr, double *map)
{
    if(!prev||!curr||!map)return;
    int t=prev->width*prev->height;
    for(int i=0;i<t;i++)map[i]=hfr_motion_detect_pixel(prev,curr,i%prev->width,i/prev->width);
}

void hfr_motion_adaptive_blend(const hfr_frame_t *prev, const hfr_frame_t *curr, const double *map, double thr, hfr_frame_t *out)
{
    if(!prev||!curr||!map||!out)return;
    if(thr<=0.0)thr=0.1;
    size_t t=(size_t)out->width*out->height*out->channels;
    for(size_t i=0;i<t;i++){
        size_t pi=i/(size_t)out->channels;
        double alpha=(map[pi]>thr)?0.0:1.0;
        out->data[i]=alpha*prev->data[i]+(1.0-alpha)*curr->data[i];
    }
    out->type=HFR_FRAME_MERGED;
}

double hfr_shutter_speed_from_angle(double angle, double fps)
{
    if(fps<=0.0)return 0.0;
    return angle/(360.0*fps);
}

double hfr_motion_blur_kernel_size(double angle, double fps, double ratio)
{
    if(fps<=0.0)return 0.0;
    return ratio*angle/(360.0*fps)*1000.0;
}

void hfr_add_motion_blur_1d(double *line, int len, double kpix)
{
    if(!line||len<2||kpix<=0.0)return;
    int k=(int)kpix;if(k<1)k=1;if(k>len)k=len;
    double *tmp=(double*)malloc((size_t)len*sizeof(double));
    if(!tmp)return;
    memcpy(tmp,line,(size_t)len*sizeof(double));
    for(int i=0;i<len;i++){double sum=0.0;int cnt=0;for(int j=i-k;j<=i+k;j++){if(j>=0&&j<len){sum+=tmp[j];cnt++;}}line[i]=(cnt>0)?sum/(double)cnt:0.0;}
    free(tmp);
}
