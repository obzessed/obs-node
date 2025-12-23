#pragma once

/**
 * cron.hpp - Cron Expression Parser
 */

#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <ctime>

namespace experiments {

class CronExpression {
public:
    explicit CronExpression(std::string expression) : expression_(std::move(expression)) {
        if (!Parse()) {
            valid_ = false;
        }
    }
    
    bool IsMatch(const std::chrono::system_clock::time_point& time) const {
        if (!valid_) return false;
        
        std::time_t t = std::chrono::system_clock::to_time_t(time);
        std::tm* tm = std::localtime(&t);
        
        if (!CheckField(minute_, tm->tm_min)) return false;
        if (!CheckField(hour_, tm->tm_hour)) return false;
        if (!CheckField(day_, tm->tm_mday)) return false;
        if (!CheckField(month_, tm->tm_mon + 1)) return false;
        if (!CheckField(day_of_week_, tm->tm_wday)) return false;
        
        return true;
    }
    
    bool IsValid() const { return valid_; }
    const std::string& GetExpression() const { return expression_; }

private:
    struct Field {
        std::vector<int> values;
        int step{1};
        bool all{false};
    };
    
    std::string expression_;
    bool valid_{true};
    Field minute_, hour_, day_, month_, day_of_week_;
    
    bool CheckField(const Field& f, int current) const {
        if (f.all) return (current % f.step) == 0;
        for (int v : f.values) {
            if (v == current) return true;
        }
        return false;
    }
    
    bool Parse() {
        std::istringstream iss(expression_);
        std::string s_min, s_hour, s_day, s_month, s_dow;
        
        if (!(iss >> s_min >> s_hour >> s_day >> s_month >> s_dow)) return false;
        
        if (!ParseField(s_min, minute_, 0, 59)) return false;
        if (!ParseField(s_hour, hour_, 0, 23)) return false;
        if (!ParseField(s_day, day_, 1, 31)) return false;
        if (!ParseField(s_month, month_, 1, 12)) return false;
        if (!ParseField(s_dow, day_of_week_, 0, 6)) return false;
        
        return true;
    }
    
    bool ParseField(const std::string& s, Field& f, int min, int max) {
        if (s == "*") {
            f.all = true;
            return true;
        }
        
        if (s.rfind("*/", 0) == 0) {
            f.all = true;
            try {
                f.step = std::stoi(s.substr(2));
                return f.step > 0;
            } catch (...) { return false; }
        }
        
        std::stringstream ss(s);
        std::string segment;
        while (std::getline(ss, segment, ',')) {
            try {
                int val = std::stoi(segment);
                if (val < min || val > max) return false;
                f.values.push_back(val);
            } catch (...) { return false; }
        }
        
        return !f.values.empty();
    }
};

} // namespace experiments
