#include <libuvc/libuvc.h>

// PureThermal's libuvc fork names the 16-bit raw thermal format `Y16`; upstream
// libuvc (Debian/Ubuntu) calls the identical format `GRAY16`. Fall back so the
// same source builds against either header.
#ifndef UVC_FRAME_FORMAT_Y16
#define UVC_FRAME_FORMAT_Y16 UVC_FRAME_FORMAT_GRAY16
#endif
#include <opencv2/opencv.hpp>
#include <zmq.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

int parseInt(const std::string& s, int fallback) {
    try {
        size_t i = 0;
        int v = std::stoi(s, &i, 0);
        return (i == s.size()) ? v : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string expandHome(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = std::getenv("HOME");
    if (!home) return p;
    if (p.size() == 1) return std::string(home);
    if (p[1] == '/') return std::string(home) + p.substr(1);
    return p;
}

std::string isoTs() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto sec = time_point_cast<seconds>(now);
    auto ms = duration_cast<milliseconds>(now - sec).count();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms << "Z";
    return os.str();
}

int iNode(const cv::FileNode& n, int d) { return n.empty() ? d : (n.isString() ? parseInt((std::string)n, d) : (int)n); }
double dNode(const cv::FileNode& n, double d) { return n.empty() ? d : (double)n; }
bool bNode(const cv::FileNode& n, bool d) {
    if (n.empty()) return d;
    if (n.isString()) {
        std::string v = lower((std::string)n);
        if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
        if (v == "0" || v == "false" || v == "no" || v == "off") return false;
        return d;
    }
    return ((int)n) != 0;
}
std::string sNode(const cv::FileNode& n, const std::string& d) { return n.empty() ? d : (std::string)n; }

uint64_t monotonicNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct CaptureCfg {
    int vendor_id = 0x1e4e;
    int product_id = 0x0100;
    int request_width = 160;
    int request_height = 120;
    int telemetry_rows = 0;
    bool telemetry_bottom = true;
    int fps = 9;
    int frame_timeout_ms = 1400;
    int reopen_backoff_ms = 700;
    int max_reopen_attempts = 8;
    bool strict_native_160x120 = true;
    int max_same_signature_frames = 72;
};

struct StreamCfg {
    bool enable_rtp = true;
    bool enable_raw_zmq = true;
    std::string parent_ip = "10.0.0.98";
    int rtp_port = 5000;
    std::string zmq_endpoint = "tcp://*:5557";
    int out_width = 640;
    int out_height = 480;
    int bitrate_kbps = 900;
    std::string colormap = "inferno";
    double temp_min_c = 26.0;
    double temp_max_c = 38.0;
};

struct DetectCfg {
    int crib_x = 0, crib_y = 0, crib_w = 0, crib_h = 0;
    double wet_y_frac_start = 0.45;
    int min_baby_area = 1700;
    double warm_delta_c = 1.2;
    double head_top_frac = 0.35;
    double hottest_frac = 0.15;
    double temp_smooth_alpha = 0.12;
    double baseline_alpha = 0.01;
    double baseline_motion_freeze_delta = 0.75;
    double baseline_ambient_jump_freeze_delta = 1.2;
    double wet_cold_delta = -1.0;
    double wet_warm_delta = 1.0;
    int wet_enter_area = 900;
    int wet_exit_area = 620;
    double wet_persist_sec = 6.0;
};

struct RuntimeCfg {
    bool headless = true;
    bool preview = false;
    std::string events_file = "~/crib_monitor_events.jsonl";
    int status_every_n_frames = 45;
};

struct AppCfg {
    CaptureCfg capture;
    StreamCfg stream;
    DetectCfg detect;
    RuntimeCfg runtime;
    std::string config_path = "baby-pi/lepton_monitor_config.yaml";
};

struct Cli {
    bool help = false;
    bool preview = false;
    bool headless = false;
    std::string config = "baby-pi/lepton_monitor_config.yaml";
    std::string parent_ip;
    int rtp_port = -1;
    std::string zmq_endpoint;
};

Cli parseCli(int argc, char** argv) {
    Cli c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") c.help = true;
        else if (a == "--preview") c.preview = true;
        else if (a == "--headless") c.headless = true;
        else if (a == "--config" && i + 1 < argc) c.config = argv[++i];
        else if (a == "--parent-ip" && i + 1 < argc) c.parent_ip = argv[++i];
        else if (a == "--rtp-port" && i + 1 < argc) c.rtp_port = parseInt(argv[++i], -1);
        else if (a == "--zmq-endpoint" && i + 1 < argc) c.zmq_endpoint = argv[++i];
        else std::cerr << "[WARN] Ignored arg: " << a << "\n";
    }
    return c;
}

void usage(const char* exe) {
    std::cout
        << "Usage: " << exe << " [--config file] [--preview|--headless] [--parent-ip ip] [--rtp-port p] [--zmq-endpoint ep]\n";
}

void loadYaml(const std::string& pathIn, AppCfg& cfg) {
    std::string path = expandHome(pathIn);
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "[WARN] Config not found: " << path << " (defaults)\n";
        return;
    }
    auto cap = fs["capture"];
    cfg.capture.vendor_id = iNode(cap["vendor_id"], cfg.capture.vendor_id);
    cfg.capture.product_id = iNode(cap["product_id"], cfg.capture.product_id);
    cfg.capture.request_width = iNode(cap["request_width"], cfg.capture.request_width);
    cfg.capture.request_height = iNode(cap["request_height"], cfg.capture.request_height);
    cfg.capture.telemetry_rows = iNode(cap["telemetry_rows"], cfg.capture.telemetry_rows);
    cfg.capture.telemetry_bottom = bNode(cap["telemetry_bottom"], cfg.capture.telemetry_bottom);
    cfg.capture.fps = iNode(cap["fps"], cfg.capture.fps);
    cfg.capture.frame_timeout_ms = iNode(cap["frame_timeout_ms"], cfg.capture.frame_timeout_ms);
    cfg.capture.reopen_backoff_ms = iNode(cap["reopen_backoff_ms"], cfg.capture.reopen_backoff_ms);
    cfg.capture.max_reopen_attempts = iNode(cap["max_reopen_attempts"], cfg.capture.max_reopen_attempts);
    cfg.capture.strict_native_160x120 = bNode(cap["strict_native_160x120"], cfg.capture.strict_native_160x120);
    cfg.capture.max_same_signature_frames = iNode(cap["max_same_signature_frames"], cfg.capture.max_same_signature_frames);

    auto st = fs["stream"];
    cfg.stream.enable_rtp = bNode(st["enable_rtp"], cfg.stream.enable_rtp);
    cfg.stream.enable_raw_zmq = bNode(st["enable_raw_zmq"], cfg.stream.enable_raw_zmq);
    cfg.stream.parent_ip = sNode(st["parent_ip"], cfg.stream.parent_ip);
    cfg.stream.rtp_port = iNode(st["rtp_port"], cfg.stream.rtp_port);
    cfg.stream.zmq_endpoint = sNode(st["zmq_endpoint"], cfg.stream.zmq_endpoint);
    cfg.stream.out_width = iNode(st["out_width"], cfg.stream.out_width);
    cfg.stream.out_height = iNode(st["out_height"], cfg.stream.out_height);
    cfg.stream.bitrate_kbps = iNode(st["bitrate_kbps"], cfg.stream.bitrate_kbps);
    cfg.stream.colormap = sNode(st["colormap"], cfg.stream.colormap);
    cfg.stream.temp_min_c = dNode(st["temp_min_c"], cfg.stream.temp_min_c);
    cfg.stream.temp_max_c = dNode(st["temp_max_c"], cfg.stream.temp_max_c);

    auto d = fs["detection"];
    cfg.detect.crib_x = iNode(d["crib_x"], cfg.detect.crib_x);
    cfg.detect.crib_y = iNode(d["crib_y"], cfg.detect.crib_y);
    cfg.detect.crib_w = iNode(d["crib_w"], cfg.detect.crib_w);
    cfg.detect.crib_h = iNode(d["crib_h"], cfg.detect.crib_h);
    cfg.detect.wet_y_frac_start = dNode(d["wet_y_frac_start"], cfg.detect.wet_y_frac_start);
    cfg.detect.min_baby_area = iNode(d["min_baby_area"], cfg.detect.min_baby_area);
    cfg.detect.warm_delta_c = dNode(d["warm_delta_c"], cfg.detect.warm_delta_c);
    cfg.detect.head_top_frac = dNode(d["head_top_frac"], cfg.detect.head_top_frac);
    cfg.detect.hottest_frac = dNode(d["hottest_frac"], cfg.detect.hottest_frac);
    cfg.detect.temp_smooth_alpha = dNode(d["temp_smooth_alpha"], cfg.detect.temp_smooth_alpha);
    cfg.detect.baseline_alpha = dNode(d["baseline_alpha"], cfg.detect.baseline_alpha);
    cfg.detect.baseline_motion_freeze_delta = dNode(d["baseline_motion_freeze_delta"], cfg.detect.baseline_motion_freeze_delta);
    cfg.detect.baseline_ambient_jump_freeze_delta = dNode(d["baseline_ambient_jump_freeze_delta"], cfg.detect.baseline_ambient_jump_freeze_delta);
    cfg.detect.wet_cold_delta = dNode(d["wet_cold_delta"], cfg.detect.wet_cold_delta);
    cfg.detect.wet_warm_delta = dNode(d["wet_warm_delta"], cfg.detect.wet_warm_delta);
    cfg.detect.wet_enter_area = iNode(d["wet_enter_area"], cfg.detect.wet_enter_area);
    cfg.detect.wet_exit_area = iNode(d["wet_exit_area"], cfg.detect.wet_exit_area);
    cfg.detect.wet_persist_sec = dNode(d["wet_persist_sec"], cfg.detect.wet_persist_sec);

    auto r = fs["runtime"];
    cfg.runtime.headless = bNode(r["headless"], cfg.runtime.headless);
    cfg.runtime.preview = bNode(r["preview"], cfg.runtime.preview);
    cfg.runtime.events_file = sNode(r["events_file"], cfg.runtime.events_file);
    cfg.runtime.status_every_n_frames = iNode(r["status_every_n_frames"], cfg.runtime.status_every_n_frames);
}

class EventLog {
  public:
    explicit EventLog(const std::string& path) {
        auto p = expandHome(path);
        if (!p.empty()) {
            out_.open(p, std::ios::app);
            if (!out_) std::cerr << "[WARN] Cannot open event file: " << p << "\n";
        }
    }
    void emit(const std::string& type, const std::string& state, const std::string& reason,
              int cold, int warm, double ambient, double conf) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(3)
           << "{\"ts\":\"" << isoTs() << "\",\"event_type\":\"" << type << "\",\"state\":\"" << state
           << "\",\"reason\":\"" << reason << "\",\"cold_area\":" << cold << ",\"warm_area\":" << warm
           << ",\"ambient_c\":" << ambient << ",\"confidence\":" << conf << "}";
        std::string line = os.str();
        std::cout << line << "\n";
        if (out_) { out_ << line << "\n"; out_.flush(); }
    }
  private:
    std::ofstream out_;
};

int colorMap(const std::string& name) {
    std::string v = lower(name);
    if (v == "turbo") return cv::COLORMAP_TURBO;
    if (v == "magma") return cv::COLORMAP_MAGMA;
    if (v == "plasma") return cv::COLORMAP_PLASMA;
    if (v == "hot") return cv::COLORMAP_HOT;
    return cv::COLORMAP_INFERNO;
}

class RtpOut {
  public:
    bool open(const StreamCfg& c, int fps) {
        if (!c.enable_rtp) return true;
        size_ = cv::Size(std::max(2, c.out_width), std::max(2, c.out_height));
        // openh264enc is the encoder available on the baby pi (x264enc lives in
        // gstreamer1.0-plugins-ugly which isn't installed). openh264enc takes
        // bitrate in bits/sec (not kbps) and requires a framerate in caps to
        // init; without it the encoder raises cmInitParaError and emits no
        // frames. Force the caps here to match the appsrc's actual fps.
        std::string pipe =
            "appsrc is-live=true format=time do-timestamp=true ! "
            "videoconvert ! "
            "video/x-raw,format=I420,framerate=" + std::to_string(std::max(1, fps)) + "/1 ! "
            "openh264enc bitrate=" + std::to_string(c.bitrate_kbps * 1000) +
            " complexity=low ! rtph264pay pt=96 config-interval=1 ! udpsink host=" +
            c.parent_ip + " port=" + std::to_string(c.rtp_port) + " sync=false async=false";
        w_.open(pipe, cv::CAP_GSTREAMER, 0, std::max(1, fps), size_, true);
        if (!w_.isOpened()) {
            std::cerr << "[ERR] Failed to open RTP pipeline\n";
            return false;
        }
        return true;
    }
    void write(const cv::Mat& bgr) {
        if (!w_.isOpened() || bgr.empty()) return;
        if (bgr.size() == size_) w_.write(bgr);
        else {
            cv::Mat r;
            cv::resize(bgr, r, size_, 0, 0, cv::INTER_LINEAR);
            w_.write(r);
        }
    }
    void close() { if (w_.isOpened()) w_.release(); }
  private:
    cv::VideoWriter w_;
    cv::Size size_{640, 480};
};

class ZmqOut {
  public:
    bool open(const std::string& endpoint) {
        ctx_ = zmq_ctx_new();
        if (!ctx_) return false;
        pub_ = zmq_socket(ctx_, ZMQ_PUB);
        if (!pub_) return false;
        int hwm = 8, linger = 0;
        zmq_setsockopt(pub_, ZMQ_SNDHWM, &hwm, sizeof(hwm));
        zmq_setsockopt(pub_, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_bind(pub_, endpoint.c_str()) != 0) {
            std::cerr << "[ERR] zmq_bind failed " << endpoint << ": " << zmq_strerror(zmq_errno()) << "\n";
            return false;
        }
        return true;
    }
    void publish(uint64_t id, uint64_t ts_ns, const cv::Mat& y16) {
        if (!pub_ || y16.empty()) return;
        std::ostringstream meta;
        meta << "{\"frame_id\":" << id << ",\"ts_monotonic_ns\":" << ts_ns
             << ",\"width\":" << y16.cols << ",\"height\":" << y16.rows << ",\"pixel_format\":\"Y16\"}";
        auto m = meta.str();
        size_t bytes = (size_t)y16.cols * (size_t)y16.rows * sizeof(uint16_t);
        if (zmq_send(pub_, m.data(), m.size(), ZMQ_SNDMORE) < 0) return;
        zmq_send(pub_, y16.data, bytes, 0);
    }
    void close() {
        if (pub_) { zmq_close(pub_); pub_ = nullptr; }
        if (ctx_) { zmq_ctx_term(ctx_); ctx_ = nullptr; }
    }
    ~ZmqOut() { close(); }
  private:
    void* ctx_ = nullptr;
    void* pub_ = nullptr;
};

struct UvcFrame {
    int w = 0;
    int h = 0;
    uvc_frame_format fmt = UVC_FRAME_FORMAT_UNKNOWN;
    uint64_t id = 0;
    uint64_t ts_ns = 0;
    std::vector<uint16_t> data;
};

class UvcGrabber {
  public:
    explicit UvcGrabber(const CaptureCfg& c) : c_(c) {}
    ~UvcGrabber() { stop(); }

    bool start(std::string& err) {
        stop();
        uvc_error_t r = uvc_init(&ctx_, nullptr);
        if (r < 0) { err = std::string("uvc_init: ") + uvc_strerror(r); return false; }
        r = uvc_find_device(ctx_, &dev_, c_.vendor_id, c_.product_id, nullptr);
        if (r < 0) { err = std::string("uvc_find_device: ") + uvc_strerror(r); stop(); return false; }
        r = uvc_open(dev_, &devh_);
        if (r < 0) { err = std::string("uvc_open: ") + uvc_strerror(r); stop(); return false; }
        r = uvc_get_stream_ctrl_format_size(devh_, &ctrl_, UVC_FRAME_FORMAT_Y16, c_.request_width, c_.request_height, c_.fps);
        if (r < 0 && c_.telemetry_rows > 0) {
            r = uvc_get_stream_ctrl_format_size(devh_, &ctrl_, UVC_FRAME_FORMAT_Y16, c_.request_width, c_.request_height + c_.telemetry_rows, c_.fps);
        }
        if (r < 0) { err = std::string("get_stream_ctrl_format_size: ") + uvc_strerror(r); stop(); return false; }
        r = uvc_start_streaming(devh_, &ctrl_, &UvcGrabber::cb, this, 0);
        if (r < 0) { err = std::string("uvc_start_streaming: ") + uvc_strerror(r); stop(); return false; }
        streaming_ = true;
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            streaming_ = false;
        }
        cv_.notify_all();
        if (devh_) uvc_stop_streaming(devh_);
        if (devh_) { uvc_close(devh_); devh_ = nullptr; }
        if (dev_) { uvc_unref_device(dev_); dev_ = nullptr; }
        if (ctx_) { uvc_exit(ctx_); ctx_ = nullptr; }
    }

    bool waitNew(uint64_t last_id, int timeout_ms, UvcFrame& out) {
        std::unique_lock<std::mutex> lk(m_);
        bool ok = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return latest_.id > last_id || !streaming_; });
        if (!ok || latest_.id <= last_id) return false;
        out = latest_;
        return true;
    }

  private:
    static void cb(uvc_frame_t* f, void* user) {
        if (!user || !f || !f->data || f->width <= 0 || f->height <= 0) return;
        auto* self = static_cast<UvcGrabber*>(user);
        size_t px = (size_t)f->width * (size_t)f->height;
        size_t bytes = px * sizeof(uint16_t);
        if (f->data_bytes < bytes) return;

        UvcFrame local;
        local.w = f->width; local.h = f->height; local.fmt = f->frame_format;
        local.ts_ns = monotonicNs();
        local.data.resize(px);
        std::memcpy(local.data.data(), f->data, bytes);
        {
            std::lock_guard<std::mutex> lk(self->m_);
            local.id = self->latest_.id + 1;
            self->latest_ = std::move(local);
        }
        self->cv_.notify_one();
    }

    CaptureCfg c_;
    uvc_context_t* ctx_ = nullptr;
    uvc_device_t* dev_ = nullptr;
    uvc_device_handle_t* devh_ = nullptr;
    uvc_stream_ctrl_t ctrl_{};
    std::mutex m_;
    std::condition_variable cv_;
    UvcFrame latest_;
    bool streaming_ = false;
};

uint64_t signature16(const cv::Mat& y16) {
    const uint16_t* p = y16.ptr<uint16_t>(0);
    size_t n = y16.total();
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i += 47) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

bool extractThermal(const UvcFrame& f, const CaptureCfg& cfg, cv::Mat& out, std::string& diag) {
    if (f.w <= 0 || f.h <= 0 || f.data.empty()) return false;
    cv::Mat raw(f.h, f.w, CV_16UC1, const_cast<uint16_t*>(f.data.data()));
    int top = 0, bot = f.h;
    if (cfg.telemetry_rows > 0) {
        if (cfg.telemetry_rows >= f.h) return false;
        if (cfg.telemetry_bottom) bot = f.h - cfg.telemetry_rows;
        else top = cfg.telemetry_rows;
    }
    out = raw.rowRange(top, bot).clone();
    diag = "uvc_fmt=" + std::to_string((int)f.fmt) + " raw=" + std::to_string(f.w) + "x" + std::to_string(f.h) +
           " thermal=" + std::to_string(out.cols) + "x" + std::to_string(out.rows);
    return true;
}

double median32f(const cv::Mat& m) {
    std::vector<float> v;
    v.reserve(m.total());
    if (m.isContinuous()) {
        auto* p = m.ptr<float>(0);
        v.assign(p, p + m.total());
    } else {
        for (int r = 0; r < m.rows; ++r) {
            auto* p = m.ptr<float>(r);
            v.insert(v.end(), p, p + m.cols);
        }
    }
    if (v.empty()) return 0.0;
    auto mid = v.begin() + (v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

int largestCC(const cv::Mat& mask) {
    if (mask.empty()) return 0;
    cv::Mat labels, stats, cent;
    int n = cv::connectedComponentsWithStats(mask, labels, stats, cent, 8, CV_32S);
    int a = 0;
    for (int i = 1; i < n; ++i) a = std::max(a, stats.at<int>(i, cv::CC_STAT_AREA));
    return a;
}

enum class WetState { None, Cold, Warm };
const char* wetName(WetState s) {
    if (s == WetState::Cold) return "cold";
    if (s == WetState::Warm) return "warm";
    return "none";
}

struct DetectState {
    cv::Mat baseline;
    cv::Mat prev_crib;
    bool has_prev_ambient = false;
    double prev_ambient = 0.0;
    bool has_head = false;
    double head_sm = 0.0;
    WetState latched = WetState::None;
    WetState pending = WetState::None;
    bool pending_on = false;
    Clock::time_point pending_since{};
};

struct DetectOut {
    cv::Rect crib, baby, head;
    bool has_baby = false;
    bool has_head = false;
    double head_c = 0.0;
    double ambient = 0.0;
    int wet_y = 0;
    int cold_area = 0, warm_area = 0;
    bool baseline_frozen = false;
    WetState state = WetState::None;
    bool changed = false;
    std::string reason = "stable";
    double confidence = 0.0;
};

DetectOut runDetect(const cv::Mat& full_c, const DetectCfg& cfg, DetectState& st) {
    DetectOut o;
    cv::Rect full(0, 0, full_c.cols, full_c.rows);
    o.crib = (cfg.crib_w > 0 && cfg.crib_h > 0) ? (cv::Rect(cfg.crib_x, cfg.crib_y, cfg.crib_w, cfg.crib_h) & full) : full;
    if (o.crib.width < 4 || o.crib.height < 4) o.crib = full;
    cv::Mat crib = full_c(o.crib).clone();
    int ch = crib.rows, cw = crib.cols;
    if (st.baseline.empty()) st.baseline = crib.clone();

    o.ambient = median32f(crib);
    cv::Mat warm;
    cv::compare(crib, o.ambient + cfg.warm_delta_c, warm, cv::CMP_GT);
    cv::morphologyEx(warm, warm, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 1);
    cv::morphologyEx(warm, warm, cv::MORPH_CLOSE, cv::Mat::ones(5, 5, CV_8U), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> cs;
    cv::findContours(warm, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat baby_mask = cv::Mat::zeros(ch, cw, CV_8U);
    if (!cs.empty()) {
        auto it = std::max_element(cs.begin(), cs.end(), [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
        if (it != cs.end() && cv::contourArea(*it) >= cfg.min_baby_area) {
            cv::Rect b = cv::boundingRect(*it);
            o.has_baby = true;
            o.baby = cv::Rect(o.crib.x + b.x, o.crib.y + b.y, b.width, b.height);
            cv::drawContours(baby_mask, std::vector<std::vector<cv::Point>>{*it}, -1, 255, -1);

            int hh = std::max(2, (int)std::round(b.height * std::clamp(cfg.head_top_frac, 0.1, 0.9)));
            cv::Rect h(b.x, b.y, b.width, std::min(hh, b.height));
            o.head = cv::Rect(o.crib.x + h.x, o.crib.y + h.y, h.width, h.height);
            cv::Mat hm = cv::Mat::zeros(ch, cw, CV_8U);
            hm(h).setTo(255);
            cv::Mat m;
            cv::bitwise_and(baby_mask, hm, m);
            std::vector<float> vals;
            vals.reserve((size_t)h.area());
            for (int r = 0; r < ch; ++r) {
                const uchar* mp = m.ptr<uchar>(r);
                const float* cp = crib.ptr<float>(r);
                for (int c = 0; c < cw; ++c) if (mp[c]) vals.push_back(cp[c]);
            }
            if (vals.size() > 20) {
                int k = std::max(5, (int)std::round(vals.size() * std::clamp(cfg.hottest_frac, 0.05, 0.5)));
                std::nth_element(vals.begin(), vals.end() - k, vals.end());
                double sum = 0.0; for (int i = (int)vals.size() - k; i < (int)vals.size(); ++i) sum += vals[i];
                o.head_c = sum / k; o.has_head = true;
                if (!st.has_head) { st.head_sm = o.head_c; st.has_head = true; }
                else { double a = std::clamp(cfg.temp_smooth_alpha, 0.01, 0.95); st.head_sm = (1.0 - a) * st.head_sm + a * o.head_c; }
                o.head_c = st.head_sm;
            }
        }
    }

    bool freeze = false;
    if (!st.prev_crib.empty() && st.prev_crib.size() == crib.size()) {
        cv::Mat d; cv::absdiff(crib, st.prev_crib, d);
        if (cv::mean(d)[0] >= cfg.baseline_motion_freeze_delta) freeze = true;
    }
    if (st.has_prev_ambient && std::abs(o.ambient - st.prev_ambient) >= cfg.baseline_ambient_jump_freeze_delta) freeze = true;
    o.baseline_frozen = freeze;
    if (!freeze) {
        double a = std::clamp(cfg.baseline_alpha, 0.0001, 0.5);
        cv::Mat blend; cv::addWeighted(st.baseline, 1.0 - a, crib, a, 0.0, blend);
        cv::Mat non_baby; cv::bitwise_not(baby_mask, non_baby);
        blend.copyTo(st.baseline, non_baby);
    }
    st.prev_crib = crib;
    st.prev_ambient = o.ambient;
    st.has_prev_ambient = true;

    int wy = std::clamp((int)std::round(ch * std::clamp(cfg.wet_y_frac_start, 0.0, 0.95)), 0, std::max(0, ch - 1));
    o.wet_y = o.crib.y + wy;
    cv::Mat wet = crib.rowRange(wy, ch), base = st.baseline.rowRange(wy, ch), diff;
    cv::subtract(wet, base, diff, cv::noArray(), CV_32F);
    cv::Mat cm, wm;
    cv::compare(diff, cfg.wet_cold_delta, cm, cv::CMP_LE);
    cv::compare(diff, cfg.wet_warm_delta, wm, cv::CMP_GE);
    cv::morphologyEx(cm, cm, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 1);
    cv::morphologyEx(cm, cm, cv::MORPH_CLOSE, cv::Mat::ones(5, 5, CV_8U), cv::Point(-1, -1), 2);
    cv::morphologyEx(wm, wm, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 1);
    cv::morphologyEx(wm, wm, cv::MORPH_CLOSE, cv::Mat::ones(5, 5, CV_8U), cv::Point(-1, -1), 2);
    o.cold_area = largestCC(cm);
    o.warm_area = largestCC(wm);

    int enter = std::max(1, cfg.wet_enter_area), exit = std::max(1, cfg.wet_exit_area);
    WetState obs = st.latched;
    if (st.latched == WetState::None) obs = (o.cold_area >= enter) ? WetState::Cold : ((o.warm_area >= enter) ? WetState::Warm : WetState::None);
    else if (st.latched == WetState::Cold) obs = (o.cold_area >= exit) ? WetState::Cold : ((o.warm_area >= enter) ? WetState::Warm : WetState::None);
    else obs = (o.warm_area >= exit) ? WetState::Warm : ((o.cold_area >= enter) ? WetState::Cold : WetState::None);

    auto now = Clock::now();
    if (obs != st.latched) {
        if (!st.pending_on || st.pending != obs) {
            st.pending_on = true; st.pending = obs; st.pending_since = now; o.reason = "candidate_change";
        } else {
            double s = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.pending_since).count() / 1000.0;
            if (s >= cfg.wet_persist_sec) { st.latched = obs; st.pending_on = false; o.changed = true; o.reason = "persistence_met"; }
            else o.reason = "persistence_wait";
        }
    } else st.pending_on = false;
    o.state = st.latched;
    double area_conf = std::min(1.0, (double)std::max(o.cold_area, o.warm_area) / (double)enter);
    if (st.pending_on) {
        double s = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.pending_since).count() / 1000.0;
        area_conf *= std::min(1.0, s / std::max(0.1, cfg.wet_persist_sec));
    }
    o.confidence = area_conf;
    return o;
}

void colorize(const cv::Mat& temp_c, double lo, double hi, int cmap, cv::Mat& bgr) {
    double h = std::max(hi, lo + 0.1);
    cv::Mat clamped; cv::min(cv::max(temp_c, lo), h, clamped);
    cv::Mat u8; clamped.convertTo(u8, CV_8U, 255.0 / (h - lo), -lo * 255.0 / (h - lo));
    cv::applyColorMap(u8, bgr, cmap);
}

void text(cv::Mat& img, const std::string& s, cv::Point p, cv::Scalar c) {
    constexpr double kScale = 0.16;  // Smaller overlay for full-screen parent display.
    cv::putText(img, s, p, cv::FONT_HERSHEY_SIMPLEX, kScale, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    cv::putText(img, s, p, cv::FONT_HERSHEY_SIMPLEX, kScale, c, 1, cv::LINE_AA);
}

void drawPreview(cv::Mat& img, const DetectOut& d, bool recovering) {
    cv::rectangle(img, d.crib, cv::Scalar(255, 255, 255), 1);
    cv::line(img, cv::Point(d.crib.x, d.wet_y), cv::Point(d.crib.x + d.crib.width - 1, d.wet_y), cv::Scalar(200, 200, 200), 1);
    if (d.has_baby) { cv::rectangle(img, d.baby, cv::Scalar(255, 255, 255), 1); cv::rectangle(img, d.head, cv::Scalar(180, 220, 255), 1); }

    const int x = 6;
    const int y0 = 10;
    const int lh = 8;

    std::string h = d.has_head ? (std::to_string(d.head_c).substr(0, 4) + "C") : "n/a";
    text(img, "Amb " + std::to_string(d.ambient).substr(0, 4) + "C  Head " + h, cv::Point(x, y0), cv::Scalar(255, 255, 255));
    cv::Scalar sc = d.state == WetState::None ? cv::Scalar(120, 255, 120) : (d.state == WetState::Cold ? cv::Scalar(255, 220, 90) : cv::Scalar(100, 180, 255));
    text(img, "Wet " + std::string(wetName(d.state)), cv::Point(x, y0 + lh), sc);
    text(img, d.baseline_frozen ? "Base: frozen" : "Base: adaptive", cv::Point(x, y0 + (2 * lh)),
         d.baseline_frozen ? cv::Scalar(130, 220, 255) : cv::Scalar(130, 255, 130));
    if (recovering) text(img, "RECOVERY", cv::Point(x, y0 + (3 * lh)), cv::Scalar(60, 60, 255));
}

bool startWithRetry(UvcGrabber& g, const CaptureCfg& c) {
    for (int i = 1; i <= std::max(1, c.max_reopen_attempts) && !g_stop.load(); ++i) {
        std::string err;
        if (g.start(err)) { std::cout << "[INFO] Capture opened attempt " << i << "\n"; return true; }
        std::cerr << "[WARN] Capture open failed " << i << ": " << err << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(100, c.reopen_backoff_ms)));
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Cli cli = parseCli(argc, argv);
    if (cli.help) {
        usage(argv[0]);
        return 0;
    }

    AppCfg cfg;
    cfg.config_path = cli.config;
    loadYaml(cli.config, cfg);
    if (!cli.parent_ip.empty()) cfg.stream.parent_ip = cli.parent_ip;
    if (cli.rtp_port > 0) cfg.stream.rtp_port = cli.rtp_port;
    if (!cli.zmq_endpoint.empty()) cfg.stream.zmq_endpoint = cli.zmq_endpoint;
    if (cli.preview) { cfg.runtime.preview = true; cfg.runtime.headless = false; }
    if (cli.headless) { cfg.runtime.headless = true; cfg.runtime.preview = false; }
    cfg.runtime.status_every_n_frames = std::max(1, cfg.runtime.status_every_n_frames);
    cfg.capture.frame_timeout_ms = std::max(300, cfg.capture.frame_timeout_ms);
    cfg.capture.max_same_signature_frames = std::max(10, cfg.capture.max_same_signature_frames);

    EventLog events(cfg.runtime.events_file);
    events.emit("startup", "none", "service_start", 0, 0, 0.0, 0.0);

    RtpOut rtp;
    if (!rtp.open(cfg.stream, cfg.capture.fps)) {
        events.emit("startup_error", "none", "rtp_init_failed", 0, 0, 0.0, 0.0);
        return 2;
    }
    ZmqOut zmq;
    if (cfg.stream.enable_raw_zmq && !zmq.open(cfg.stream.zmq_endpoint)) {
        events.emit("startup_error", "none", "zmq_init_failed", 0, 0, 0.0, 0.0);
        return 3;
    }

    UvcGrabber cap(cfg.capture);
    if (!startWithRetry(cap, cfg.capture)) {
        events.emit("startup_error", "none", "capture_open_failed", 0, 0, 0.0, 0.0);
        return 4;
    }

    DetectState ds;
    int cmap = colorMap(cfg.stream.colormap);
    uint64_t last_id = 0, last_sig = 0;
    int same_sig = 0;
    int frames = 0;
    bool recovering = false;
    bool first_ok = false;

    while (!g_stop.load()) {
        UvcFrame uf;
        if (!cap.waitNew(last_id, cfg.capture.frame_timeout_ms, uf)) {
            events.emit("capture_recovery", wetName(ds.latched), "frame_timeout", 0, 0, 0.0, 0.0);
            recovering = true;
            cap.stop();
            if (!startWithRetry(cap, cfg.capture)) {
                events.emit("fatal", wetName(ds.latched), "capture_recovery_failed", 0, 0, 0.0, 0.0);
                break;
            }
            ds = DetectState{};
            last_id = 0;
            same_sig = 0;
            continue;
        }
        recovering = false;
        last_id = uf.id;

        cv::Mat y16;
        std::string diag;
        if (!extractThermal(uf, cfg.capture, y16, diag)) continue;

        if (cfg.capture.strict_native_160x120 && (y16.cols != 160 || y16.rows != 120)) {
            std::cerr << "[ERR] native 160x120 required, got " << y16.cols << "x" << y16.rows << " (" << diag << ")\n";
            events.emit("fatal", "none", "native_160x120_required", 0, 0, 0.0, 0.0);
            cap.stop();
            return 5;
        }
        if (!first_ok) {
            std::cout << "[INFO] First frame OK: " << diag << "\n";
            first_ok = true;
        }

        uint64_t sig = signature16(y16);
        if (sig == last_sig) same_sig++; else { same_sig = 0; last_sig = sig; }
        if (same_sig >= cfg.capture.max_same_signature_frames) {
            events.emit("capture_recovery", wetName(ds.latched), "stale_signature", 0, 0, 0.0, 0.0);
            recovering = true;
            cap.stop();
            if (!startWithRetry(cap, cfg.capture)) {
                events.emit("fatal", wetName(ds.latched), "capture_recovery_failed", 0, 0, 0.0, 0.0);
                break;
            }
            ds = DetectState{};
            last_id = 0;
            same_sig = 0;
            continue;
        }

        cv::Mat tc;
        y16.convertTo(tc, CV_32F, 0.01, -273.15);
        DetectOut d = runDetect(tc, cfg.detect, ds);

        cv::Mat color;
        colorize(tc, cfg.stream.temp_min_c, cfg.stream.temp_max_c, cmap, color);
        if (cfg.stream.enable_rtp) rtp.write(color);
        if (cfg.stream.enable_raw_zmq) zmq.publish(uf.id, uf.ts_ns, y16);

        if (d.changed) {
            events.emit(d.state == WetState::None ? "wet_clear" : "wet_alert",
                        wetName(d.state), d.reason, d.cold_area, d.warm_area, d.ambient, d.confidence);
        } else if ((frames % cfg.runtime.status_every_n_frames) == 0) {
            events.emit("status", wetName(d.state), d.reason, d.cold_area, d.warm_area, d.ambient, d.confidence);
        }

        if (cfg.runtime.preview && !cfg.runtime.headless) {
            cv::Mat view = color.clone();
            drawPreview(view, d, recovering);
            cv::imshow("Lepton Monitor 160x120", view);
            int k = cv::waitKey(1) & 0xFF;
            if (k == 27 || k == 'q' || k == 'Q') g_stop.store(true);
        }
        frames++;
    }

    cap.stop();
    rtp.close();
    zmq.close();
    if (cfg.runtime.preview && !cfg.runtime.headless) cv::destroyAllWindows();
    events.emit("shutdown", wetName(ds.latched), "service_stop", 0, 0, 0.0, 0.0);
    return 0;
}
