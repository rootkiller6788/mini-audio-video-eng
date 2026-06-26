#include "hdr_core.h"
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double PQ_M1_NUMERATOR   = 2610.0;
static const double PQ_M1_DENOMINATOR = 16384.0;
static const double PQ_M2_NUMERATOR   = 2523.0;
static const double PQ_M2_DENOMINATOR = 4096.0;
static const double PQ_C1_NUMERATOR   = 3424.0;
static const double PQ_C1_DENOMINATOR = 4096.0;
static const double PQ_C2_NUMERATOR   = 2413.0;
static const double PQ_C2_DENOMINATOR = 4096.0;
static const double PQ_C3_NUMERATOR   = 2392.0;
static const double PQ_C3_DENOMINATOR = 4096.0;
static const double BT1886_DEFAULT_GAMMA = 2.4;
static const double BT1886_DEFAULT_PEAK = 100.0;
static const double HLG_DEFAULT_A = 0.17883277;
static const double HLG_DEFAULT_B = 0.28466892;
static const double HLG_DEFAULT_C = 0.55991073;
static const double HLG_DEFAULT_SYSTEM_GAMMA = 1.2;
static const double HLG_DEFAULT_NOMINAL_PEAK = 1000.0;
static const double WEBER_FRACTION_PHOTOPIC = 0.01;

static const hdr_primaries_set_t g_bt2020_primaries = {
    {0.708, 0.292},  {0.170, 0.797},  {0.131, 0.046},  {0.3127, 0.3290}, "BT.2020"
};
static const hdr_primaries_set_t g_bt709_primaries = {
    {0.640, 0.330},  {0.300, 0.600},  {0.150, 0.060},  {0.3127, 0.3290}, "BT.709"
};
static const hdr_primaries_set_t g_dci_p3_primaries = {
    {0.680, 0.320},  {0.265, 0.690},  {0.150, 0.060},  {0.314, 0.351},  "DCI-P3"
};
static const hdr_primaries_set_t g_aces_ap0_primaries = {
    {0.7347, 0.2653}, {0.0000, 1.0000}, {0.0001, -0.0770}, {0.32168, 0.33767}, "ACES AP0"
};
static const hdr_primaries_set_t g_aces_ap1_primaries = {
    {0.713, 0.293},  {0.165, 0.830},  {0.128, 0.044},  {0.32168, 0.33767}, "ACES AP1"
};

void hdr_pq_params_init(hdr_pq_params_t *pq)
{
    if (!pq) return;
    /* SMPTE ST 2084 constants:
     *   m1 = 2610/16384
     *   m2 = 2523/4096 * 128  (applied power with signal^{1/m2})
     *   c1 = 3424/4096 = c3 - c2 + 1
     *   c2 = 2413/4096 * 32
     *   c3 = 2392/4096 * 32
     */
    pq->m1 = PQ_M1_NUMERATOR / PQ_M1_DENOMINATOR;
    pq->m2 = (PQ_M2_NUMERATOR / PQ_M2_DENOMINATOR) * 128.0;
    pq->c1 = PQ_C1_NUMERATOR / PQ_C1_DENOMINATOR;
    pq->c2 = (PQ_C2_NUMERATOR / PQ_C2_DENOMINATOR) * 32.0;
    pq->c3 = (PQ_C3_NUMERATOR / PQ_C3_DENOMINATOR) * 32.0;
}

void hdr_hlg_params_init(hdr_hlg_params_t *hlg)
{
    if (!hlg) return;
    hlg->a = HLG_DEFAULT_A;
    hlg->b = HLG_DEFAULT_B;
    hlg->c = HLG_DEFAULT_C;
    hlg->system_gamma = HLG_DEFAULT_SYSTEM_GAMMA;
    hlg->nominal_peak = HLG_DEFAULT_NOMINAL_PEAK;
}

void hdr_bt1886_params_init(hdr_bt1886_params_t *bt, double peak_nits, double gamma_val)
{
    if (!bt) return;
    bt->gamma = (gamma_val > 0.0) ? gamma_val : BT1886_DEFAULT_GAMMA;
    bt->black_offset = 0.0;
    bt->nominal_peak = (peak_nits > 0.0) ? peak_nits : BT1886_DEFAULT_PEAK;
}

double hdr_pq_eotf(double signal, const hdr_pq_params_t *pq)
{
    if (!pq) return 0.0;
    if (signal <= 0.0) return 0.0;
    if (signal >= 1.0) return 10000.0;

    double E_pow = pow(signal, 1.0 / pq->m2);
    double numerator = E_pow - pq->c1;
    if (numerator < 0.0) numerator = 0.0;
    double denominator = pq->c2 - pq->c3 * E_pow;
    if (denominator <= 0.0) return 10000.0;

    double F_D = pow(numerator / denominator, 1.0 / pq->m1);
    return F_D * 10000.0;
}

double hdr_pq_oetf(double luminance, const hdr_pq_params_t *pq)
{
    if (!pq) return 0.0;
    if (luminance <= 0.0) return 0.0;

    double L = luminance / 10000.0;
    if (L >= 1.0) return 1.0;

    double L_pow = pow(L, pq->m1);
    double numerator = pq->c1 + pq->c2 * L_pow;
    double denominator = 1.0 + pq->c3 * L_pow;
    double E = pow(numerator / denominator, pq->m2);
    if (E > 1.0) E = 1.0;
    return E;
}

double hdr_hlg_oetf(double linear_light, const hdr_hlg_params_t *hlg)
{
    if (!hlg) return 0.0;
    if (linear_light <= 0.0) return 0.0;
    if (linear_light >= 1.0) return 1.0;

    double threshold = 1.0 / 12.0;
    if (linear_light <= threshold)
        return sqrt(3.0 * linear_light);
    else
        return hlg->a * log(12.0 * linear_light - hlg->b) + hlg->c;
}

double hdr_hlg_eotf(double signal, const hdr_hlg_params_t *hlg)
{
    if (!hlg) return 0.0;
    if (signal <= 0.0) return 0.0;
    if (signal >= 1.0) return 1.0;

    double threshold = sqrt(3.0 / 12.0);
    double E;
    if (signal <= threshold) {
        E = (signal * signal) / 3.0;
    } else {
        E = (exp((signal - hlg->c) / hlg->a) + hlg->b) / 12.0;
    }
    double display_linear = pow(E, hlg->system_gamma - 1.0);
    if (display_linear > 1.0) display_linear = 1.0;
    return display_linear;
}

double hdr_bt1886_eotf(double signal, const hdr_bt1886_params_t *bt)
{
    if (!bt) return 0.0;
    if (signal < 0.0) signal = 0.0;
    if (signal > 1.0) signal = 1.0;
    double V_plus_b = signal + bt->black_offset;
    if (V_plus_b < 0.0) V_plus_b = 0.0;
    return bt->nominal_peak * pow(V_plus_b, bt->gamma);
}

void hdr_transfer_eval_init(hdr_transfer_evaluator_t *eval, hdr_transfer_function_t type, int dir)
{
    if (!eval) return;
    memset(eval, 0, sizeof(*eval));
    eval->type = type;
    eval->direction = dir;
    switch (type) {
    case HDR_TF_PQ_ST2084:     hdr_pq_params_init(&eval->params.pq); break;
    case HDR_TF_HLG_ARIB_B67:  hdr_hlg_params_init(&eval->params.hlg); break;
    case HDR_TF_SDR_GAMMA_22:  eval->params.pure_gamma = 2.2; break;
    case HDR_TF_SDR_GAMMA_24:  eval->params.pure_gamma = 2.4; break;
    case HDR_TF_LINEAR:        eval->params.pure_gamma = 1.0; break;
    default: hdr_bt1886_params_init(&eval->params.bt1886, 100.0, 2.4); break;
    }
}

static double eval_pq_eotf(double v, const hdr_transfer_evaluator_t *eval)
{ return hdr_pq_eotf(v, &eval->params.pq); }

static double eval_pq_oetf(double v, const hdr_transfer_evaluator_t *eval)
{ return hdr_pq_oetf(v, &eval->params.pq); }

static double eval_hlg_eotf(double v, const hdr_transfer_evaluator_t *eval)
{ return hdr_hlg_eotf(v, &eval->params.hlg); }

static double eval_hlg_oetf(double v, const hdr_transfer_evaluator_t *eval)
{ return hdr_hlg_oetf(v, &eval->params.hlg); }

static double eval_gamma_eotf(double v, const hdr_transfer_evaluator_t *eval)
{
    if (v <= 0.0) return 0.0;
    return 100.0 * pow(v, eval->params.pure_gamma);
}

static double eval_gamma_oetf(double v, const hdr_transfer_evaluator_t *eval)
{
    if (v <= 0.0) return 0.0;
    return pow(v / 100.0, 1.0 / eval->params.pure_gamma);
}

static double eval_bt1886_eotf(double v, const hdr_transfer_evaluator_t *eval)
{ return hdr_bt1886_eotf(v, &eval->params.bt1886); }

static double eval_bt1886_oetf(double v, const hdr_transfer_evaluator_t *eval)
{
    (void)eval;
    if (v <= 0.0) return 0.0;
    double V = pow(v / 100.0, 1.0 / 2.4);
    if (V < 0.0) V = 0.0;
    if (V > 1.0) V = 1.0;
    return V;
}

static double eval_linear(double v, const hdr_transfer_evaluator_t *eval)
{
    (void)eval;
    return v;
}

double hdr_transfer_evaluate(double value, const hdr_transfer_evaluator_t *eval)
{
    if (!eval) return 0.0;
    if (eval->direction == 1) {
        switch (eval->type) {
        case HDR_TF_PQ_ST2084:    return eval_pq_eotf(value, eval);
        case HDR_TF_HLG_ARIB_B67: return eval_hlg_eotf(value, eval);
        case HDR_TF_SDR_GAMMA_22:
        case HDR_TF_SDR_GAMMA_24: return eval_gamma_eotf(value, eval);
        case HDR_TF_LINEAR:       return eval_linear(value, eval);
        default:                  return eval_bt1886_eotf(value, eval);
        }
    } else {
        switch (eval->type) {
        case HDR_TF_PQ_ST2084:    return eval_pq_oetf(value, eval);
        case HDR_TF_HLG_ARIB_B67: return eval_hlg_oetf(value, eval);
        case HDR_TF_SDR_GAMMA_22:
        case HDR_TF_SDR_GAMMA_24: return eval_gamma_oetf(value, eval);
        case HDR_TF_LINEAR:       return eval_linear(value, eval);
        default:                  return eval_bt1886_oetf(value, eval);
        }
    }
}

static double tf_evaluate_for_lut(hdr_transfer_function_t tf_type, double value, int forward)
{
    hdr_transfer_evaluator_t eval;
    hdr_transfer_eval_init(&eval, tf_type, forward ? 0 : 1);
    return hdr_transfer_evaluate(value, &eval);
}

int hdr_lut_build_forward(hdr_transfer_lut_t *lut, hdr_transfer_function_t tf_type, int lut_size, double in_min, double in_max)
{
    if (!lut || lut_size < 2 || in_min >= in_max) return -1;
    memset(lut, 0, sizeof(*lut));
    lut->tf_type = tf_type;
    lut->lut_size = lut_size;
    lut->input_min = in_min;
    lut->input_max = in_max;
    lut->input_step = (in_max - in_min) / (double)(lut_size - 1);
    lut->forward_lut = (double *)malloc((size_t)lut_size * sizeof(double));
    if (!lut->forward_lut) return -1;
    for (int i = 0; i < lut_size; i++) {
        double input = in_min + (double)i * lut->input_step;
        lut->forward_lut[i] = tf_evaluate_for_lut(tf_type, input, 1);
    }
    return 0;
}

double hdr_lut_lookup_forward(const hdr_transfer_lut_t *lut, double value)
{
    if (!lut || !lut->forward_lut || lut->lut_size < 2) return 0.0;
    if (value <= lut->input_min) return lut->forward_lut[0];
    if (value >= lut->input_max) return lut->forward_lut[lut->lut_size - 1];
    double frac = (value - lut->input_min) / lut->input_step;
    int idx = (int)frac;
    if (idx < 0) idx = 0;
    if (idx >= lut->lut_size - 1) idx = lut->lut_size - 2;
    double t = frac - (double)idx;
    return lut->forward_lut[idx] * (1.0 - t) + lut->forward_lut[idx + 1] * t;
}

int hdr_lut_build_inverse(hdr_transfer_lut_t *lut, hdr_transfer_function_t tf_type, int lut_size, double out_min, double out_max)
{
    if (!lut || lut_size < 2 || out_min >= out_max) return -1;
    lut->inverse_lut = (double *)malloc((size_t)lut_size * sizeof(double));
    if (!lut->inverse_lut) return -1;
    double step = (out_max - out_min) / (double)(lut_size - 1);
    for (int i = 0; i < lut_size; i++) {
        double output_val = out_min + (double)i * step;
        lut->inverse_lut[i] = tf_evaluate_for_lut(tf_type, output_val, 0);
    }
    return 0;
}

double hdr_lut_lookup_inverse(const hdr_transfer_lut_t *lut, double value)
{
    if (!lut || !lut->inverse_lut || lut->lut_size < 2) return 0.0;
    if (value <= lut->inverse_lut[0]) return lut->input_min;
    if (value >= lut->inverse_lut[lut->lut_size - 1]) return lut->input_max;
    int lo = 0, hi = lut->lut_size - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (lut->inverse_lut[mid] <= value) lo = mid;
        else hi = mid;
    }
    double range = lut->inverse_lut[hi] - lut->inverse_lut[lo];
    double t = (range > 1e-16) ? (value - lut->inverse_lut[lo]) / range : 0.0;
    return lut->input_min + ((double)lo + t) * lut->input_step;
}

void hdr_lut_destroy(hdr_transfer_lut_t *lut)
{
    if (!lut) return;
    if (lut->forward_lut) { free(lut->forward_lut); lut->forward_lut = NULL; }
    if (lut->inverse_lut) { free(lut->inverse_lut); lut->inverse_lut = NULL; }
    lut->lut_size = 0;
}

hdr_luminance_histogram_t *hdr_histogram_create(int num_bins, double log_min, double log_max)
{
    if (num_bins < 2 || log_min >= log_max) return NULL;
    hdr_luminance_histogram_t *hist = (hdr_luminance_histogram_t *)calloc(1, sizeof(*hist));
    if (!hist) return NULL;
    hist->bins = (uint64_t *)calloc((size_t)num_bins, sizeof(uint64_t));
    if (!hist->bins) { free(hist); return NULL; }
    hist->num_bins = num_bins;
    hist->log_min = log_min;
    hist->log_max = log_max;
    hist->bin_width = (log_max - log_min) / (double)num_bins;
    hist->total_samples = 0;
    return hist;
}

void hdr_histogram_clear(hdr_luminance_histogram_t *hist)
{
    if (!hist || !hist->bins) return;
    memset(hist->bins, 0, (size_t)hist->num_bins * sizeof(uint64_t));
    hist->total_samples = 0;
}

static int hist_find_bin(const hdr_luminance_histogram_t *hist, double luminance)
{
    if (luminance <= 0.0) return -1;
    double log_L = log10(luminance);
    if (log_L < hist->log_min || log_L >= hist->log_max) return -1;
    int bin = (int)((log_L - hist->log_min) / hist->bin_width);
    if (bin < 0 || bin >= hist->num_bins) return -1;
    return bin;
}

int hdr_histogram_add(hdr_luminance_histogram_t *hist, double luminance)
{
    if (!hist || !hist->bins) return -1;
    int bin = hist_find_bin(hist, luminance);
    if (bin < 0) return -1;
    hist->bins[bin]++;
    hist->total_samples++;
    return 0;
}

void hdr_histogram_compute_percentiles(hdr_luminance_histogram_t *hist)
{
    if (!hist || !hist->bins || hist->total_samples == 0) return;
    uint64_t cumulative = 0;
    uint64_t target_50 = (uint64_t)(0.50 * (double)hist->total_samples);
    uint64_t target_90 = (uint64_t)(0.90 * (double)hist->total_samples);
    uint64_t target_99 = (uint64_t)(0.99 * (double)hist->total_samples);
    int found_50 = 0, found_90 = 0, found_99 = 0;
    for (int i = 0; i < hist->num_bins; i++) {
        cumulative += hist->bins[i];
        double bin_center_log = hist->log_min + ((double)i + 0.5) * hist->bin_width;
        double bin_center = pow(10.0, bin_center_log);
        if (!found_50 && cumulative >= target_50) { hist->percentile_50 = bin_center; found_50 = 1; }
        if (!found_90 && cumulative >= target_90) { hist->percentile_90 = bin_center; found_90 = 1; }
        if (!found_99 && cumulative >= target_99) { hist->percentile_99 = bin_center; found_99 = 1; }
        if (found_50 && found_90 && found_99) break;
    }
}

void hdr_histogram_destroy(hdr_luminance_histogram_t *hist)
{
    if (!hist) return;
    if (hist->bins) { free(hist->bins); hist->bins = NULL; }
    free(hist);
}

void hdr_metadata_init(hdr_metadata_t *meta)
{
    if (!meta) return;
    memset(meta, 0, sizeof(*meta));
    meta->mastering_primaries = g_bt2020_primaries;
    meta->mastering_max_luminance = 1000.0;
    meta->mastering_min_luminance = 0.005;
    meta->pq_min_signal = 0.0;
    meta->pq_max_signal = 1.0;
}

double hdr_metadata_compute_maxcll(const hdr_luminance_histogram_t *histogram, double percentile)
{
    if (!histogram || histogram->total_samples == 0) return 0.0;
    uint64_t target = (uint64_t)(percentile * (double)histogram->total_samples);
    uint64_t cumulative = 0;
    for (int i = 0; i < histogram->num_bins; i++) {
        cumulative += histogram->bins[i];
        if (cumulative >= target) {
            double bin_center_log = histogram->log_min + ((double)i + 0.5) * histogram->bin_width;
            return pow(10.0, bin_center_log);
        }
    }
    return pow(10.0, histogram->log_max);
}

double hdr_metadata_compute_maxfall(const hdr_luminance_histogram_t *histogram)
{
    if (!histogram || histogram->total_samples == 0) return 0.0;
    double sum_linear = 0.0;
    uint64_t total = 0;
    for (int i = 0; i < histogram->num_bins; i++) {
        if (histogram->bins[i] > 0) {
            double bin_center_log = histogram->log_min + ((double)i + 0.5) * histogram->bin_width;
            double bin_center = pow(10.0, bin_center_log);
            sum_linear += bin_center * (double)histogram->bins[i];
            total += histogram->bins[i];
        }
    }
    if (total == 0) return 0.0;
    return sum_linear / (double)total;
}

void hdr_display_init(hdr_display_model_t *display, double peak_nits, double black_nits, hdr_primaries_t primaries, int is_hdr)
{
    if (!display) return;
    memset(display, 0, sizeof(*display));
    display->peak_luminance = peak_nits;
    display->black_level = black_nits;
    display->diffuse_white = peak_nits * 0.1;
    display->primaries = primaries;
    display->eotf_gamma = 2.4;
    display->is_hdr_capable = is_hdr;
    display->supports_pq = (is_hdr && peak_nits > 500.0) ? 1 : 0;
    display->supports_hlg = (is_hdr) ? 1 : 0;
    display->min_frame_interval_ms = 1000.0 / 60.0;
    display->max_bit_depth = is_hdr ? 10 : 8;
    display->ambient_lux = 5.0;
}

const hdr_primaries_set_t *hdr_primaries_get(hdr_primaries_t type)
{
    switch (type) {
    case HDR_PRIMARIES_BT709:    return &g_bt709_primaries;
    case HDR_PRIMARIES_DCI_P3:   return &g_dci_p3_primaries;
    case HDR_PRIMARIES_BT2020:   return &g_bt2020_primaries;
    case HDR_PRIMARIES_ACES_AP0: return &g_aces_ap0_primaries;
    case HDR_PRIMARIES_ACES_AP1: return &g_aces_ap1_primaries;
    default:                     return &g_bt2020_primaries;
    }
}

void hdr_chromaticity_to_xyz(hdr_chromaticity_t chroma, double Y, double *X, double *Z)
{
    if (!X || !Z) return;
    if (chroma.y <= 0.0) { *X = 0.0; *Z = 0.0; return; }
    *X = (chroma.x / chroma.y) * Y;
    *Z = ((1.0 - chroma.x - chroma.y) / chroma.y) * Y;
}

void hdr_weber_init(hdr_weber_model_t *model, double adaptation)
{
    if (!model) return;
    model->weber_fraction = WEBER_FRACTION_PHOTOPIC;
    model->adaptation_level = adaptation;
    model->jnd_threshold = adaptation * WEBER_FRACTION_PHOTOPIC;
}

double hdr_weber_jnd(const hdr_weber_model_t *model, double L)
{
    if (!model) return 0.0;
    if (L <= 0.0) return 0.0;
    double weber_component = model->weber_fraction * L;
    double sqrt_component = 0.005 * model->adaptation_level * sqrt(L / model->adaptation_level);
    double L_rel = L / model->adaptation_level;
    double blend = L_rel / (1.0 + L_rel);
    return sqrt_component * (1.0 - blend) + weber_component * blend;
}

double hdr_barten_csf_compute(hdr_barten_csf_t *csf)
{
    if (!csf) return 0.0;
    double L = csf->luminance;
    double u = csf->spatial_freq;
    if (L <= 0.0 || u <= 0.0) return 0.0;

    double sigma = 0.005;
    double mtf_opt = exp(-2.0 * M_PI * M_PI * sigma * sigma * u * u);
    csf->mtf_optical = mtf_opt;

    double mtf_neural = 1.0 / sqrt(1.0 + 0.05 * u * u);
    csf->mtf_neural = mtf_neural;

    double k = 3.0;
    double T = 0.1;
    double X_max = 12.0;
    double N_max = 15.0;
    double eta = 0.03;
    double p = 1.2e6;
    double phi0 = eta * p * L;
    double numerator = mtf_opt / k;
    double denom1 = sqrt(2.0 / T);
    double denom2 = sqrt(1.0 / (X_max * X_max) + 1.0 / (N_max * N_max));
    double denom3 = sqrt(1.0 - exp(-u * u / (2.0 * 2.5 * 2.5)));
    double denom4 = sqrt(1.0 / phi0);
    double denom5 = sqrt(1.0 + u * u / 7.0);
    double S = numerator / (denom1 * denom2 * denom3 * denom4 * denom5);
    if (S < 0.001) S = 0.001;
    csf->csf_threshold = 1.0 / S;
    return csf->csf_threshold;
}

int hdr_barten_min_bit_depth(double peak_luminance, double black_luminance)
{
    if (peak_luminance <= 0.0) return 8;
    if (black_luminance <= 0.0) black_luminance = 0.0001;
    double dynamic_range = peak_luminance / black_luminance;
    double stops = log2(dynamic_range);
    double pq_bits = stops / 1.6;
    double margin = 2.0;
    double needed = pq_bits + margin;
    if (needed <= 8.0) return 8;
    if (needed <= 10.0) return 10;
    if (needed <= 12.0) return 12;
    return 16;
}
