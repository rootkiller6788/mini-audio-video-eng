# Mini Audio Video Engineering（迷你音视频工程）

一套**从零实现、零外部依赖的 C 语言**音视频信号处理与多媒体工程基础库。每个模块对标 MIT（及其他顶尖大学）课程，将教科书公式与行业标准转化为可运行的 C 代码，架起理论与实践的桥梁。

## 子模块

| 子模块 | 主题 | 对标课程 |
|--------|------|----------|
| [mini-3d-audio](mini-3d-audio/) | HRTF、双耳渲染、Ambisonics（一阶/高阶）、VBAP 声像定位、房间声学、球谐函数 | MIT 6.003, Stanford EE359, Berkeley EE123 |
| [mini-audio-codec](mini-audio-codec/) | PCM 编解码、μ律/A 律压扩、MDCT/IMDCT、LPC、心理声学模型、比特分配 | MIT 6.003, Stanford EE102A, ETH 227-0427 |
| [mini-av-sync](mini-av-sync/) | 音视频时钟恢复、唇音同步、抖动缓冲、MPEG STC、偏移检测与补偿、环形缓冲 | MIT 6.003, Stanford EE398, Berkeley EE290 |
| [mini-camera-sensor](mini-camera-sensor/) | CMOS/CCD 传感器建模、ISP 管线、Bayer 去马赛克、自动曝光 (AE/AGC)、色彩科学、噪声建模 | Stanford EE392J, Illinois ECE418, Berkeley EE225B |
| [mini-display-technology](mini-display-technology/) | HDMI/DisplayPort/MIPI DSI、EDID、TMDS 8b/10b、VESA CVT/GTF 时序、扫描输出、帧缓冲 | MIT 6.450, Stanford EE359, Illinois ECE459 |
| [mini-hdr-hfr-video](mini-hdr-hfr-video/) | PQ (ST.2084) / HLG 电光传递函数、色调映射、色彩空间变换、帧插值、高帧率运动估计 | MIT 6.003, Stanford EE392J, ETH 227-0447 |
| [mini-streaming-protocol](mini-streaming-protocol/) | RTP/RTCP (RFC 3550)、MPEG-2 传输流、HLS/DASH 自适应流媒体、抖动缓冲 | MIT 6.829, Stanford CS144, Berkeley EE122 |
| [mini-video-codec](mini-video-codec/) | 二维 DCT/IDCT、运动估计与补偿、熵编码 (CABAC/CAVLC)、去块滤波、帧内/帧间预测 | MIT 6.344, Stanford EE392J, ETH 227-0447 |

## 设计哲学

- **零外部依赖** — 纯 C（C99/C11），仅依赖 `libc` 和 `libm`
- **模块独立自包含** — 每个目录均含独立的 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **标准为纲** — 实现参考 ITU-T、ISO/IEC、VESA、HDMI Forum、IETF RFC 等行业规范
- **理论映射到代码** — 每个模块均含 `docs/` 目录，包含课程对齐笔记与教材参考文献

## 构建

每个模块独立构建。进入模块目录后执行：

```bash
cd mini-3d-audio
make all    # 构建全部目标
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-audio-video-eng/
├── mini-3d-audio/              # 空间音频：HRTF、Ambisonics、双耳渲染
├── mini-audio-codec/           # 音频编码：PCM、压扩、MDCT、心理声学
├── mini-av-sync/               # 唇音同步、音视频时钟恢复、抖动缓冲
├── mini-camera-sensor/         # 图像传感器建模、ISP 管线、去马赛克
├── mini-display-technology/    # 显示接口、EDID、VESA 时序、TMDS
├── mini-hdr-hfr-video/         # HDR EOTF、色调映射、高帧率处理
├── mini-streaming-protocol/    # RTP/RTCP、MPEG-TS、HLS/DASH 自适应流
└── mini-video-codec/           # DCT、运动估计、熵编码、去块滤波
```

## 许可证

MIT
