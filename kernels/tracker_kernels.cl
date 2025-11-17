// Simple normalized cross-correlation tracker
__kernel void direct_ncc_tracker(
    __global const uchar* template_img,
    __global const uchar* search_region,
    __global float* correlation_map,
    const int template_width,
    const int template_height,
    const int search_width,
    const int search_height,
    const int channels
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= search_width - template_width || y >= search_height - template_height) {
        return;
    }
    
    // Compute means
    float template_mean = 0.0f;
    float search_mean = 0.0f;
    
    for (int ty = 0; ty < template_height; ty++) {
        for (int tx = 0; tx < template_width; tx++) {
            for (int ch = 0; ch < channels; ch++) {
                int template_idx = (ty * template_width + tx) * channels + ch;
                int search_idx = ((y + ty) * search_width + (x + tx)) * channels + ch;
                
                template_mean += convert_float(template_img[template_idx]);
                search_mean += convert_float(search_region[search_idx]);
            }
        }
    }
    
    int total_pixels = template_width * template_height * channels;
    template_mean /= total_pixels;
    search_mean /= total_pixels;
    
    // Compute NCC
    float numerator = 0.0f;
    float template_var = 0.0f;
    float search_var = 0.0f;
    
    for (int ty = 0; ty < template_height; ty++) {
        for (int tx = 0; tx < template_width; tx++) {
            for (int ch = 0; ch < channels; ch++) {
                int template_idx = (ty * template_width + tx) * channels + ch;
                int search_idx = ((y + ty) * search_width + (x + tx)) * channels + ch;
                
                float template_val = convert_float(template_img[template_idx]) - template_mean;
                float search_val = convert_float(search_region[search_idx]) - search_mean;
                
                numerator += template_val * search_val;
                template_var += template_val * template_val;
                search_var += search_val * search_val;
            }
        }
    }
    
    float correlation = 0.0f;
    if (template_var > 1e-6f && search_var > 1e-6f) {
        correlation = numerator / sqrt(template_var * search_var);
        correlation = (correlation + 1.0f) * 0.5f;
    }
    
    correlation_map[y * (search_width - template_width) + x] = correlation;
}