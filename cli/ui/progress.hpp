/**
 * @file progress.hpp
 * @brief Progress bar for transfer visualization
 */

#ifndef TELEPORT_PROGRESS_HPP
#define TELEPORT_PROGRESS_HPP

#include "teleport/teleport.h"
#include "console.hpp"
#include <string>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace teleport {
namespace cli {

/**
 * @brief Progress bar renderer
 */
class ProgressBar {
public:
    ProgressBar(int width = 40) : m_width(width), m_last_update(0) {}
    
    /**
     * @brief Update and render progress
     */
    void update(const TeleportProgress* progress) {
        if (!progress) return;
        
        // Throttle updates to avoid flickering
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        
        if (elapsed - m_last_update < 100) return;  // Max 10 updates/sec
        m_last_update = elapsed;
        
        double percent = progress->total_bytes_total > 0 
            ? (static_cast<double>(progress->total_bytes_transferred) / 
               progress->total_bytes_total) * 100.0
            : 0.0;
        
        int filled = static_cast<int>((percent / 100.0) * m_width);
        
        // Format speed
        char speed_str[32];
        teleport_format_bytes(static_cast<uint64_t>(progress->speed_bytes_per_sec), 
                             speed_str, sizeof(speed_str));
        
        // Format ETA
        char eta_str[32];
        teleport_format_duration(progress->eta_seconds, eta_str, sizeof(eta_str));
        
        // Format transferred/total
        char transferred_str[32], total_str[32];
        teleport_format_bytes(progress->total_bytes_transferred, transferred_str, sizeof(transferred_str));
        teleport_format_bytes(progress->total_bytes_total, total_str, sizeof(total_str));
        
        // Build progress bar
        std::ostringstream bar;
        bar << "[";
        for (int i = 0; i < m_width; ++i) {
            if (i < filled) {
                bar << "█";
            } else if (i == filled) {
                bar << "▓";
            } else {
                bar << "░";
            }
        }
        bar << "]";
        
        // Clear line and print
        clear_line();
        std::cout << color::cyan << bar.str() << color::reset
                  << " " << std::fixed << std::setprecision(1) << percent << "%"
                  << "  " << color::green << transferred_str << color::reset
                  << "/" << total_str
                  << "  " << color::yellow << speed_str << "/s" << color::reset
                  << "  ETA: " << eta_str
                  << std::flush;
    }
    
    /**
     * @brief Mark complete
     */
    void complete() {
        clear_line();
        std::cout << color::green << "Transfer complete!" << color::reset << std::endl;
    }
    
    /**
     * @brief Mark failed
     */
    void failed(const std::string& error) {
        clear_line();
        std::cout << color::red << "Transfer failed: " << error << color::reset << std::endl;
    }
    
private:
    int m_width;
    int64_t m_last_update;
};

} // namespace cli
} // namespace teleport

#endif // TELEPORT_PROGRESS_HPP
