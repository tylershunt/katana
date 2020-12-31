#ifndef GALOIS_LIBSUPPORT_GALOIS_HTTPCLIENT_H_
#define GALOIS_LIBSUPPORT_GALOIS_HTTPCLIENT_H_

#include <vector>

#include "galois/JSON.h"
#include "galois/Result.h"

namespace galois {

class CurlHandle;

class GALOIS_EXPORT HttpClient {
  std::unique_ptr<CurlHandle> handle_;

  Result<void> UploadCommon(const std::string& data);

  HttpClient(std::unique_ptr<CurlHandle>&& handle);

public:
  HttpClient(const HttpClient& other);
  HttpClient(HttpClient&& other) noexcept;
  HttpClient& operator=(const HttpClient& other);
  HttpClient& operator=(HttpClient&& other) noexcept;
  ~HttpClient();

  static Result<std::unique_ptr<HttpClient>> Make();

  /// Perform an HTTP get request on url and fill buffer with the result on success
  Result<void> Get(const std::string& url, std::vector<char>* response);

  /// Perform an HTTP post request on url and send the contents of buffer
  Result<void> Post(
      const std::string& url, const std::string& data,
      std::vector<char>* response);

  /// Perform an HTTP put request on url and send the contents of buffer
  Result<void> Put(
      const std::string& url, const std::string& data,
      std::vector<char>* response);

  /// Perform an HTTP delete request on url and send the contents of buffer
  Result<void> Delete(const std::string& url, std::vector<char>* response);

  // Use these if the server speaks JSON
  template <typename ResponseType>
  Result<ResponseType> GetJson(const std::string& url) {
    std::vector<char> response;
    auto res = Get(url, &response);
    if (!res) {
      return res.error();
    }
    return JsonParse<ResponseType>(response);
  }

  template <typename ResponseType>
  Result<ResponseType> DeleteJson(const std::string& url) {
    std::vector<char> response;
    auto res = Delete(url, &response);
    if (!res) {
      return res.error();
    }
    return JsonParse<ResponseType>(response);
  }

  template <typename T, typename ResponseType>
  Result<ResponseType> PostJson(const std::string& url, const T& obj) {
    auto json_res = JsonDump(obj);
    if (!json_res) {
      return json_res.error();
    }
    std::vector<char> response;
    auto res = Post(url, std::move(json_res.value()), &response);
    if (!res) {
      return res.error();
    }
    return JsonParse<ResponseType>(response);
  }

  template <typename T, typename ResponseType>
  Result<ResponseType> PutJson(const std::string& url, const T& obj) {
    auto json_res = JsonDump(obj);
    if (!json_res) {
      return json_res.error();
    }
    std::vector<char> response;
    auto res = Put(url, std::move(json_res.value()), &response);
    if (!res) {
      return res.error();
    }
    return JsonParse<ResponseType>(response);
  }

  template <typename ResponseType>
  Result<ResponseType> PutJson(const std::string& url) {
    std::vector<char> response;
    auto res = Put(url, "", &response);
    if (!res) {
      return res.error();
    }
    return JsonParse<ResponseType>(response);
  }
};

}  // namespace galois

#endif
