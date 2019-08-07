#!/usr/bin/env python
import unittest
import dns
import lmdb
import socket
from dnsdisttests import DNSDistTest

class TestLMDB(DNSDistTest):

    _lmdbFileName = '/tmp/test-lmdb-db'
    _lmdbDBName = 'db-name'
    _config_template = """
    newServer{address="127.0.0.1:%d"}

    kvs = newLMDBKVStore('%s', '%s')

    -- KVS lookups follow
    -- does a lookup in the LMDB database using the source IP as key, and store the result into the 'kvs-sourceip-result' tag
    addAction(AllRule(), KeyValueStoreLookupAction(kvs, KeyValueLookupKeySourceIP(), 'kvs-sourceip-result'))

    -- does a lookup in the LMDB database using the qname in wire format as key, and store the result into the 'kvs-qname-result' tag
    addAction(AllRule(), KeyValueStoreLookupAction(kvs, KeyValueLookupKeyQName(), 'kvs-qname-result'))

    -- if the value of the 'kvs-qname-result' tag is set to 'this is the value of the qname tag'
    -- does a lookup in the LMDB database using the value of the 'kvs-qname-result' tag as key, and store the result into the 'kvs-tag-result' tag
    addAction(TagRule('kvs-qname-result', 'this is the value of the qname tag'), KeyValueStoreLookupAction(kvs, KeyValueLookupKeyTag('kvs-qname-result'), 'kvs-tag-result'))

    -- does a lookup in the LMDB database using the source IP as key, and store the result into the 'kvs-sourceip-result' tag
    addAction(AllRule(), KeyValueStoreLookupAction(kvs, KeyValueLookupKeySourceIP(), 'kvs-sourceip-result'))

    -- does a lookup in the LMDB database using the qname in wire format as key, but this time does a suffix lookup, and store the result into the 'kvs-suffix-result' tag
    addAction(AllRule(), KeyValueStoreLookupAction(kvs, KeyValueLookupKeySuffix(), 'kvs-suffix-result'))

    -- Now we take action based on the result of the lookups
    -- if the value of the 'kvs-tag-result' is set to 'this is the value of the second tag', spoof a response
    addAction(TagRule('kvs-tag-result', 'this is the value of the second tag'), SpoofAction('1.2.3.4'))

    -- if the value of the 'kvs-suffix-result' is set to 'this is the value of the suffix tag', spoof a response
    addAction(TagRule('kvs-suffix-result', 'this is the value of the suffix tag'), SpoofAction('42.42.42.42'))

    -- if the value of the 'kvs-sourceip-result' is set to 'this is the value of the source address tag', spoof a response
    addAction(TagRule('kvs-sourceip-result', 'this is the value of the source address tag'), SpoofAction('5.6.7.8'))

    -- otherwise, spoof a different response
    addAction(AllRule(), SpoofAction('9.9.9.9'))
    """
    _config_params = ['_testServerPort', '_lmdbFileName', '_lmdbDBName']

    @classmethod
    def setUpLMDB(cls):
        env = lmdb.open(cls._lmdbFileName, map_size=1014*1024, max_dbs=1024, subdir=False)
        db = env.open_db(key=cls._lmdbDBName.encode())
        with env.begin(db=db, write=True) as txn:
            txn.put(b'\x05qname\x04lmdb\x05tests\x08powerdns\x03com\x00', b'this is the value of the qname tag')
            txn.put(socket.inet_aton('127.0.0.1'), b'this is the value of the source address tag')
            txn.put(b'this is the value of the qname tag', b'this is the value of the second tag')
            txn.put(b'\x06suffix\x04lmdb\x05tests\x08powerdns\x03com\x00', b'this is the value of the suffix tag')

    @classmethod
    def setUpClass(cls):

        cls.setUpLMDB()
        cls.startResponders()
        cls.startDNSDist()
        cls.setUpSockets()

        print("Launching tests..")

    def testLMDBSource(self):
        """
        LMDB: Match on source address
        """
        name = 'source-ip.lmdb.tests.powerdns.com.'
        query = dns.message.make_query(name, 'A', 'IN')
        # dnsdist set RA = RD for spoofed responses
        query.flags &= ~dns.flags.RD
        expectedResponse = dns.message.make_response(query)
        rrset = dns.rrset.from_text(name,
                                    3600,
                                    dns.rdataclass.IN,
                                    dns.rdatatype.A,
                                    '5.6.7.8')
        expectedResponse.answer.append(rrset)

        for method in ("sendUDPQuery", "sendTCPQuery"):
            sender = getattr(self, method)
            (receivedQuery, receivedResponse) = sender(query, response=None, useQueue=False)
            self.assertFalse(receivedQuery)
            self.assertTrue(receivedResponse)
            self.assertEquals(expectedResponse, receivedResponse)

    def testLMDBQNamePlusTagLookup(self):
        """
        LMDB: Match on qname then does a second lookup using the value of the first lookup
        """
        name = 'qname.lmdb.tests.powerdns.com.'
        query = dns.message.make_query(name, 'A', 'IN')
        # dnsdist set RA = RD for spoofed responses
        query.flags &= ~dns.flags.RD
        expectedResponse = dns.message.make_response(query)
        rrset = dns.rrset.from_text(name,
                                    3600,
                                    dns.rdataclass.IN,
                                    dns.rdatatype.A,
                                    '1.2.3.4')
        expectedResponse.answer.append(rrset)

        for method in ("sendUDPQuery", "sendTCPQuery"):
            sender = getattr(self, method)
            (receivedQuery, receivedResponse) = sender(query, response=None, useQueue=False)
            self.assertFalse(receivedQuery)
            self.assertTrue(receivedResponse)
            self.assertEquals(expectedResponse, receivedResponse)

    def testLMDBSuffixLookup(self):
        """
        LMDB: Match on the qname via a suffix lookup
        """
        name = 'sub.sub.suffix.lmdb.tests.powerdns.com.'
        query = dns.message.make_query(name, 'A', 'IN')
        # dnsdist set RA = RD for spoofed responses
        query.flags &= ~dns.flags.RD
        expectedResponse = dns.message.make_response(query)
        rrset = dns.rrset.from_text(name,
                                    3600,
                                    dns.rdataclass.IN,
                                    dns.rdatatype.A,
                                    '42.42.42.42')
        expectedResponse.answer.append(rrset)

        for method in ("sendUDPQuery", "sendTCPQuery"):
            sender = getattr(self, method)
            (receivedQuery, receivedResponse) = sender(query, response=None, useQueue=False)
            self.assertFalse(receivedQuery)
            self.assertTrue(receivedResponse)
            self.assertEquals(expectedResponse, receivedResponse)

