#include <tesults/tesults.h>
#include <chrono>
#include <iostream>

int main() {
    // Build a list of test cases — mirrors the java-test example.
    std::vector<tesults::Case> cases;

    // Basic pass
    tesults::Case tc1;
    tc1.name   = "Test 1";
    tc1.desc   = "Test 1 description";
    tc1.suite  = "Suite A";
    tc1.result = "pass";
    cases.push_back(tc1);

    // Pass with timing, params, and a custom field
    tesults::Case tc2;
    tc2.name      = "Test 2";
    tc2.desc      = "Test 2 description";
    tc2.suite     = "Suite B";
    tc2.result    = "pass";
    tc2.start     = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count() - 60000;
    tc2.end       = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    tc2.params    = {{"param1", "value1"}, {"param2", "value2"}};
    tc2.custom    = {{"CustomField", "Custom field value"}};
    cases.push_back(tc2);

    // Fail with reason, files, and steps
    tesults::Case tc3;
    tc3.name   = "Test 3";
    tc3.desc   = "Test 3 description";
    tc3.suite  = "Suite A";
    tc3.result = "fail";
    tc3.reason = "Assert failed at line 203 of example.cpp";
    tc3.files  = {
        "/path/to/log.txt",
        "/path/to/screenshot.png"
    };
    tesults::Step step1{"Step 1", "pass", "Step 1 description", ""};
    tesults::Step step2{"Step 2", "fail", "Step 2 description", "Unexpected value"};
    tc3.steps  = {step1, step2};
    cases.push_back(tc3);

    tesults::Data data;
    data.target = "token";  // replace with your target token
    data.cases  = cases;

    std::cout << "Tesults results upload..." << std::endl;
    auto response = tesults::upload(data);
    std::cout << "Success: "  << (response.success ? "true" : "false") << std::endl;
    std::cout << "Message: "  << response.message << std::endl;
    std::cout << "Warnings: " << response.warnings.size() << std::endl;
    std::cout << "Errors: "   << response.errors.size() << std::endl;

    return response.success ? 0 : 1;
}
