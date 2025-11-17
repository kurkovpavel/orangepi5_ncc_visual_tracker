#include "tracker.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "framebuffer/framebuffer.h"

// Global control variables
std::atomic<bool> should_select_template(false);
std::atomic<bool> should_quit(false);
std::atomic<bool> should_reset_tracking(false);
std::atomic<int> mouse_x(320);
std::atomic<int> mouse_y(240);
std::atomic<bool> mouse_left_click(false);
std::atomic<bool> mouse_available(false);

// Function to list all input devices
void listInputDevices() {
    std::cout << "=== Available Input Devices ===" << std::endl;
    
    struct dirent **namelist;
    int n = scandir("/dev/input", &namelist, NULL, alphasort);
    
    if (n < 0) {
        perror("scandir");
        return;
    }
    
    for (int i = 0; i < n; i++) {
        if (namelist[i]->d_name[0] != '.') {
            std::string path = std::string("/dev/input/") + namelist[i]->d_name;
            int fd = open(path.c_str(), O_RDONLY);
            if (fd >= 0) {
                struct libevdev *dev = NULL;
                if (libevdev_new_from_fd(fd, &dev) == 0) {
                    std::cout << "Device: " << namelist[i]->d_name 
                              << " - " << libevdev_get_name(dev)
                              << " ("
                              << (libevdev_has_event_type(dev, EV_REL) ? "Mouse" : "")
                              << (libevdev_has_event_type(dev, EV_ABS) ? "Touchpad" : "")
                              << (libevdev_has_event_type(dev, EV_KEY) && 
                                  libevdev_has_event_code(dev, EV_KEY, BTN_LEFT) ? "Mouse" : "")
                              << ")" << std::endl;
                    libevdev_free(dev);
                }
                close(fd);
            }
        }
        free(namelist[i]);
    }
    free(namelist);
}

// Mouse input thread function
void mouseInputThread() {
    std::cout << "Mouse input thread started..." << std::endl;
    
    struct libevdev *dev = NULL;
    int fd = -1;
    
    // List all devices first
    listInputDevices();
    
    // Try to find mouse device
    const char* device_paths[] = {
        "/dev/input/event10",
        "/dev/input/event11", 
        "/dev/input/event0",
        "/dev/input/event1",
        "/dev/input/event2",
        "/dev/input/event3",
        "/dev/input/event4",
        "/dev/input/event5",
        "/dev/input/event6", 
        "/dev/input/event7",
        "/dev/input/event8",
        "/dev/input/event9",
        "/dev/input/mouse0",
        "/dev/input/mice",
        NULL
    };
    
    for (int i = 0; device_paths[i] != NULL; i++) {
        fd = open(device_paths[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            std::cout << "Failed to open " << device_paths[i] << std::endl;
            continue;
        }
        
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            close(fd);
            continue;
        }
        
        std::cout << "Testing device: " << libevdev_get_name(dev) << std::endl;
        
        // Check if this is a mouse (has relative events and left button)
        if (libevdev_has_event_type(dev, EV_REL) && 
            libevdev_has_event_code(dev, EV_KEY, BTN_LEFT)) {
            std::cout << "✓ Found mouse: " << libevdev_get_name(dev) 
                      << " at " << device_paths[i] << std::endl;
            mouse_available = true;
            break;
        } else {
            std::cout << "✗ Not a mouse: " << libevdev_get_name(dev) << std::endl;
            libevdev_free(dev);
            close(fd);
            dev = NULL;
            fd = -1;
        }
    }
    
    if (dev == NULL) {
        std::cerr << "No suitable mouse device found!" << std::endl;
        return;
    }
    
    // Grab the device to get exclusive access
    libevdev_grab(dev, LIBEVDEV_GRAB);
    
    std::cout << "Mouse initialized successfully!" << std::endl;
    
    while (!should_quit) {
        struct input_event ev;
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_REL) {
                if (ev.code == REL_X) {
                    mouse_x += ev.value;
                    // Clamp to screen boundaries (640x480)
                    mouse_x = std::max(0, std::min(1920, mouse_x.load()));
                } else if (ev.code == REL_Y) {
                    mouse_y += ev.value;
                    // Clamp to screen boundaries
                    mouse_y = std::max(0, std::min(1080, mouse_y.load()));
                }
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_LEFT || ev.code == BTN_MOUSE) {
                    bool was_clicked = mouse_left_click.load();
                    mouse_left_click = (ev.value == 1); // 1 = pressed, 0 = released
                    
                    if (mouse_left_click && !was_clicked) {
                        std::cout << "Left mouse click at: " << mouse_x << ", " << mouse_y << std::endl;
                        should_select_template = true;
                    }
                } else if (ev.code == BTN_RIGHT) {
                    // Right click to reset tracking
                    if (ev.value == 1) {
                        std::cout << "Right mouse click - reset tracking" << std::endl;
                        should_reset_tracking = true;
                    }
                }
            }
        } else if (rc == -EAGAIN) {
            // No data available, sleep a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            // Error, sleep a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Cleanup
    libevdev_grab(dev, LIBEVDEV_UNGRAB);
    libevdev_free(dev);
    close(fd);
    std::cout << "Mouse input thread stopped." << std::endl;
}

// Keyboard input thread function
void keyboardInputThread() {
    std::cout << "Keyboard control thread started..." << std::endl;
    std::cout << "Press 'q' to quit, 'r' to reset tracking, 'm' to show mouse position" << std::endl;
    
    while (!should_quit) {
        char key = std::cin.get();
        switch(key) {
            case 'q':
            case 'Q':
                should_quit = true;
                std::cout << "Quit signal received..." << std::endl;
                break;
            case 'r':
            case 'R':
                should_reset_tracking = true;
                std::cout << "Reset tracking requested..." << std::endl;
                break;
            case 's':
            case 'S':
                should_select_template = true;
                std::cout << "Template selection requested..." << std::endl;
                break;
            case 'm':
            case 'M':
                std::cout << "Mouse position: " << mouse_x << ", " << mouse_y << std::endl;
                break;
            default:
                break;
        }
    }
}

// Function to select template around mouse position
cv::Rect selectTemplateAtMouse(const cv::Mat& frame, int mouse_x, int mouse_y, int size = 32) {
    int half_size = size / 2;
    
    int x = std::max(0, mouse_x - half_size);
    int y = std::max(0, mouse_y - half_size);
    int width = std::min(size, frame.cols - x);
    int height = std::min(size, frame.rows - y);
    
    return cv::Rect(x, y, width, height);
}

int main() {
    std::cout << "Starting Visual Tracker on Orange Pi 5..." << std::endl;
    std::cout << "Mouse controls: Left click to select template, Right click to reset" << std::endl;
    std::cout << "Keyboard: 'q'=quit, 'r'=reset, 's'=select template, 'm'=show mouse position" << std::endl;
    
    // Initialize tracker
    VisualTracker tracker;
    if (!tracker.initialize()) {
        std::cerr << "Failed to initialize tracker!" << std::endl;
        return -1;
    }







    // Open camera
    cv::VideoCapture cap(11, cv::CAP_V4L2); // V4L2 camera
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera!" << std::endl;
        return -1;
    }
    
    // Set camera resolution
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
    cap.set(cv::CAP_PROP_FPS, 30);
    
    std::cout << "Camera opened successfully!" << std::endl;

    // Initialize framebuffer
    Framebuffer fb;
    if (!fb.init()) {
        std::cerr << "Cannot init framebuffer" << std::endl;
        return 1;
    }

    // Start display thread
    fb.startDisplayThread();
    std::cout << "Framebuffer display started..." << std::endl;

    // Start input threads
    std::thread mouse_thread(mouseInputThread);
    std::thread keyboard_thread(keyboardInputThread);

    cv::Mat frame;
    bool tracking = false;
    cv::Rect template_roi;
    cv::Point track_point;
    float confidence = 0.0f;
    
    // Wait a bit for mouse detection
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (!mouse_available) {
        std::cout << "WARNING: Mouse not detected. Using keyboard controls only." << std::endl;
        std::cout << "Press 's' to select template at center, 'r' to reset" << std::endl;
    }
    
    // Main loop
    while (!should_quit) {
        // Capture frame
        cap >> frame;
        if (frame.empty()) {
            std::cerr << "Failed to grab frame!" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Display frame with mouse cursor and status
        cv::Mat display_frame = frame.clone();
        
        // Draw mouse cursor if available
        int current_mouse_x = mouse_x;
        int current_mouse_y = mouse_y;
        
        if (mouse_available) {
            // Draw crosshair for mouse
            cv::line(display_frame, 
                    cv::Point(current_mouse_x - 10, current_mouse_y),
                    cv::Point(current_mouse_x + 10, current_mouse_y),
                    cv::Scalar(255, 255, 0), 2);
            cv::line(display_frame, 
                    cv::Point(current_mouse_x, current_mouse_y - 10),
                    cv::Point(current_mouse_x, current_mouse_y + 10),
                    cv::Scalar(255, 255, 0), 2);
            
            // Draw template area preview when not tracking
            if (!tracking) {
                cv::Rect preview_roi = selectTemplateAtMouse(frame, current_mouse_x, current_mouse_y);
                cv::rectangle(display_frame, preview_roi, cv::Scalar(0, 255, 255), 2);
                cv::putText(display_frame, "Template Preview", 
                           cv::Point(preview_roi.x, preview_roi.y - 5), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 255), 1);
            }
        } else {
            // Show center marker when no mouse
            cv::Point center(frame.cols/2, frame.rows/2);
            cv::circle(display_frame, center, 5, cv::Scalar(0, 0, 255), -1);
            cv::circle(display_frame, center, 40, cv::Scalar(0, 0, 255), 2);
        }
        
        // Handle template selection FIRST (before tracking logic)
        // Handle template selection FIRST (before tracking logic)
        if (should_select_template && !tracking) {
            if (mouse_available) {
                template_roi = selectTemplateAtMouse(frame, current_mouse_x, current_mouse_y);
            } else {
                // Fallback: select center template
                template_roi = selectTemplateAtMouse(frame, frame.cols/2, frame.rows/2);
            }
            
            if (template_roi.width > 20 && template_roi.height > 20) {
                cv::Mat template_img = frame(template_roi);
                
                // Remove the if condition since setTemplate returns void
                tracker.setTemplate(template_img);
                track_point = cv::Point(template_roi.x + template_roi.width / 2, 
                                    template_roi.y + template_roi.height / 2);
                tracking = true;
                std::cout << "Template set! Starting tracking..." << std::endl;
                std::cout << "Template ROI: " << template_roi << std::endl;
            } else {
                std::cerr << "Template ROI too small: " << template_roi << std::endl;
            }
            
            should_select_template = false;
        }
        
        // Handle tracking reset
        if (should_reset_tracking) {
            tracking = false;
            std::cout << "Tracking reset." << std::endl;
            should_reset_tracking = false;
        }
        
        // Perform tracking if active
        if (tracking) {
            auto start = std::chrono::high_resolution_clock::now();
            
            // Create search region around tracked point
            int search_margin = 100;
            cv::Rect search_roi(
                std::max(0, track_point.x - search_margin),
                std::max(0, track_point.y - search_margin),
                std::min(frame.cols - (track_point.x - search_margin), search_margin * 2),
                std::min(frame.rows - (track_point.y - search_margin), search_margin * 2)
            );
            
            if (search_roi.width > 50 && search_roi.height > 50) {
                cv::Mat search_region = frame(search_roi);
                
                if (tracker.track(search_region, track_point, confidence)) {
                    // Convert back to full frame coordinates
                    track_point.x += search_roi.x;
                    track_point.y += search_roi.y;
                    
                    // Draw tracking result
                    cv::circle(display_frame, track_point, 8, cv::Scalar(0, 255, 0), 2);
                    cv::circle(display_frame, track_point, 3, cv::Scalar(0, 255, 0), -1);
                    
                    // Draw search region
                    cv::rectangle(display_frame, search_roi, cv::Scalar(255, 255, 0), 2);
                    
                    // Draw original template location
                    cv::rectangle(display_frame, template_roi, cv::Scalar(0, 255, 255), 1);
                    
                    // Display status
                    std::string status_text = "Tracking: " + std::to_string(confidence).substr(0, 4);
                    cv::putText(display_frame, status_text, 
                               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                               cv::Scalar(0, 255, 0), 2);
                    
                    // Display tracking point coordinates
                    std::string coord_text = "Pos: " + std::to_string(track_point.x) + "," + std::to_string(track_point.y);
                    cv::putText(display_frame, coord_text, 
                               cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 0.5, 
                               cv::Scalar(255, 255, 255), 1);
                    // if (confidence>0.97){
                    //     template_roi = selectTemplateAtMouse(frame, track_point.x, track_point.y);                        
                    //     cv::Mat template_img = frame(template_roi);
                    //     tracker.setTemplate(template_img);
                    // }
                } else {
                    cv::putText(display_frame, "Tracking lost!", cv::Point(10, 30), 
                               cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
                    // Don't reset tracking automatically - let user decide
                }
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            if (duration.count() > 0) {
                std::string fps_text = "FPS: " + std::to_string(1000.0 / duration.count()).substr(0, 4);
                cv::putText(display_frame, fps_text, 
                           cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                           cv::Scalar(255, 255, 255), 2);
            }
        } else {
            // Show instructions
            if (mouse_available) {
                cv::putText(display_frame, "Left click to select template", 
                           cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           cv::Scalar(255, 255, 255), 1);
                cv::putText(display_frame, "Right click to reset, 'q' to quit", 
                           cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           cv::Scalar(255, 255, 255), 1);
            } else {
                cv::putText(display_frame, "Press 's' to select template at center", 
                           cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           cv::Scalar(255, 255, 255), 1);
                cv::putText(display_frame, "Press 'q' to quit, 'r' to reset", 
                           cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           cv::Scalar(255, 255, 255), 1);
            }
        }
        
        // Push frame to framebuffer
        fb.pushFrame(display_frame);
        
        // Small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    should_quit = true;
    
    if (mouse_thread.joinable()) {
        mouse_thread.join();
    }
    if (keyboard_thread.joinable()) {
        keyboard_thread.join();
    }
    
    cap.release();
    fb.stop();
    
    std::cout << "Application terminated." << std::endl;
    return 0;
}