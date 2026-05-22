# tesults-cpp

C++ library for uploading test results to [Tesults](https://www.tesults.com).

## Requirements

- CMake 3.14+
- C++17 compiler
- libcurl
- OpenSSL

### macOS (Homebrew)

```sh
brew install curl openssl
```

On Apple Silicon you may need to hint CMake:
```sh
cmake -DOPENSSL_ROOT_DIR=$(brew --prefix openssl) ..
```

### Linux (apt)

```sh
apt install libcurl4-openssl-dev libssl-dev
```

## Installation via CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    tesults
    GIT_REPOSITORY https://github.com/tesults/tesults-cpp.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tesults)

target_link_libraries(your_target PRIVATE tesults)
```

## Usage

```cpp
#include <tesults/tesults.h>
#include <iostream>

int main() {
    tesults::Case tc;
    tc.name   = "Test 1";
    tc.suite  = "Suite A";
    tc.result = "pass";

    tesults::Data data;
    data.target = "your-target-token";
    data.cases  = {tc};

    std::cout << "Tesults results upload..." << std::endl;
    auto r = tesults::upload(data);
    std::cout << "Success: "  << (r.success ? "true" : "false") << std::endl;
    std::cout << "Message: "  << r.message  << std::endl;
    std::cout << "Warnings: " << r.warnings.size() << std::endl;
    std::cout << "Errors: "   << r.errors.size()   << std::endl;
}
```

## API

### `tesults::upload(data)` → `tesults::Response`

#### `tesults::Case` fields

| Field       | Type                                | Required | Description                        |
|-------------|-------------------------------------|----------|------------------------------------|
| name        | string                              | yes      | Test case name                     |
| result      | string                              | yes      | `"pass"`, `"fail"`, or `"unknown"` |
| suite       | string                              | no       | Suite / group name                 |
| desc        | string                              | no       | Description                        |
| reason      | string                              | no       | Failure reason                     |
| rawResult   | string                              | no       | Raw result from the framework      |
| start       | long long                           | no       | Start time — ms since epoch        |
| end         | long long                           | no       | End time — ms since epoch          |
| files       | vector\<string\>                    | no       | Absolute paths to files to upload  |
| params      | map\<string, string\>               | no       | Parametrised test values           |
| custom      | map\<string, string\>               | no       | Custom fields (stored with `_` prefix) |
| steps       | vector\<Step\>                      | no       | Test steps                         |

#### `tesults::Response` fields

| Field    | Type            | Description                        |
|----------|-----------------|------------------------------------|
| success  | bool            | `true` if upload succeeded         |
| message  | string          | Status message                     |
| warnings | vector\<string\>| File upload warnings               |
| errors   | vector\<string\>| Upload errors                      |

## License

MIT
