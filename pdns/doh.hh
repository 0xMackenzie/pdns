#pragma once
#include "iputils.hh"
#include "libssl.hh"

struct DOHServerConfig;

class DOHResponseMapEntry
{
public:
  DOHResponseMapEntry(const std::string& regex, uint16_t status, const std::string& content, const boost::optional<std::vector<std::pair<std::string, std::string>>>& headers): d_regex(regex), d_customHeaders(headers), d_content(content), d_status(status)
  {
  }

  bool matches(const std::string& path) const
  {
    return d_regex.match(path);
  }

  uint16_t getStatusCode() const
  {
    return d_status;
  }

  const std::string& getContent() const
  {
    return d_content;
  }

  const boost::optional<std::vector<std::pair<std::string, std::string>>>& getHeaders() const
  {
    return d_customHeaders;
  }

private:
  Regex d_regex;
  boost::optional<std::vector<std::pair<std::string, std::string>>> d_customHeaders;
  std::string d_content;
  uint16_t d_status;
};

struct DOHFrontend
{
  DOHFrontend()
  {
    d_rotatingTicketsKey.clear();
  }

  std::shared_ptr<DOHServerConfig> d_dsc{nullptr};
  std::vector<std::pair<std::string, std::string>> d_certKeyPairs;
  std::vector<std::string> d_ocspFiles;
  std::vector<std::shared_ptr<DOHResponseMapEntry>> d_responsesMap;
  std::string d_ciphers;
  std::string d_ciphers13;
  std::string d_serverTokens{"h2o/dnsdist"};
  LibsslTLSVersion d_minTLSVersion{LibsslTLSVersion::TLS10};
#ifdef HAVE_DNS_OVER_HTTPS
  std::unique_ptr<OpenSSLTLSTicketKeysRing> d_ticketKeys{nullptr};
#endif
  std::vector<std::pair<std::string, std::string>> d_customResponseHeaders;
  ComboAddress d_local;

  uint32_t d_idleTimeout{30};             // HTTP idle timeout in seconds
  std::vector<std::string> d_urls;
  std::string d_ticketKeyFile;

  time_t d_ticketsKeyRotationDelay{43200};
  size_t d_maxStoredSessions{20480};
  uint8_t d_numberOfTicketsKeys{5};
  bool d_enableTickets{true};
  bool d_preferServerCiphers{false};

  std::atomic<uint64_t> d_httpconnects{0};   // number of TCP/IP connections established
  std::atomic<uint64_t> d_getqueries{0};     // valid DNS queries received via GET
  std::atomic<uint64_t> d_postqueries{0};    // valid DNS queries received via POST
  std::atomic<uint64_t> d_badrequests{0};     // request could not be converted to dns query
  std::atomic<uint64_t> d_errorresponses{0}; // dnsdist set 'error' on response
  std::atomic<uint64_t> d_redirectresponses{0}; // dnsdist set 'redirect' on response
  std::atomic<uint64_t> d_validresponses{0}; // valid responses sent out

  struct HTTPVersionStats
  {
    std::atomic<uint64_t> d_nbQueries{0}; // valid DNS queries received
    std::atomic<uint64_t> d_nb200Responses{0};
    std::atomic<uint64_t> d_nb400Responses{0};
    std::atomic<uint64_t> d_nb403Responses{0};
    std::atomic<uint64_t> d_nb500Responses{0};
    std::atomic<uint64_t> d_nb502Responses{0};
    std::atomic<uint64_t> d_nbOtherResponses{0};
  };

  HTTPVersionStats d_http1Stats;
  HTTPVersionStats d_http2Stats;


#ifndef HAVE_DNS_OVER_HTTPS
  void setup()
  {
  }

  void reloadCertificates()
  {
  }

  void rotateTicketsKey(time_t now)
  {
  }

  void loadTicketsKeys(const std::string& keyFile)
  {
  }

  void handleTicketsKeyRotation()
  {
  }

#else
  void setup();
  void reloadCertificates();

  void rotateTicketsKey(time_t now);
  void loadTicketsKeys(const std::string& keyFile);
  void handleTicketsKeyRotation();

#endif /* HAVE_DNS_OVER_HTTPS */

  time_t getNextTicketsKeyRotation() const
  {
    return d_ticketsKeyNextRotation;
  }

  size_t getTicketsKeysCount() const
  {
    size_t res = 0;
#ifdef HAVE_DNS_OVER_HTTPS
    if (d_ticketKeys) {
      res = d_ticketKeys->getKeysCount();
    }
#endif /* HAVE_DNS_OVER_HTTPS */
    return res;
  }

private:
  time_t d_ticketsKeyNextRotation{0};
  std::atomic_flag d_rotatingTicketsKey;
};

#ifndef HAVE_DNS_OVER_HTTPS
struct DOHUnit
{
};

#else /* HAVE_DNS_OVER_HTTPS */
#include <unordered_map>

struct st_h2o_req_t;

struct DOHUnit
{
  DOHUnit()
  {
  }
  DOHUnit(const DOHUnit&) = delete;
  DOHUnit& operator=(const DOHUnit&) = delete;

  void get()
  {
    ++d_refcnt;
  }

  void release()
  {
    --d_refcnt;
    if (d_refcnt == 0) {
      delete this;
    }
  }

  std::string query;
  std::string response;
  ComboAddress remote;
  ComboAddress dest;
  st_h2o_req_t* req{nullptr};
  DOHUnit** self{nullptr};
  std::string contentType;
  std::atomic<uint64_t> d_refcnt{1};
  int rsock;
  uint16_t qtype;
  /* the status_code is set from
     processDOHQuery() (which is executed in
     the DOH client thread) so that the correct
     response can be sent in on_dnsdist(),
     after the DOHUnit has been passed back to
     the main DoH thread.
  */
  uint16_t status_code{200};
  bool ednsAdded{false};

  std::string getHTTPPath() const;
  std::string getHTTPHost() const;
  std::string getHTTPScheme() const;
  std::string getHTTPQueryString() const;
  std::unordered_map<std::string, std::string> getHTTPHeaders() const;
  void setHTTPResponse(uint16_t statusCode, const std::string& body, const std::string& contentType="");
};

#endif /* HAVE_DNS_OVER_HTTPS  */

void handleDOHTimeout(DOHUnit* oldDU);
