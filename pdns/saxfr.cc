#include "dnsparser.hh"
#include "sstuff.hh"
#include "misc.hh"
#include "dnswriter.hh"
#include "dnsrecords.hh"
#include "statbag.hh"
#include "base32.hh"
#include "dnssecinfra.hh"
#include <boost/foreach.hpp>

StatBag S;

int main(int argc, char** argv)
try
{
  if(argc < 4) {
    cerr<<"Syntax: saxfr IP-address port zone [showflags] [hidesoadetails] [unhash]"<<endl;;
    exit(EXIT_FAILURE);
  }

  bool showflags=false;
  bool hidesoadetails=false;
  bool unhash=false;

  if (argc > 4) {
    for(int i=4; i<argc; i++) {
      if (strcmp(argv[i], "showflags") == 0)
        showflags=true;
      if (strcmp(argv[i], "hidesoadetails") == 0)
        hidesoadetails=true;
      if (strcmp(argv[i], "unhash") == 0)
        unhash=true;
    }
  }

  reportAllTypes();

  vector<uint8_t> packet;
  DNSPacketWriter pw(packet, argv[3], 252);

  Socket sock(InterNetwork, Stream);
  ComboAddress dest(argv[1] + (*argv[1]=='@'), atoi(argv[2]));
  sock.connect(dest);
  uint16_t len;
  len = htons(packet.size());
  if(sock.write((char *) &len, 2) != 2)
    throw PDNSException("tcp write failed");

  sock.writen(string((char*)&*packet.begin(), (char*)&*packet.end()));

  bool isNSEC3 = false;
  int soacount=0;
  vector<pair<string,string> > records;
  set<string> labels;
  map<string,string> hashes;
  NSEC3PARAMRecordContent ns3pr;

  while(soacount<2) {
    if(sock.read((char *) &len, 2) != 2)
      throw PDNSException("tcp read failed");

    len=ntohs(len);
    char *creply = new char[len];
    int n=0;
    int numread;
    while(n<len) {
      numread=sock.read(creply+n, len-n);
      if(numread<0)
        throw PDNSException("tcp read failed");
      n+=numread;
    }

    MOADNSParser mdp(string(creply, len));
    for(MOADNSParser::answers_t::const_iterator i=mdp.d_answers.begin(); i!=mdp.d_answers.end(); ++i) {
      if(i->first.d_type == QType::SOA)
      {
        ++soacount;
      }
      else if (i->first.d_type == QType::NSEC3PARAM) {
          ns3pr = NSEC3PARAMRecordContent(i->first.d_content->getZoneRepresentation());
          isNSEC3 = true;
      }

      ostringstream o;
      o<<"\tIN\t"<<DNSRecordContent::NumberToType(i->first.d_type);
      if(i->first.d_type == QType::RRSIG)
      {
        string zoneRep = i->first.d_content->getZoneRepresentation();
        vector<string> parts;
        stringtok(parts, zoneRep);
        o<<"\t"<<i->first.d_ttl<<"\t"<< parts[0]<<" "<<parts[1]<<" "<<parts[2]<<" "<<parts[3]<<" [expiry] [inception] [keytag] "<<parts[7]<<" ...";
      }
      else if(!showflags && i->first.d_type == QType::NSEC3)
      {
        string zoneRep = i->first.d_content->getZoneRepresentation();
        vector<string> parts;
        stringtok(parts, zoneRep);
        o<<"\t"<<i->first.d_ttl<<"\t"<< parts[0]<<" [flags] "<<parts[2]<<" "<<parts[3]<<" "<<parts[4];
        for(vector<string>::iterator iter = parts.begin()+5; iter != parts.end(); ++iter)
          o<<" "<<*iter;
      }
      else if(i->first.d_type == QType::DNSKEY)
      {
        string zoneRep = i->first.d_content->getZoneRepresentation();
        vector<string> parts;
        stringtok(parts, zoneRep);
        o<<"\t"<<i->first.d_ttl<<"\t"<< parts[0]<<" "<<parts[1]<<" "<<parts[2]<<" ...";
      }
      else if (i->first.d_type == QType::SOA && hidesoadetails)
      {
        string zoneRep = i->first.d_content->getZoneRepresentation();
        vector<string> parts;
        stringtok(parts, zoneRep);
        o<<"\t"<<i->first.d_ttl<<"\t"<<parts[0]<<" "<<parts[1]<<" [serial] "<<parts[3]<<" "<<parts[4]<<" "<<parts[5]<<" "<<parts[6];
      }
      else
      {
        o<<"\t"<<i->first.d_ttl<<"\t"<< i->first.d_content->getZoneRepresentation();
      }

      records.push_back(make_pair(stripDot(i->first.d_label),o.str()));

      string shorter(stripDot(i->first.d_label));
      do {
        labels.insert(shorter);
        if (pdns_iequals(shorter, argv[3]))
          break;
      }while(chopOff(shorter));

    }
    delete[] creply;
  }

  if (isNSEC3 && unhash)
  {
    string hashed;
    BOOST_FOREACH(const string &label, labels) {
      hashed=toBase32Hex(hashQNameWithSalt(ns3pr.d_iterations, ns3pr.d_salt, label));
      hashes.insert(pair<string,string>(hashed, label));
    }
  }

  pair<string,string> record;
  BOOST_FOREACH(record, records) {
    string label=record.first;
    if (isNSEC3 && unhash)
    {
      map<string,string>::iterator i = hashes.find(makeRelative(label, argv[3]));
      if (i != hashes.end())
        label=i->second;
    }
    cout<<label<<"."<<record.second<<endl;
  }

}
catch(std::exception &e)
{
  cerr<<"Fatal: "<<e.what()<<endl;
}
