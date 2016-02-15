#include "dolog.hh"
#include "dnsdist-cache.hh"
#include "dnsparser.hh"

DNSDistPacketCache::DNSDistPacketCache(size_t maxEntries, uint32_t maxTTL, uint32_t minTTL): d_maxEntries(maxEntries), d_maxTTL(maxTTL), d_minTTL(minTTL)
{
  pthread_rwlock_init(&d_lock, 0);
  /* we reserve maxEntries + 1 to avoid rehashing from occuring
     when we get to maxEntries, as it means a load factor of 1 */
  d_map.reserve(maxEntries + 1);
}

DNSDistPacketCache::~DNSDistPacketCache()
{
  WriteLock l(&d_lock);
}

bool DNSDistPacketCache::cachedValueMatches(const CacheValue& cachedValue, const DNSName& qname, uint16_t qtype, uint16_t qclass)
{
  if (cachedValue.qname != qname || cachedValue.qtype != qtype || cachedValue.qclass != qclass)
    return false;
  return true;
}

void DNSDistPacketCache::insert(uint32_t key, const DNSName& qname, uint16_t qtype, uint16_t qclass, const char* response, uint16_t responseLen)
{
  if (responseLen == 0)
    return;

  uint32_t minTTL = getMinTTL(response, responseLen);
  if (minTTL > d_maxTTL)
    minTTL = d_maxTTL;

  if (minTTL < d_minTTL)
    return;

  {
    TryReadLock r(&d_lock);
    if (!r.gotIt()) {
      d_deferredInserts++;
      return;
    }
    if (d_map.size() >= d_maxEntries) {
      return;
    }
  }

  const time_t now = time(NULL);
  std::unordered_map<uint32_t,CacheValue>::iterator it;
  bool result;
  time_t newValidity = now + minTTL;
  CacheValue newValue;
  newValue.qname = qname;
  newValue.qtype = qtype;
  newValue.qclass = qclass;
  newValue.len = responseLen;
  newValue.validity = newValidity;
  newValue.added = now;
  newValue.value = std::string(response, responseLen);

  {
    TryWriteLock w(&d_lock);

    if (!w.gotIt()) {
      d_deferredInserts++;
      return;
    }

    tie(it, result) = d_map.insert({key, newValue});

    if (result) {
      return;
    }

    /* in case of collision, don't override the existing entry
       except if it has expired */
    CacheValue& value = it->second;
    bool wasExpired = value.validity <= now;

    if (!wasExpired && !cachedValueMatches(value, qname, qtype, qclass)) {
      d_insertCollisions++;
      return;
    }

    /* if the existing entry had a longer TTD, keep it */
    if (newValidity <= value.validity)
      return;

    value = newValue;
  }
}

bool DNSDistPacketCache::get(const unsigned char* query, uint16_t queryLen, const DNSName& qname, uint16_t qtype, uint16_t qclass, uint16_t consumed, uint16_t queryId, char* response, uint16_t* responseLen, uint32_t* keyOut, bool skipAging)
{
  uint32_t key = getKey(qname, consumed, query, queryLen);
  if (keyOut)
    *keyOut = key;

  time_t now = time(NULL);
  time_t age;
  {
    TryReadLock r(&d_lock);
    if (!r.gotIt()) {
      d_deferredLookups++;
      return false;
    }

    std::unordered_map<uint32_t,CacheValue>::const_iterator it = d_map.find(key);
    if (it == d_map.end()) {
      d_misses++;
      return false;
    }

    const CacheValue& value = it->second;
    if (value.validity < now) {
      d_misses++;
      return false;
    }

    if (*responseLen < value.len) {
      return false;
    }

    /* check for collision */
    if (!cachedValueMatches(value, qname, qtype, qclass)) {
      d_misses++;
      d_lookupCollisions++;
      return false;
    }

    string dnsQName(qname.toDNSString());
    memcpy(response, &queryId, sizeof(queryId));
    memcpy(response + sizeof(queryId), value.value.c_str() + sizeof(queryId), sizeof(dnsheader) - sizeof(queryId));
    memcpy(response + sizeof(dnsheader), dnsQName.c_str(), dnsQName.length());
    memcpy(response + sizeof(dnsheader) + dnsQName.length(), value.value.c_str() + sizeof(dnsheader) + dnsQName.length(), value.value.length() - (sizeof(dnsheader) + dnsQName.length()));
    *responseLen = value.len;
    age = now - value.added;
  }

  if (!skipAging)
    ageDNSPacket(response, *responseLen, age);
  d_hits++;
  return true;
}

void DNSDistPacketCache::purge(size_t upTo)
{
  time_t now = time(NULL);
  WriteLock w(&d_lock);
  if (upTo <= d_map.size())
    return;

  size_t toRemove = d_map.size() - upTo;
  for(auto it = d_map.begin(); toRemove > 0 && it != d_map.end(); ) {
    const CacheValue& value = it->second;

    if (value.validity < now) {
        it = d_map.erase(it);
        --toRemove;
    } else {
      ++it;
    }
  }
}

void DNSDistPacketCache::expunge(const DNSName& name, uint16_t qtype)
{
  WriteLock w(&d_lock);

  for(auto it = d_map.begin(); it != d_map.end(); ) {
    const CacheValue& value = it->second;
    uint16_t cqtype = 0;
    uint16_t cqclass = 0;
    DNSName cqname(value.value.c_str(), value.len, sizeof(dnsheader), false, &cqtype, &cqclass, nullptr);

    if (cqname == name && (qtype == QType::ANY || qtype == cqtype)) {
        it = d_map.erase(it);
    } else {
      ++it;
    }
  }
}

bool DNSDistPacketCache::isFull()
{
    ReadLock r(&d_lock);
    return (d_map.size() >= d_maxEntries);
}

uint32_t DNSDistPacketCache::getMinTTL(const char* packet, uint16_t length)
{
  const struct dnsheader* dh = (const struct dnsheader*) packet;
  uint32_t result = std::numeric_limits<uint32_t>::max();
  vector<uint8_t> content(length - sizeof(dnsheader));
  copy(packet + sizeof(dnsheader), packet + length, content.begin());
  PacketReader pr(content);
  size_t idx = 0;
  DNSName rrname;
  uint16_t qdcount = ntohs(dh->qdcount);
  uint16_t ancount = ntohs(dh->ancount);
  uint16_t nscount = ntohs(dh->nscount);
  uint16_t arcount = ntohs(dh->arcount);
  uint16_t rrtype;
  uint16_t rrclass;
  struct dnsrecordheader ah;

  /* consume qd */
  for(idx = 0; idx < qdcount; idx++) {
    rrname = pr.getName();
    rrtype = pr.get16BitInt();
    rrclass = pr.get16BitInt();
    (void) rrtype;
    (void) rrclass;
  }

  /* consume AN and NS */
  for (idx = 0; idx < ancount + nscount; idx++) {
    rrname = pr.getName();
    pr.getDnsrecordheader(ah);
    pr.d_pos += ah.d_clen;
    if (result > ah.d_ttl)
      result = ah.d_ttl;
  }

  /* consume AR, watch for OPT */
  for (idx = 0; idx < arcount; idx++) {
    rrname = pr.getName();
    pr.getDnsrecordheader(ah);
    pr.d_pos += ah.d_clen;
    if (ah.d_type == QType::OPT) {
      continue;
    }
    if (result > ah.d_ttl)
      result = ah.d_ttl;
  }
  return result;
}

uint32_t DNSDistPacketCache::getKey(const DNSName& qname, uint16_t consumed, const unsigned char* packet, uint16_t packetLen)
{
  uint32_t result = 0;
  /* skip the query ID */
  result = burtle(packet + 2, sizeof(dnsheader) - 2, result);
  string lc(qname.toDNSStringLC());
  result = burtle((const unsigned char*) lc.c_str(), lc.length(), result);
  result = burtle(packet + sizeof(dnsheader) + consumed, packetLen - (sizeof(dnsheader) + consumed), result);
  return result;
}

string DNSDistPacketCache::toString()
{
  ReadLock r(&d_lock);
  return std::to_string(d_map.size()) + "/" + std::to_string(d_maxEntries);
}
