// mlx90640_streaming.cpp
// IR camera streaming program for Baby Pi
// Streams thermal camera feed to Parent Pi via GStreamer/RTP

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <algorithm>
#include <string>

#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

#include <opencv2/opencv.hpp>

#define MLX_I2C_ADDR 0x33
#define ROWS 24
#define COLS 32
#define PIXELS (ROWS*COLS)

// Configurable parameters
std::string g_parent_ip = "10.0.0.98";
int g_parent_port = 5000;
const int OUT_WIDTH = 640;   // even dimensions for H.264
const int OUT_HEIGHT = 480;
const int FPS = 8;

int main(int argc, char** argv) {
    // Allow parent IP to be passed as argument
    if (argc >= 2) {
        g_parent_ip = argv[1];
    }
    if (argc >= 3) {
        g_parent_port = std::atoi(argv[2]);
    }

    printf("[MLX] Starting streaming to %s:%d\n", g_parent_ip.c_str(), g_parent_port);

    int status;
    uint16_t eeData[832];
    paramsMLX90640 mlxParams;
    uint16_t frameData[834];
    float to[PIXELS];

    // Init I2C
    MLX90640_I2CInit();

    status = MLX90640_DumpEE(MLX_I2C_ADDR, eeData);
    if (status != 0) { 
        fprintf(stderr, "[MLX] DumpEE failed: %d\n", status); 
        return 1; 
    }

    status = MLX90640_ExtractParameters(eeData, &mlxParams);
    if (status != 0) { 
        fprintf(stderr, "[MLX] ExtractParameters failed: %d\n", status); 
        return 1; 
    }

    // Set refresh rate (0x03 = ~4 Hz, 0x04 = ~8 Hz)
    MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0x04);

    // Build GStreamer pipeline
    std::string gst_pipeline = 
        "appsrc ! videoconvert ! "
        "x264enc tune=zerolatency bitrate=800 speed-preset=ultrafast key-int-max=15 ! "
        "rtph264pay pt=96 config-interval=1 ! "
        "udpsink host=" + g_parent_ip + " port=" + std::to_string(g_parent_port);

    cv::VideoWriter writer;
    if (!writer.open(gst_pipeline, cv::CAP_GSTREAMER, 0, FPS, cv::Size(OUT_WIDTH, OUT_HEIGHT), true)) {
        fprintf(stderr, "[MLX] Failed to open GStreamer VideoWriter\n");
        return 1;
    }

    printf("[MLX] GStreamer pipeline opened, streaming at %dx%d @ %d fps\n", OUT_WIDTH, OUT_HEIGHT, FPS);

    int frame_count = 0;
    while (true) {
        status = MLX90640_GetFrameData(MLX_I2C_ADDR, frameData);
        if (status < 0) continue; // skip bad frames

        float ta = MLX90640_GetTa(frameData, &mlxParams);
        float emissivity = 0.95f;
        float tr = ta - 8.0f;

        MLX90640_CalculateTo(frameData, &mlxParams, emissivity, tr, to);

        // Fixed range for stable colors
        float tmin = 26.0f;
        float tmax = 38.0f;

        // Put temps into float Mat (24x32)
        cv::Mat tempF(ROWS, COLS, CV_32F);
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                tempF.at<float>(r, c) = to[r*COLS + c];
            }
        }

        // Upscale temps with cubic interpolation
        cv::Mat tempUpF;
        cv::resize(tempF, tempUpF,
                   cv::Size(OUT_WIDTH, OUT_HEIGHT),
                   0, 0, cv::INTER_CUBIC);

        // Clamp to range, convert to 8-bit
        cv::Mat tempClamped;
        cv::min(cv::max(tempUpF, tmin), tmax, tempClamped);

        cv::Mat img8;
        tempClamped.convertTo(
            img8, CV_8U,
            255.0f/(tmax - tmin),
            -tmin * 255.0f/(tmax - tmin)
        );

        // Apply colormap
        cv::Mat colored;
        cv::applyColorMap(img8, colored, cv::COLORMAP_INFERNO);

        // Overlay range + ambient text
        char buf[200];
        snprintf(buf, sizeof(buf), "Range %.1f-%.1fC  Ta %.1fC", tmin, tmax, ta);
        cv::putText(colored, buf, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255,255,255), 2);

        // Write frame to GStreamer
        writer.write(colored);

        frame_count++;
        if (frame_count % 100 == 0) {
            printf("[MLX] Streamed %d frames\n", frame_count);
        }

        usleep(125000); // ~8 Hz pacing for 0x04
    }

    return 0;
}

