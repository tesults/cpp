#include "tesults/tesults.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

namespace tesults {
namespace {

// ── Curl callbacks ────────────────────────────────────────────────────────────

size_t write_cb(void* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct ReadBuffer {
    const char* data;
    size_t      size;
    size_t      offset = 0;
};

size_t read_cb(char* buf, size_t size, size_t nmemb, void* userdata) {
    auto* rb      = static_cast<ReadBuffer*>(userdata);
    size_t avail  = rb->size - rb->offset;
    size_t n      = std::min(size * nmemb, avail);
    std::memcpy(buf, rb->data + rb->offset, n);
    rb->offset += n;
    return n;
}

// ── Crypto ────────────────────────────────────────────────────────────────────

std::string hex_encode(const unsigned char* data, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return ss.str();
}

std::string sha256_hex(const std::string& input) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    EVP_MD_CTX*   ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
    return hex_encode(hash, len);
}

std::string sha256_hex_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return sha256_hex("");
    EVP_MD_CTX*   ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount()));
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
    return hex_encode(hash, len);
}

std::vector<unsigned char> hmac_sha256(const void* key, size_t key_len,
                                        const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    HMAC(EVP_sha256(), key, static_cast<int>(key_len),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &len);
    return {result, result + len};
}

std::vector<unsigned char> hmac_sha256(const std::string& key, const std::string& data) {
    return hmac_sha256(key.data(), key.size(), data);
}

std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                        const std::string& data) {
    return hmac_sha256(key.data(), key.size(), data);
}

// ── Date / time ───────────────────────────────────────────────────────────────

struct AwsDt {
    std::string date;      // YYYYMMDD
    std::string datetime;  // YYYYMMDDTHHmmssZ
};

AwsDt now_utc() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream d, dt;
    d  << std::put_time(&tm, "%Y%m%d");
    dt << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return {d.str(), dt.str()};
}

long long epoch_seconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// ── URL encoding ──────────────────────────────────────────────────────────────

// Percent-encode a string, preserving '/' for S3 key paths.
std::string url_encode_path(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
            out << c;
        else
            out << '%' << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return out.str();
}

// ── HTTP ──────────────────────────────────────────────────────────────────────

const std::string TESULTS_URL = "https://www.tesults.com";

struct HttpResp {
    long        status = 0;
    std::string body;
};

HttpResp http_post(const std::string& url, const std::string& body) {
    HttpResp r;
    CURL* c = curl_easy_init();
    if (!c) return r;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,    body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     &r.body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       60L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return r;
}

// ── AWS S3 upload (SigV4) ─────────────────────────────────────────────────────

struct FileUploadResult {
    size_t      bytes_uploaded = 0;
    std::string warning;        // empty on success
};

const std::string S3_BUCKET = "tesults-results";
const std::string S3_REGION = "us-east-1";
const long long   EXPIRE_BUFFER = 30; // seconds before expiry to refresh

std::string s3_filename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos != std::string::npos ? path.substr(pos + 1) : path;
}

FileUploadResult upload_file_s3(const std::string& local_path, const std::string& s3_key,
                                 const std::string& access_key, const std::string& secret_key,
                                 const std::string& session_token) {
    std::ifstream f(local_path, std::ios::binary | std::ios::ate);
    if (!f) return {0, "File not found: " + local_path};
    auto file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string content(file_size, '\0');
    f.read(content.data(), static_cast<std::streamsize>(file_size));

    const std::string host         = S3_BUCKET + ".s3." + S3_REGION + ".amazonaws.com";
    const std::string encoded_path = "/" + url_encode_path(s3_key);
    const std::string url          = "https://" + host + encoded_path;

    auto        dt           = now_utc();
    std::string payload_hash = sha256_hex(content);

    // Canonical headers — must be in alphabetical order
    std::string canon_hdrs =
        "content-type:application/octet-stream\n"
        "host:" + host + "\n"
        "x-amz-content-sha256:" + payload_hash + "\n"
        "x-amz-date:" + dt.datetime + "\n"
        "x-amz-security-token:" + session_token + "\n";
    std::string signed_hdrs =
        "content-type;host;x-amz-content-sha256;x-amz-date;x-amz-security-token";

    std::string canon_req =
        "PUT\n" + encoded_path + "\n\n" + canon_hdrs + "\n" + signed_hdrs + "\n" + payload_hash;

    std::string cred_scope  = dt.date + "/" + S3_REGION + "/s3/aws4_request";
    std::string str_to_sign =
        "AWS4-HMAC-SHA256\n" + dt.datetime + "\n" + cred_scope + "\n" + sha256_hex(canon_req);

    auto k_date    = hmac_sha256("AWS4" + secret_key, dt.date);
    auto k_region  = hmac_sha256(k_date,    S3_REGION);
    auto k_service = hmac_sha256(k_region,  "s3");
    auto k_signing = hmac_sha256(k_service, "aws4_request");
    auto sig_bytes = hmac_sha256(k_signing, str_to_sign);
    std::string sig = hex_encode(sig_bytes.data(), sig_bytes.size());

    std::string auth =
        "AWS4-HMAC-SHA256 Credential=" + access_key + "/" + cred_scope +
        ", SignedHeaders=" + signed_hdrs + ", Signature=" + sig;

    ReadBuffer  rb{content.data(), content.size()};
    std::string resp_body;
    long        status = 0;

    CURL* c = curl_easy_init();
    if (!c) return {0, "Failed to initialise curl for file upload"};

    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, ("Authorization: "           + auth).c_str());
    hdrs = curl_slist_append(hdrs, ("x-amz-date: "             + dt.datetime).c_str());
    hdrs = curl_slist_append(hdrs, ("x-amz-security-token: "   + session_token).c_str());
    hdrs = curl_slist_append(hdrs, ("x-amz-content-sha256: "   + payload_hash).c_str());
    hdrs = curl_slist_append(hdrs, "Content-Type: application/octet-stream");

    curl_easy_setopt(c, CURLOPT_URL,              url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,       hdrs);
    curl_easy_setopt(c, CURLOPT_UPLOAD,           1L);
    curl_easy_setopt(c, CURLOPT_READFUNCTION,     read_cb);
    curl_easy_setopt(c, CURLOPT_READDATA,         &rb);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file_size));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,    write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,        &resp_body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,          120L);

    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (status < 200 || status >= 300)
        return {0, "Failed to upload " + local_path + " (HTTP " + std::to_string(status) + ")"};
    return {file_size, ""};
}

// ── Credentials ───────────────────────────────────────────────────────────────

struct Creds {
    bool        success    = false;
    bool        permit     = false;
    std::string message;
    std::string key_prefix;
    std::string access_key;
    std::string secret_key;
    std::string session_token;
    long long   expiration = 0;  // Unix seconds
};

Creds parse_creds(const json& upload_obj) {
    Creds c;
    c.success    = true;
    c.permit     = upload_obj.value("permit", false);
    c.message    = upload_obj.value("message", "");
    c.key_prefix = upload_obj.value("key", "");
    if (c.permit && upload_obj.contains("auth")) {
        const auto& auth  = upload_obj["auth"];
        c.access_key      = auth.value("AccessKeyId",     "");
        c.secret_key      = auth.value("SecretAccessKey", "");
        c.session_token   = auth.value("SessionToken",    "");
        c.expiration      = auth.value("Expiration",      0LL);
    }
    return c;
}

Creds refresh_creds(const std::string& target, const std::string& key_prefix) {
    Creds c;
    auto  resp = http_post(TESULTS_URL + "/permitupload",
                            json({{"target", target}, {"key", key_prefix}}).dump());
    if (resp.status != 200) {
        c.message = "Failed to refresh credentials (HTTP " + std::to_string(resp.status) + ")";
        return c;
    }
    try {
        c = parse_creds(json::parse(resp.body)["data"]["upload"]);
    } catch (...) {
        c.message = "Failed to parse credential refresh response";
    }
    return c;
}

// ── JSON serialisation ────────────────────────────────────────────────────────

json step_to_json(const Step& s) {
    json j;
    j["name"]   = s.name;
    j["result"] = s.result.empty() ? "unknown" : s.result;
    if (!s.desc.empty())   j["desc"]   = s.desc;
    if (!s.reason.empty()) j["reason"] = s.reason;
    return j;
}

json case_to_json(const Case& tc) {
    json j;
    j["name"]   = tc.name;
    j["result"] = tc.result.empty() ? "unknown" : tc.result;
    if (!tc.suite.empty())     j["suite"]     = tc.suite;
    if (!tc.desc.empty())      j["desc"]      = tc.desc;
    if (!tc.reason.empty())    j["reason"]    = tc.reason;
    if (!tc.rawResult.empty()) j["rawResult"] = tc.rawResult;
    if (tc.start > 0)          j["start"]     = tc.start;
    if (tc.end > 0)            j["end"]       = tc.end;
    if (!tc.files.empty())     j["files"]     = tc.files;
    if (!tc.params.empty()) {
        json p = json::object();
        for (auto& [k, v] : tc.params) p[k] = v;
        j["params"] = p;
    }
    for (auto& [k, v] : tc.custom)
        j[k.front() == '_' ? k : "_" + k] = v;
    if (!tc.steps.empty()) {
        json steps = json::array();
        for (auto& s : tc.steps) steps.push_back(step_to_json(s));
        j["steps"] = steps;
    }
    return j;
}

json data_to_json(const Data& data) {
    json cases = json::array();
    for (auto& tc : data.cases) cases.push_back(case_to_json(tc));
    json j = {
        {"target",  data.target},
        {"results", {{"cases", cases}}}
    };
    if (!data.integrationName.empty() || !data.testFramework.empty()) {
        json meta = json::object();
        if (!data.integrationName.empty())
            meta["integration_name"] = data.integrationName;
        if (!data.integrationVersion.empty())
            meta["integration_version"] = data.integrationVersion;
        if (!data.testFramework.empty())
            meta["test_framework"] = data.testFramework;
        j["metadata"] = meta;
    }
    return j;
}

// ── File upload orchestration ─────────────────────────────────────────────────

struct UploadFilesResult {
    std::string              message;
    std::vector<std::string> warnings;
};

UploadFilesResult upload_files(const Data& data, Creds creds) {
    UploadFilesResult result;
    if (!creds.permit) {
        if (!creds.message.empty()) result.warnings.push_back(creds.message);
        return result;
    }
    int    idx            = 0;
    int    files_uploaded = 0;
    size_t bytes_uploaded = 0;

    for (auto& tc : data.cases) {
        for (auto& path : tc.files) {
            if (epoch_seconds() + EXPIRE_BUFFER > creds.expiration) {
                creds = refresh_creds(data.target, creds.key_prefix);
                if (!creds.success || !creds.permit) {
                    result.warnings.push_back("Unable to refresh upload credentials");
                    return result;
                }
            }
            std::string s3_key = creds.key_prefix + "/" + std::to_string(idx) +
                                  "/" + s3_filename(path);
            auto r = upload_file_s3(path, s3_key,
                                     creds.access_key, creds.secret_key, creds.session_token);
            if (!r.warning.empty()) {
                result.warnings.push_back(r.warning);
            } else {
                files_uploaded++;
                bytes_uploaded += r.bytes_uploaded;
            }
        }
        ++idx;
    }
    result.message = std::to_string(files_uploaded) + " files uploaded. " +
                     std::to_string(bytes_uploaded) + " bytes uploaded.";
    return result;
}

// ── Error helper ──────────────────────────────────────────────────────────────

Response make_error(const std::string& msg) {
    return {false, msg, {}, {msg}};
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

Response upload(const Data& data) {
    if (data.target.empty()) return make_error("Target token not provided.");

    std::string body;
    try {
        body = data_to_json(data).dump();
    } catch (...) {
        return make_error("Failed to serialise data to JSON.");
    }

    auto resp = http_post(TESULTS_URL + "/results", body);
    if (resp.status == 0) return make_error("Unable to connect to Tesults.");

    json j;
    try {
        j = json::parse(resp.body);
    } catch (...) {
        return make_error("Failed to parse server response.");
    }

    if (resp.status != 200) {
        std::string msg = "Upload failed.";
        try { msg = j["error"]["message"].get<std::string>(); } catch (...) {}
        return make_error(msg);
    }

    std::string msg;
    try { msg = j["data"]["message"].get<std::string>(); } catch (...) {}

    std::vector<std::string> warnings;
    if (j["data"].contains("upload")) {
        auto r = upload_files(data, parse_creds(j["data"]["upload"]));
        if (!r.message.empty()) msg += " " + r.message;
        warnings = std::move(r.warnings);
    }

    return {true, msg, warnings, {}};
}

}  // namespace tesults
