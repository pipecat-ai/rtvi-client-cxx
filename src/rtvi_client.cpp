//
// Copyright (c) 2024, Daily
//

#include "rtvi_client.h"
#include "rtvi_exceptions.h"

#include <curl/curl.h>

using namespace rtvi;

RTVIClient::RTVIClient(
        const RTVIClientOptions& options,
        std::unique_ptr<RTVITransport> transport
)
    : _initialized(false),
      _connected(false),
      _options(options),
      _transport(std::move(transport)) {}

RTVIClient::~RTVIClient() {
    disconnect();
}

void RTVIClient::initialize() {
    if (_initialized) {
        return;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    _transport->initialize();

    _initialized = true;
}

void RTVIClient::connect() {
    if (!_initialized) {
        throw RTVIException("client is not initialized");
    }
    if (_connected) {
        return;
    }

    nlohmann::json response;
    try {
        response = connect_to_endpoint(
                _options.params.endpoints.connect,
                _options.params.request,
                _options.params.headers
        );
    } catch (nlohmann::json::parse_error& ex) {
        throw RTVIException(
                "unable to parse endpoint: " + std::string(ex.what())
        );
    }

    _transport->connect(response);

    _connected = true;
}

void RTVIClient::disconnect() {
    if (!_connected) {
        return;
    }

    _transport->disconnect();

    _connected = false;
}

void RTVIClient::send_action(const nlohmann::json& action) {
    if (!_connected) {
        return;
    }

    _transport->send_action(action);
}

void RTVIClient::send_action(
        const nlohmann::json& action,
        RTVIActionCallback callback
) {
    if (!_connected) {
        return;
    }

    _transport->send_action(action, callback);
}

int32_t RTVIClient::send_user_audio(const int16_t* frames, size_t num_frames) {
    if (!_connected) {
        return 0;
    }

    return _transport->send_user_audio(frames, num_frames);
}

int32_t RTVIClient::read_bot_audio(int16_t* frames, size_t num_frames) {
    if (!_connected) {
        return 0;
    }

    return _transport->read_bot_audio(frames, num_frames);
}

// Private

static size_t write_response_callback(
        void* contents,
        size_t size,
        size_t nmemb,
        std::string* output
) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

nlohmann::json RTVIClient::connect_to_endpoint(
        const std::string& url,
        const nlohmann::json& body,
        const std::vector<std::string>& headers
) const {
    std::string response_body;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw RTVIException("unable to initialize CURL");
    }

    struct curl_slist* curl_headers = nullptr;

    for (const std::string& header: headers) {
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }
    curl_headers =
            curl_slist_append(curl_headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    std::string body_json = body.dump();
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK) {
        throw RTVIException(
                "unable to perform POST request: " +
                std::string(curl_easy_strerror(res))
        );
    }

    long status;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (status != 200) {
        throw RTVIException(
                "unable to perform POST request (status: " +
                std::to_string(status) + "): " + response_body
        );
    }

    curl_easy_cleanup(curl);

    return nlohmann::json::parse(response_body);
}
