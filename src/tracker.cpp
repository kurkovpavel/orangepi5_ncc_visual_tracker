#include "tracker.h"
#include "opencl_utils.h"
#include <iostream>
#include <random>

VisualTracker::VisualTracker() : template_initialized(false), template_buf(nullptr) {
}

VisualTracker::~VisualTracker() {
    cleanup();
}

bool VisualTracker::initialize() {
    try {
        context = OpenCLUtils::createContext();
        queue = OpenCLUtils::createCommandQueue(context);
        program = OpenCLUtils::createProgramFromFile(context, "tracker_kernels.cl");
        
        // Debug: Check available kernels
        size_t kernel_count;
        clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, sizeof(size_t), &kernel_count, NULL);
        std::cout << "Number of kernels in program: " << kernel_count << std::endl;
        
        char kernel_names[1024];
        clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, sizeof(kernel_names), kernel_names, NULL);
        std::cout << "Available kernels: " << kernel_names << std::endl;
        
        // Try different kernel names
        const char* possible_kernel_names[] = {
            "direct_ncc_tracker",
            "grayscale_ncc_tracker", 
            "feature_extraction",
            "correlation_layer",
            "detection_head",
            NULL
        };
        
        cl_kernel kernel = NULL;
        cl_int error;
        const char* used_kernel_name = NULL;
        
        for (int i = 0; possible_kernel_names[i] != NULL; i++) {
            kernel = clCreateKernel(program, possible_kernel_names[i], &error);
            if (error == CL_SUCCESS && kernel != NULL) {
                used_kernel_name = possible_kernel_names[i];
                std::cout << "Successfully created kernel: " << used_kernel_name << std::endl;
                break;
            }
        }
        
        if (kernel == NULL) {
            throw std::runtime_error("Failed to create any kernel from the program");
        }
        
        ncc_kernel = kernel;
        
        std::cout << "Simple NCC Tracker initialized successfully with kernel: " << used_kernel_name << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void VisualTracker::setTemplate(const cv::Mat& template_roi) {
    // Use original template size, but ensure it's not too large
    cv::Mat processed = template_roi.clone();
    
    // If template is too large, resize it
    if (processed.cols > 100 || processed.rows > 100) {
        cv::resize(processed, processed, cv::Size(80, 80));
    }
    
    template_size = processed.size();
    
    // Clean up previous template
    if (template_initialized) {
        clReleaseMemObject(template_buf);
    }
    
    // Create buffer for template
    size_t template_size_bytes = template_size.width * template_size.height * 3 * sizeof(uchar);
    template_buf = clCreateBuffer(context, CL_MEM_READ_ONLY, template_size_bytes, NULL, NULL);
    
    // Copy template to GPU
    clEnqueueWriteBuffer(queue, template_buf, CL_TRUE, 0, template_size_bytes, processed.data, 0, NULL, NULL);
    
    template_initialized = true;
    std::cout << "Template set with size: " << template_size << std::endl;
}

bool VisualTracker::track(const cv::Mat& search_region, cv::Point& location, float& confidence) {
    if (!template_initialized) {
        std::cerr << "Template not initialized!" << std::endl;
        return false;
    }
    
    // Use search region as-is (no resizing)
    cv::Mat processed_search = search_region.clone();
    int search_width = processed_search.cols;
    int search_height = processed_search.rows;
    
    // Calculate correlation map size
    int corr_width = search_width - template_size.width;
    int corr_height = search_height - template_size.height;
    
    std::cout << "Search: " << search_width << "x" << search_height 
              << ", Template: " << template_size.width << "x" << template_size.height
              << ", Corr map: " << corr_width << "x" << corr_height << std::endl;
    
    if (corr_width <= 0 || corr_height <= 0) {
        std::cerr << "Search region too small for template matching!" << std::endl;
        // Fallback: return center of search region
        location = cv::Point(search_region.cols / 2, search_region.rows / 2);
        confidence = 0.0f;
        return false;
    }
    
    // Create buffers
    cl_mem search_buf = clCreateBuffer(context, CL_MEM_READ_ONLY, 
                                      search_width * search_height * 3 * sizeof(uchar), NULL, NULL);
    cl_mem correlation_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
                                          corr_width * corr_height * sizeof(float), NULL, NULL);
    
    // Copy search region to GPU
    clEnqueueWriteBuffer(queue, search_buf, CL_TRUE, 0, 
                        search_width * search_height * 3 * sizeof(uchar), processed_search.data, 0, NULL, NULL);
    
    // Create variables for literal values
    int channels = 3; // RGB channels
    
    // Set kernel arguments
    clSetKernelArg(ncc_kernel, 0, sizeof(cl_mem), &template_buf);
    clSetKernelArg(ncc_kernel, 1, sizeof(cl_mem), &search_buf);
    clSetKernelArg(ncc_kernel, 2, sizeof(cl_mem), &correlation_buf);
    clSetKernelArg(ncc_kernel, 3, sizeof(int), &template_size.width);
    clSetKernelArg(ncc_kernel, 4, sizeof(int), &template_size.height);
    clSetKernelArg(ncc_kernel, 5, sizeof(int), &search_width);
    clSetKernelArg(ncc_kernel, 6, sizeof(int), &search_height);
    clSetKernelArg(ncc_kernel, 7, sizeof(int), &channels); // Use variable instead of &3
    
    // Execute kernel
    size_t global_size[2] = {corr_width, corr_height};
    clEnqueueNDRangeKernel(queue, ncc_kernel, 2, NULL, global_size, NULL, 0, NULL, NULL);
    
    // Read correlation map
    std::vector<float> correlation_map(corr_width * corr_height);
    clEnqueueReadBuffer(queue, correlation_buf, CL_TRUE, 0, 
                       corr_width * corr_height * sizeof(float), correlation_map.data(), 0, NULL, NULL);
    
    // Find best match
    float best_correlation = -1.0f;
    int best_x = 0, best_y = 0;
    
    for (int y = 0; y < corr_height; y++) {
        for (int x = 0; x < corr_width; x++) {
            float corr = correlation_map[y * corr_width + x];
            if (corr > best_correlation) {
                best_correlation = corr;
                best_x = x;
                best_y = y;
            }
        }
    }
    
    // Convert to search region coordinates (center of template)
    location = cv::Point(best_x + template_size.width / 2, best_y + template_size.height / 2);
    confidence = best_correlation;
    
    // Cleanup
    clReleaseMemObject(search_buf);
    clReleaseMemObject(correlation_buf);
    
    // Reasonable confidence threshold for NCC
    bool success = best_correlation > 0.6f;
    
    if (!success) {
        std::cout << "Low confidence match: " << best_correlation << std::endl;
    }
    
    return success;
}

cv::Mat VisualTracker::preprocessImage(const cv::Mat& image) {
    cv::Mat resized;
    // Use larger size for template and keep search region even larger
    if (image.cols > 200 && image.rows > 200) {
        // For search regions - make them larger
        cv::resize(image, resized, cv::Size(200, 200));
    } else {
        // For templates - use original size or slightly smaller
        cv::resize(image, resized, cv::Size(80, 80));
    }
    return resized;
}

void VisualTracker::cleanup() {
    if (template_initialized) {
        clReleaseMemObject(template_buf);
    }
    if (ncc_kernel) clReleaseKernel(ncc_kernel);
    if (program) clReleaseProgram(program);
    if (queue) clReleaseCommandQueue(queue);
    if (context) clReleaseContext(context);
}