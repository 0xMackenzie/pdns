
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_NO_MAIN

#include <boost/test/unit_test.hpp>

#include "dnsdist-kvs.hh"

BOOST_AUTO_TEST_SUITE(dnsdistkvs_cc)

#ifdef HAVE_LMDB
BOOST_AUTO_TEST_CASE(test_LMDB) {

  auto lmdb = make_unique<LMDBKVStore>("/data/Dumps/lmdb", "db-name");
  auto key = make_unique<KeyValueLookupKeySourceIP>();

  DNSName qname("powerdns.com.");
  uint16_t qtype = QType::A;
  uint16_t qclass = QClass::IN;
  ComboAddress lc("127.0.0.1:53");
  ComboAddress rem("127.0.0.1:42");
  struct dnsheader dh;
  memset(&dh, 0, sizeof(dh));
  size_t bufferSize = 0;
  size_t queryLen = 0;
  bool isTcp = false;
  struct timespec queryRealTime;
  gettime(&queryRealTime, true);
  struct timespec expiredTime;
  /* the internal QPS limiter does not use the real time */
  gettime(&expiredTime);

  DNSQuestion dq(&qname, qtype, qclass, qname.wirelength(), &lc, &rem, &dh, bufferSize, queryLen, isTcp, &queryRealTime);

  std::string value;
  DTime dt;
  dt.set();
  for (size_t idx = 0; idx < 10000000; idx++) {
    value.clear();
    BOOST_CHECK_EQUAL(lmdb->getValue(key->getKey(dq), value), true);
    BOOST_CHECK_EQUAL(value, "this is the value of the tag");
  }
  cerr<<dt.udiff()/1000/1000<<endl;
}
#endif /* HAVE_LMDB */

#ifdef HAVE_CDB
BOOST_AUTO_TEST_CASE(test_CDB) {

  DNSName qname("powerdns.com.");
  uint16_t qtype = QType::A;
  uint16_t qclass = QClass::IN;
  ComboAddress lc("127.0.0.1:53");
  ComboAddress rem("127.0.0.1:42");
  struct dnsheader dh;
  memset(&dh, 0, sizeof(dh));
  size_t bufferSize = 0;
  size_t queryLen = 0;
  bool isTcp = false;
  struct timespec queryRealTime;
  gettime(&queryRealTime, true);
  struct timespec expiredTime;
  /* the internal QPS limiter does not use the real time */
  gettime(&expiredTime);

  DNSQuestion dq(&qname, qtype, qclass, qname.wirelength(), &lc, &rem, &dh, bufferSize, queryLen, isTcp, &queryRealTime);

  char db[] = "/tmp/test_cdb.XXXXXX";
  int fd = mkstemp(db);
  BOOST_REQUIRE(fd >= 0);
  CDBWriter writer(fd);
  BOOST_REQUIRE(writer.addEntry(std::string(reinterpret_cast<const char*>(&dq.remote->sin4.sin_addr.s_addr), sizeof(dq.remote->sin4.sin_addr.s_addr)), "this is the value of the tag"));
  writer.close();

  auto cdb = make_unique<CDBKVStore>(db);
  auto key = make_unique<KeyValueLookupKeySourceIP>();

  std::string value;
  DTime dt;
  dt.set();
  for (size_t idx = 0; idx < 10000000; idx++) {
    BOOST_CHECK_EQUAL(cdb->getValue(key->getKey(dq), value), true);
    BOOST_CHECK_EQUAL(value, "this is the value of the tag");
  }
  cerr<<dt.udiff()/1000/1000<<endl;
}
#endif /* HAVE_CDB */

BOOST_AUTO_TEST_SUITE_END()
