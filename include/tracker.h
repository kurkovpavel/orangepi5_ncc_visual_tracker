#pragma once
#include <CL/cl.h>
#include <opencv2/opencv.hpp>
#include <vector>

class VisualTracker {
private:
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel ncc_kernel;
    
    // Template image (stored as OpenCL buffer)
    cl_mem template_buf;
    cv::Size template_size;
    bool template_initialized;
    
public:
    VisualTracker();
    ~VisualTracker();
    
    bool initialize();
    void setTemplate(const cv::Mat& template_roi);
    bool track(const cv::Mat& search_region, cv::Point& location, float& confidence);
    void cleanup();
    
private:
    cv::Mat preprocessImage(const cv::Mat& image);
};