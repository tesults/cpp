#pragma once

#include <map>
#include <string>
#include <vector>

namespace tesults {

struct Step {
    std::string name;
    std::string result;   // "pass", "fail", or "unknown"
    std::string desc;
    std::string reason;
};

struct Case {
    std::string name;
    std::string result;       // "pass", "fail", or "unknown"
    std::string suite;
    std::string desc;
    std::string reason;
    std::string rawResult;
    long long   start = 0;    // milliseconds since epoch, 0 = not set
    long long   end   = 0;    // milliseconds since epoch, 0 = not set
    std::vector<std::string>              files;   // absolute paths
    std::map<std::string, std::string>    params;  // parametrised test values
    std::map<std::string, std::string>    custom;  // stored with _ prefix
    std::vector<Step>                     steps;
};

struct Data {
    std::string        target;             // required — target token
    std::vector<Case>  cases;              // required — test cases
    std::string        integrationName;    // optional metadata
    std::string        integrationVersion; // optional metadata
    std::string        testFramework;      // optional metadata
};

struct Response {
    bool                     success = false;
    std::string              message;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

Response upload(const Data& data);

}  // namespace tesults
