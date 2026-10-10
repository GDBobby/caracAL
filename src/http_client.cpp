#include "http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace caracal {
namespace {

std::string to_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct Fd {
  int fd = -1;
  ~Fd() {
    if (fd >= 0) ::close(fd);
  }
  int release() {
    int x = fd;
    fd = -1;
    return x;
  }
};

int connect_tcp(const std::string& host, int port, int timeout_ms) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (rc != 0) {
    throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));
  }
  int fd = -1;
  for (auto* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(res);
  if (fd < 0) throw std::runtime_error("connect failed: " + host);
  return fd;
}

void ssl_write_all(SSL* ssl, std::string_view data) {
  std::size_t off = 0;
  while (off < data.size()) {
    int n = SSL_write(ssl, data.data() + off, static_cast<int>(data.size() - off));
    if (n <= 0) throw std::runtime_error("ssl write failed");
    off += static_cast<std::size_t>(n);
  }
}

void sock_write_all(int fd, std::string_view data) {
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
    if (n <= 0) throw std::runtime_error("socket write failed");
    off += static_cast<std::size_t>(n);
  }
}

std::string read_some(int fd, SSL* ssl) {
  char buf[4096];
  if (ssl) {
    int n = SSL_read(ssl, buf, sizeof(buf));
    if (n <= 0) return {};
    return {buf, static_cast<std::size_t>(n)};
  }
  ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
  if (n <= 0) return {};
  return {buf, static_cast<std::size_t>(n)};
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

HttpResponse parse_http(std::string raw) {
  auto hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) throw std::runtime_error("http: incomplete headers");
  std::string head = raw.substr(0, hdr_end);
  std::string body = raw.substr(hdr_end + 4);
  std::istringstream hs(head);
  std::string line;
  std::getline(hs, line);
  if (!line.empty() && line.back() == '\r') line.pop_back();
  HttpResponse r;
  {
    std::istringstream sl(line);
    std::string http;
    sl >> http >> r.status;
    std::getline(sl, r.status_text);
    while (!r.status_text.empty() &&
           (r.status_text.front() == ' ' || r.status_text.front() == '\t')) {
      r.status_text.erase(r.status_text.begin());
    }
    if (!r.status_text.empty() && r.status_text.back() == '\r') r.status_text.pop_back();
  }
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto c = line.find(':');
    if (c == std::string::npos) continue;
    std::string k = line.substr(0, c);
    std::string v = line.substr(c + 1);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    r.headers.emplace_back(std::move(k), std::move(v));
  }

  auto te = r.header("transfer-encoding");
  if (te && to_lower(*te).find("chunked") != std::string::npos) {
    std::string decoded;
    std::size_t i = 0;
    while (i < body.size()) {
      auto nl = body.find("\r\n", i);
      if (nl == std::string::npos) break;
      std::string hex = body.substr(i, nl - i);
      int size = 0;
      for (char c : hex) {
        int h = hex_val(c);
        if (h < 0) break;
        size = size * 16 + h;
      }
      i = nl + 2;
      if (size == 0) break;
      if (i + static_cast<std::size_t>(size) > body.size()) break;
      decoded.append(body, i, static_cast<std::size_t>(size));
      i += static_cast<std::size_t>(size);
      if (i + 1 < body.size() && body[i] == '\r' && body[i + 1] == '\n') i += 2;
    }
    r.body = std::move(decoded);
  } else {
    r.body = std::move(body);
  }
  return r;
}

}  // namespace

std::optional<std::string> HttpResponse::header(std::string_view name) const {
  auto all = headers_named(name);
  if (all.empty()) return std::nullopt;
  return all.front();
}

std::vector<std::string> HttpResponse::headers_named(std::string_view name) const {
  std::string want = to_lower(std::string(name));
  std::vector<std::string> out;
  for (const auto& [k, v] : headers) {
    if (to_lower(k) == want) out.push_back(v);
  }
  return out;
}

Url parse_url(std::string_view url) {
  Url u;
  auto se = url.find("://");
  if (se == std::string_view::npos) throw std::runtime_error("invalid url");
  u.scheme = std::string(url.substr(0, se));
  u.tls = (u.scheme == "https" || u.scheme == "wss");
  auto rest = url.substr(se + 3);
  auto slash = rest.find('/');
  std::string hostport = std::string(slash == std::string_view::npos ? rest : rest.substr(0, slash));
  u.path = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
  if (hostport.starts_with('[')) {
    auto br = hostport.find(']');
    u.host = hostport.substr(1, br - 1);
    if (br + 1 < hostport.size() && hostport[br + 1] == ':') {
      u.port = std::stoi(hostport.substr(br + 2));
    }
  } else {
    auto c = hostport.rfind(':');
    if (c != std::string::npos && hostport.find(':') == c) {
      u.host = hostport.substr(0, c);
      u.port = std::stoi(hostport.substr(c + 1));
    } else {
      u.host = hostport;
    }
  }
  if (u.port == 0) u.port = u.tls ? 443 : 80;
  return u;
}

HttpResponse http_request(std::string method, std::string_view url,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          std::string_view body, int timeout_ms) {
  auto u = parse_url(url);
  Fd sock;
  sock.fd = connect_tcp(u.host, u.port, timeout_ms);

  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  if (u.tls) {
    static bool once = [] {
      SSL_library_init();
      SSL_load_error_strings();
      return true;
    }();
    (void)once;
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) throw std::runtime_error("SSL_CTX_new failed");
    SSL_CTX_set_default_verify_paths(ctx);
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock.fd);
    SSL_set_tlsext_host_name(ssl, u.host.c_str());
    SSL_set1_host(ssl, u.host.c_str());
    if (SSL_connect(ssl) != 1) {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      throw std::runtime_error("tls handshake failed: " + u.host);
    }
  }

  std::ostringstream req;
  req << method << " " << u.path << " HTTP/1.1\r\n";
  req << "Host: " << u.host << "\r\n";
  req << "Connection: close\r\n";
  req << "User-Agent: caracAL-cpp/0.3\r\n";
  bool has_len = false;
  for (const auto& [k, v] : headers) {
    if (to_lower(k) == "content-length") has_len = true;
    req << k << ": " << v << "\r\n";
  }
  if (!body.empty() && !has_len) {
    req << "Content-Length: " << body.size() << "\r\n";
  }
  req << "\r\n";
  std::string head = req.str();
  if (ssl) {
    ssl_write_all(ssl, head);
    if (!body.empty()) ssl_write_all(ssl, body);
  } else {
    sock_write_all(sock.fd, head);
    if (!body.empty()) sock_write_all(sock.fd, body);
  }

  std::string raw;
  for (;;) {
    auto chunk = read_some(sock.fd, ssl);
    if (chunk.empty()) break;
    raw += chunk;
  }
  if (ssl) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
  }
  return parse_http(std::move(raw));
}

}  // namespace caracal
