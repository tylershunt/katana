#include "galois/HttpClient.h"

#include <curl/curl.h>

#include "galois/ErrorCode.h"
#include "galois/Logging.h"
#include "galois/Result.h"

class galois::CurlHandle {
  CURL* handle_{};
  struct curl_slist* headers_{};

  CurlHandle(CURL* handle) : handle_(handle) {}

  static size_t WriteDataToVectorCB(
      char* ptr, size_t size, size_t nmemb, void* user_data) {
    size_t real_size = size * nmemb;
    auto buffer = static_cast<std::vector<char>*>(user_data);
    buffer->insert(
        buffer->end(), ptr, ptr + real_size);  // NOLINT (c interface)
    return real_size;
  };

public:
  CurlHandle(const CurlHandle& no_copy) = delete;
  CurlHandle& operator=(const CurlHandle& no_copy) = delete;
  CurlHandle(CurlHandle&& no_move) noexcept = delete;
  CurlHandle& operator=(CurlHandle&& no_move) noexcept = delete;

  static galois::Result<std::unique_ptr<CurlHandle>> Make() {
    CURL* curl = curl_easy_init();
    if (!curl) {
      return galois::ErrorCode::HttpError;
    }
    return std::unique_ptr<CurlHandle>(new CurlHandle(curl));
  }

  Result<void> Reset(const std::string& url, std::vector<char>* response) {
    // Cookies are not cleared on reset
    curl_easy_reset(handle_);
    if (auto res = SetOpt(CURLOPT_URL, url.c_str()); !res) {
      return res.error();
    }
    if (auto res = SetOpt(CURLOPT_WRITEDATA, response); !res) {
      return res.error();
    }
    if (auto res = SetOpt(CURLOPT_WRITEFUNCTION, WriteDataToVectorCB); !res) {
      return res.error();
    }

    // common practice to pass an empty file to make sure the cookie engine
    // is enabled (also does not clear cookies when set)
    // https://curl.se/libcurl/c/CURLOPT_COOKIEFILE.html
    if (auto res = SetOpt(CURLOPT_COOKIEFILE, ""); !res) {
      return res.error();
    }

    return galois::ResultSuccess();
  }

  ~CurlHandle() {
    if (headers_ != nullptr) {
      curl_slist_free_all(headers_);
    }
    if (handle_ != nullptr) {
      curl_easy_cleanup(handle_);
    }
  }

  void SetHeader(const std::string& header) {
    headers_ = curl_slist_append(headers_, header.c_str());
  }

  template <typename T>
  galois::Result<void> SetOpt(CURLoption option, T param) {
    if (auto err = curl_easy_setopt(handle_, option, param); err != CURLE_OK) {
      GALOIS_LOG_ERROR("CURL error: {}", curl_easy_strerror(err));
      return galois::ErrorCode::InvalidArgument;
    }
    return galois::ResultSuccess();
  }

  galois::Result<void> Perform() {
    if (headers_ != nullptr) {
      if (auto res = SetOpt(CURLOPT_HTTPHEADER, headers_); !res) {
        return res.error();
      }
    }
    CURLcode request_res = curl_easy_perform(handle_);
    if (request_res != CURLE_OK) {
      GALOIS_LOG_ERROR("CURL error: {}", curl_easy_strerror(request_res));
      return galois::ErrorCode::HttpError;
    }

    int64_t response_code;
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response_code);
    switch (response_code) {
    case 200:
      return galois::ResultSuccess();
    case 404:
      return galois::ErrorCode::NotFound;
    case 400:
      return galois::ErrorCode::HttpError;
    case 409:
      return galois::ErrorCode::AlreadyExists;
    default:
      GALOIS_LOG_ERROR(
          "HTTP request returned unhandled code: {}", response_code);
      return galois::ErrorCode::HttpError;
    }
  }
};

galois::HttpClient::HttpClient(std::unique_ptr<CurlHandle>&& handle)
    : handle_(std::move(handle)) {
  handle_->SetHeader("Content-Type: application/json");
  handle_->SetHeader("Accept: application/json");
}

galois::Result<void>
galois::HttpClient::UploadCommon(const std::string& data) {
  if (auto res = handle_->SetOpt(CURLOPT_POSTFIELDS, data.c_str()); !res) {
    return res.error();
  }
  if (auto res = handle_->SetOpt(CURLOPT_POSTFIELDSIZE, data.size()); !res) {
    return res.error();
  }
  return handle_->Perform();
}

galois::Result<void>
galois::HttpClient::Get(const std::string& url, std::vector<char>* response) {
  if (auto res = handle_->Reset(url, response); !res) {
    return res.error();
  }
  if (auto res = handle_->SetOpt(CURLOPT_HTTPGET, 1L); !res) {
    return res.error();
  }
  if (auto res = handle_->Perform(); !res) {
    GALOIS_LOG_DEBUG("GET failed for url: {}", url);
    return res;
  }
  return galois::ResultSuccess();
}

galois::Result<void>
galois::HttpClient::Post(
    const std::string& url, const std::string& data,
    std::vector<char>* response) {
  if (auto res = handle_->Reset(url, response); !res) {
    return res.error();
  }
  if (auto res = UploadCommon(data); !res) {
    GALOIS_LOG_DEBUG("POST failed for url: {}", url);
    return res.error();
  }
  return galois::ResultSuccess();
}

galois::Result<void>
galois::HttpClient::Delete(
    const std::string& url, std::vector<char>* response) {
  if (auto res = handle_->Reset(url, response); !res) {
    return res.error();
  }
  if (auto res = handle_->SetOpt(CURLOPT_CUSTOMREQUEST, "DELETE"); !res) {
    return res.error();
  }
  if (auto res = handle_->Perform(); !res) {
    GALOIS_LOG_DEBUG("DELETE failed for url: {}", url);
    return res;
  }
  return galois::ResultSuccess();
}

galois::Result<void>
galois::HttpClient::Put(
    const std::string& url, const std::string& data,
    std::vector<char>* response) {
  if (auto res = handle_->Reset(url, response); !res) {
    return res.error();
  }
  if (auto res = handle_->SetOpt(CURLOPT_CUSTOMREQUEST, "PUT"); !res) {
    return res.error();
  }
  if (auto res = UploadCommon(data); !res) {
    GALOIS_LOG_DEBUG("PUT failed for url: {}", url);
    return res.error();
  }
  return galois::ResultSuccess();
}

galois::Result<std::unique_ptr<galois::HttpClient>>
galois::HttpClient::Make() {
  // libcurl doesn't care how many times it's initialized
  auto init_ret = curl_global_init(CURL_GLOBAL_ALL);
  if (init_ret != CURLE_OK) {
    GALOIS_LOG_ERROR(
        "libcurl initialization failed: {}", curl_easy_strerror(init_ret));
    return ErrorCode::HttpError;
  }
  auto handle_res = CurlHandle::Make();
  if (!handle_res) {
    return handle_res.error();
  }
  return std::unique_ptr<HttpClient>(
      new HttpClient(std::move(handle_res.value())));
}

galois::HttpClient::HttpClient(HttpClient&& other) noexcept = default;
galois::HttpClient& galois::HttpClient::operator=(HttpClient&& other) noexcept =
    default;
galois::HttpClient::~HttpClient() = default;
