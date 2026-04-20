#include "config_parser.h"

#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdio>

// Remove leading and trailing
// whitespaces (spaces, tabs, \r, \n)
static std::string trim_whitespace(const std::string& string_value) {
    size_t first_char = string_value.find_first_not_of(" \t\r\n");
    if (first_char == std::string::npos) {
        return "";
    }

    size_t last_char = string_value.find_last_not_of(" \t\r\n");
    return string_value.substr(first_char, last_char - first_char +1);
}

// Remove quotes form YAML file
// If the string is not quoted, returns
// it unchanged.
static std::string trim_quotes(const std::string& quoted_value) {
    if (quoted_value.size() >= 2 && 
        ((quoted_value.front() == '"' && quoted_value.back() == '"') ||
         (quoted_value.front() == '\'' && quoted_value.back() == '\''))) {
        return quoted_value.substr(1, quoted_value.size() - 2);
    }
    return quoted_value;
}

// Build yaml key indent
// 2-space, 4-space, tab or mixed all
// produce consistent relative depth values
static int get_indent_width(const std::string& line) {
    int indent_width = 0;
    for (char character : line) {
        if (character == ' ') {
            indent_width += 1;
        }
        else if (character == '\t') {
            indent_width += 2;
        } else {
            break;
        }
    }
    return indent_width;
}

static ServiceConfig global_config;

// Read configs
bool load_config(const char* yaml_path) {
    std::ifstream config_file(yaml_path);
    if (!config_file) {
        std::printf("Error: Connot open: %s\n", yaml_path);
        return false;
    }

    ServiceConfig parsed_config;

    // State Tracking
    std::string active_section;
    std::string active_subsection;
    int section_indent_level = -1;

    // Channel Builder
    ChannelConfig current_channel;
    bool is_parsing_channel = false;

    // Recovery Builder
    RecoveryChannelConfig current_recovery_channel;
    bool is_parsing_recovery_channel = false;

    // Clean Buffer
    // Saves any in-progress list item into
    // the config and resets the builder
    // Used when:
    // - A new list starts ('- ') - save the previous item
    // - A new section starts ("recovery") - save any leftover from realtime
    // - End of line - save the last item
    auto flush_pending_entries = [&]() {
        if (is_parsing_channel) {
            parsed_config.channels.push_back(current_channel);
            current_channel = {};
            is_parsing_channel = false;
        }
        if (is_parsing_recovery_channel) {
            parsed_config.recovery_channels.push_back(current_recovery_channel);
            current_recovery_channel = {};
            is_parsing_recovery_channel = false;
        }
    };

    std::string raw_line;
    while (std::getline(config_file, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }

        int indent_level = get_indent_width(raw_line);

        std::string line_trimmed = trim_whitespace(raw_line);
        if (line_trimmed.empty()) {
            continue;
        }
        if (line_trimmed[0] == '#') {
            continue;
        }

        // Find list item marker
        // '- '
        bool is_list_item = false;
        if (line_trimmed.size() >=2 && line_trimmed[0] == '-' && line_trimmed[1] == ' ') {
            is_list_item = true;
            line_trimmed = trim_whitespace(line_trimmed.substr(2));
            indent_level += 2;
        }

        // Split on first ':' into key and value
        size_t colon_pos = line_trimmed.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string key = trim_whitespace(line_trimmed.substr(0, colon_pos));
        std::string value = trim_quotes(trim_whitespace(line_trimmed.substr(colon_pos + 1)));

        // Top-level key
        // "service:" - skip
        if (indent_level == 0) {
            continue;
        }

        // Section headers
        // realtime, recovery
        if (!is_list_item && value.empty() && (key == "realtime" || key == "recovery")) {
            flush_pending_entries();
            active_section = key;
            section_indent_level = indent_level;
            active_subsection.clear();
            continue;
        }

        // Sub-section header
        // channels
        if (!is_list_item && value.empty() && key == "channels" && indent_level > section_indent_level) {
            flush_pending_entries();
            active_subsection = "channels";
            continue;
        }

        // RealTime Scalar values
        if (active_section == "realtime" && active_subsection.empty() && !value.empty()) {
            if (key == "next_sequence_number") {
                parsed_config.next_sequence_number = std::strtoull(value.c_str(), nullptr, 10);
            } else if (key == "queue_size") {
                parsed_config.queue_size = std::atoi(value.c_str());
            }
            continue;
        }

        // Realtime channel Items
        if (active_section == "realtime" && active_subsection == "channels") {
            if (is_list_item) {
                if (is_parsing_channel) {
                    parsed_config.channels.push_back(current_channel);
                }
                current_channel = {};
                is_parsing_channel = true;
            }

            if (is_parsing_channel) {
                if (key == "multicast_address") {
                    current_channel.multicast_address = value;
                }
                else if (key == "multicast_port") {
                    current_channel.multicast_port = (uint16_t)std::atoi(value.c_str());
                }
                else if (key == "interface") {
                    current_channel.interface = value;
                }
                else if (key == "source") {
                    current_channel.source = value;
                }
            }
            continue;
        }

        // Recovery Channel Items
        if (active_section == "recovery" && active_subsection == "channels") {
            if (is_list_item) {
                if (is_parsing_recovery_channel) {
                    parsed_config.recovery_channels.push_back(current_recovery_channel);
                }

                current_recovery_channel  = {};
                is_parsing_recovery_channel = true;
            }
            if (is_parsing_recovery_channel) {
                if (key == "rerequester_address") {
                    current_recovery_channel.rerequester_address = value;
                }
                else if (key == "rerequester_port") {
                    current_recovery_channel.rerequester_port = (uint16_t)std::atoi(value.c_str());
                }
                else if (key == "max_recovery_message_count") {
                    current_recovery_channel.max_recovery_message_count = (uint16_t)std::atoi(value.c_str());
                }
            }
            continue;
        }
    }

    // Save the last item being
    // built
    flush_pending_entries();

    if (parsed_config.channels.empty()) {
        std::printf("ERROR: No channels defined\n");
        return false;
    }

    global_config = parsed_config;
    return true;
}

const ServiceConfig& config() {
    return global_config;
}
