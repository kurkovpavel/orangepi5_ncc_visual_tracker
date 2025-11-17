#include "opencl_utils.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <iterator>  // Add this for std::istreambuf_iterato

cl_context OpenCLUtils::createContext() {
    cl_platform_id platform;
    cl_device_id device;
    cl_uint num_platforms, num_devices;
    cl_int error;
    
    // Get platform
    error = clGetPlatformIDs(1, &platform, &num_platforms);
    checkError(error, "Failed to get platform IDs");
    
    // Get GPU device
    error = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &num_devices);
    if (error != CL_SUCCESS) {
        std::cout << "No GPU found, trying CPU..." << std::endl;
        error = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, &num_devices);
    }
    checkError(error, "Failed to get device IDs");
    
    // Create context
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &error);
    checkError(error, "Failed to create context");
    
    return context;
}

cl_command_queue OpenCLUtils::createCommandQueue(cl_context context) {
    cl_int error;
    cl_device_id device;
    
    // Get device from context
    error = clGetContextInfo(context, CL_CONTEXT_DEVICES, sizeof(cl_device_id), &device, NULL);
    checkError(error, "Failed to get device from context");
    
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &error);
    checkError(error, "Failed to create command queue");
    
    return queue;
}

cl_program OpenCLUtils::createProgramFromFile(cl_context context, const std::string& filename) {
    std::string source = readKernelSource(filename);
    const char* source_str = source.c_str();
    size_t source_size = source.size();
    
    cl_int error;
    cl_program program = clCreateProgramWithSource(context, 1, &source_str, &source_size, &error);
    checkError(error, "Failed to create program from source");
    
    // Build program
    cl_device_id device;
    error = clGetContextInfo(context, CL_CONTEXT_DEVICES, sizeof(cl_device_id), &device, NULL);
    checkError(error, "Failed to get device for building");
    
    error = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (error != CL_SUCCESS) {
        // Get build log
        size_t log_size;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        std::vector<char> log(log_size);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), NULL);
        std::cerr << "Build failed:\n" << log.data() << std::endl;
        throw std::runtime_error("Program build failed");
    }
    
    return program;
}

std::string OpenCLUtils::readKernelSource(const std::string& filename) {
    std::ifstream file(filename);  // Fixed: should be 'ifstream' not 'iffile'
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open kernel file: " + filename);
    }
    return std::string((std::istreambuf_iterator<char>(file)), 
                       std::istreambuf_iterator<char>());
}

void OpenCLUtils::checkError(cl_int error, const std::string& message) {
    if (error != CL_SUCCESS) {
        throw std::runtime_error(message + " (Error code: " + std::to_string(error) + ")");
    }
}