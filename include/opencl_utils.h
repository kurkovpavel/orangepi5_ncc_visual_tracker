#pragma once
#include <CL/cl.h>
#include <string>
#include <vector>

class OpenCLUtils {
public:
    static cl_context createContext();
    static cl_command_queue createCommandQueue(cl_context context);
    static cl_program createProgramFromFile(cl_context context, const std::string& filename);
    static std::string readKernelSource(const std::string& filename);
    static void checkError(cl_int error, const std::string& message);
};