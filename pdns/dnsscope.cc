#define __FAVOR_BSD
#include "statbag.hh"
#include "dnspcap.hh"
#include "dnsparser.hh"
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <map>
#include <set>
#include <fstream>
#include <algorithm>
#include "anadns.hh"
#include <boost/program_options.hpp>
#include <boost/foreach.hpp>
#include <boost/logic/tribool.hpp>
#include "namespaces.hh"

namespace po = boost::program_options;
po::variables_map g_vm;

StatBag S;

struct QuestionData
{
  QuestionData() : d_qcount(0), d_answercount(0)
  {
    d_firstquestiontime.tv_sec=0;
  }

  int d_qcount;
  int d_answercount;

  struct pdns_timeval d_firstquestiontime;
};

typedef map<QuestionIdentifier, QuestionData> statmap_t;
statmap_t statmap;

unsigned int liveQuestions()
{
  unsigned int ret=0;
  BOOST_FOREACH(statmap_t::value_type& val, statmap) {
    if(!val.second.d_answercount)
      ret++;
    //    if(val.second.d_qcount > val.second.d_answercount)
    //      ret+= val.second.d_qcount - val.second.d_answercount;
  }
  return ret;
}

struct LiveCounts
{
  unsigned int questions;
  unsigned int answers;
  unsigned int outstanding;

  LiveCounts()
  {
    questions=answers=outstanding=0;
  }

  LiveCounts operator-(const LiveCounts& rhs)
  {
    LiveCounts ret;
    ret.questions = questions - rhs.questions;
    ret.answers = answers - rhs.answers;
    ret.outstanding = outstanding;
    return ret;
  }
};

int main(int argc, char** argv)
try
{
  po::options_description desc("Allowed options"), hidden, alloptions;
  desc.add_options()
    ("help,h", "produce help message")
    ("rd", po::value<bool>(), "If set to true, only process RD packets, to false only non-RD, unset: both")
    ("ipv4", po::value<bool>()->default_value(true), "Process IPv4 packets")
    ("ipv6", po::value<bool>()->default_value(true), "Process IPv6 packets")
    ("write-failures,w", po::value<string>()->default_value(""), "if set, write weird packets to this PCAP file")
    ("verbose,v", "be verbose");
    
  hidden.add_options()
    ("files", po::value<vector<string> >(), "files");

  alloptions.add(desc).add(hidden); 

  po::positional_options_description p;
  p.add("files", -1);

  po::store(po::command_line_parser(argc, argv).options(alloptions).positional(p).run(), g_vm);
  po::notify(g_vm);
 
  vector<string> files;
  if(g_vm.count("files")) 
    files = g_vm["files"].as<vector<string> >(); 
  if(files.size() != 1 || g_vm.count("help")) {
    cerr<<"Syntax: dnsscope filename.pcap"<<endl;
    cout << desc << endl;
    exit(1);
  }

  bool verbose = g_vm.count("verbose");

  bool haveRDFilter=0, rdFilter=0;
  if(g_vm.count("rd")) {
    rdFilter = g_vm["rd"].as<bool>();
    haveRDFilter=1;
    cout<<"Filtering on recursion desired="<<rdFilter<<endl;
  }
  else
    cout<<"Warning, looking at both RD and non-RD traffic!"<<endl;

  bool doIPv4 = g_vm["ipv4"].as<bool>();
  bool doIPv6 = g_vm["ipv6"].as<bool>();

  PcapPacketReader pr(files[0]);
  PcapPacketWriter* pw=0;
  if(!g_vm["write-failures"].as<string>().empty())
    pw=new PcapPacketWriter(g_vm["write-failures"].as<string>(), pr);
 

  int dnserrors=0, bogus=0;
  typedef map<uint32_t,uint32_t> cumul_t;
  cumul_t cumul;
  unsigned int untracked=0, errorresult=0, reallylate=0, nonRDQueries=0, queries=0;
  unsigned int ipv4Packets=0, ipv6Packets=0, fragmented=0;
  unsigned int questions=0, answers=0;

  typedef map<uint16_t,uint32_t> rcodes_t;
  rcodes_t rcodes;

  time_t lowestTime=2000000000, highestTime=0;
  time_t lastsec=0;
  LiveCounts lastcounts;
  typedef vector<pair<time_t, LiveCounts> > pcounts_t;
  pcounts_t pcounts;
  while(pr.getUDPPacket()) {
    if((ntohs(pr.d_udp->uh_dport)==5300 || ntohs(pr.d_udp->uh_sport)==5300 ||
        ntohs(pr.d_udp->uh_dport)==53   || ntohs(pr.d_udp->uh_sport)==53) &&
        pr.d_len > 12) {
      try {
        if((pr.d_ip->ip_v == 4 && !doIPv4) || (pr.d_ip->ip_v == 6 && !doIPv6))
          continue;
	if(pr.d_ip->ip_v == 4) {
	  uint16_t frag = ntohs(pr.d_ip->ip_off);
	  if((frag & IP_MF) || (frag & IP_OFFMASK)) { // more fragments or IS a fragment
	    fragmented++;
	    continue;
	  }
	}
        MOADNSParser mdp((const char*)pr.d_payload, pr.d_len);
        if(haveRDFilter && mdp.d_header.rd != rdFilter)
          continue;

        if(pr.d_ip->ip_v == 4) 
          ++ipv4Packets;
        else
          ++ipv6Packets;
        
	if(pr.d_pheader.ts.tv_sec != lastsec) {
	  LiveCounts lc;
	  if(lastsec) {
	    lc.questions = questions;
	    lc.answers = answers;
	    lc.outstanding = liveQuestions(); 

	    LiveCounts diff = lc - lastcounts;
	    pcounts.push_back(make_pair(pr.d_pheader.ts.tv_sec, diff));

	  }
	  lastsec = pr.d_pheader.ts.tv_sec;
	  lastcounts = lc;
	}

        if(!mdp.d_header.qr) {
          if(!mdp.d_header.rd)
            nonRDQueries++;
          queries++;
        }

        lowestTime=min((time_t)lowestTime,  (time_t)pr.d_pheader.ts.tv_sec);
        highestTime=max((time_t)highestTime, (time_t)pr.d_pheader.ts.tv_sec);

        string name=mdp.d_qname+"|"+DNSRecordContent::NumberToType(mdp.d_qtype);
        
        QuestionIdentifier qi=QuestionIdentifier::create(pr.getSource(), pr.getDest(), mdp);

        if(!mdp.d_header.qr) { // question
          QuestionData& qd=statmap[qi];
          
          if(!qd.d_firstquestiontime.tv_sec)
            qd.d_firstquestiontime=pr.d_pheader.ts;
          qd.d_qcount++;
	  questions++;
        }
        else  {  // NO ERROR or NXDOMAIN
	  answers++;
          QuestionData& qd=statmap[qi];

          if(!qd.d_qcount)
            untracked++;

          qd.d_answercount++;

          if(qd.d_qcount) {
            uint32_t usecs= (pr.d_pheader.ts.tv_sec - qd.d_firstquestiontime.tv_sec) * 1000000 +  
                            (pr.d_pheader.ts.tv_usec - qd.d_firstquestiontime.tv_usec) ;
            //            cout<<"Took: "<<usecs<<"usec\n";
            if(usecs<2049000)
              cumul[usecs]++;
            else
              reallylate++;
            
            if(mdp.d_header.rcode != 0 && mdp.d_header.rcode!=3) 
              errorresult++;
          }

          if(!qd.d_qcount || qd.d_qcount == qd.d_answercount)
            statmap.erase(qi);
         }

        rcodes[mdp.d_header.rcode]++;
      }
      catch(MOADNSException& mde) {
        if(verbose)
	  cout<<"error parsing packet: "<<mde.what()<<endl;
        if(pw)
          pw->write();
        dnserrors++;
        continue;
      }
      catch(std::exception& e) {
        if(verbose)
	  cout<<"error parsing packet: "<<e.what()<<endl;

        if(pw)
          pw->write();
        bogus++;
        continue;
      }
    }
  }
  cout<<"Timespan: "<<(highestTime-lowestTime)/3600.0<<" hours"<<endl;

  cout<<"PCAP contained "<<pr.d_correctpackets<<" correct packets, "<<pr.d_runts<<" runts, "<< pr.d_oversized<<" oversize, "<<
    pr.d_nonetheripudp<<" unknown encaps, "<<dnserrors<<" dns decoding errors, "<<bogus<<" bogus packets"<<endl;
  cout<<"Ignored fragment packets: "<<fragmented<<endl;
  cout<<"DNS IPv4: "<<ipv4Packets<<" packets, IPv6: "<<ipv6Packets<<" packets"<<endl;
  unsigned int unanswered=0;


  //  ofstream openf("openf");
  for(statmap_t::const_iterator i=statmap.begin(); i!=statmap.end(); ++i) {
    if(!i->second.d_answercount) {
      unanswered++;
    }
    //openf<< i->first.d_source.toStringWithPort()<<' ' <<i->first.d_dest.toStringWithPort()<<' '<<i->first.d_id<<' '<<i->first.d_qname <<" " <<i->first.d_qtype<< " "<<i->second.d_qcount <<" " <<i->second.d_answercount<<endl;
  }

  cout<< boost::format("%d (%.02f%% of all) queries did not request recursion") % nonRDQueries % ((nonRDQueries*100.0)/queries) << endl;
  cout<<statmap.size()<<" queries went unanswered, of which "<< statmap.size()-unanswered<<" were answered on exact retransmit"<<endl;
  cout<<untracked<<" responses could not be matched to questions"<<endl;
  cout<<dnserrors<<" responses were unsatisfactory (indefinite, or SERVFAIL)"<<endl;
  cout<<reallylate<<" responses (would be) discarded because older than 2 seconds"<<endl;
#if 0
        ns_r_noerror = 0,       /* No error occurred. */
        ns_r_formerr = 1,       /* Format error. */
        ns_r_servfail = 2,      /* Server failure. */
        ns_r_nxdomain = 3,      /* Name error. */
        ns_r_notimpl = 4,       /* Unimplemented. */
        ns_r_refused = 5,       /* Operation refused. */
#endif

  cout<<"Rcode\tCount\n";
  for(rcodes_t::const_iterator i=rcodes.begin(); i!=rcodes.end(); ++i)
    cout<<i->first<<"\t"<<i->second<<endl;

  uint32_t sum=0;
  //  ofstream stats("stats");
  uint32_t totpackets=0;
  double tottime=0;
  for(cumul_t::const_iterator i=cumul.begin(); i!=cumul.end(); ++i) {
    //    stats<<i->first<<"\t"<<(sum+=i->second)<<"\n";
    totpackets+=i->second;
    tottime+=i->first*i->second;
  }

  typedef map<uint32_t, bool> done_t;
  done_t done;
  done[50];
  done[100];
  done[200];
  done[300];
  done[400];
  done[800];
  done[1000];
  done[2000];
  done[4000];
  done[8000];
  done[32000];
  done[64000];
  done[256000];
  done[1024000];
  done[2048000];

  cout.setf(std::ios::fixed);
  cout.precision(2);
  sum=0;
  
  double lastperc=0, perc=0;
  for(cumul_t::const_iterator i=cumul.begin(); i!=cumul.end(); ++i) {
    sum+=i->second;

    for(done_t::iterator j=done.begin(); j!=done.end(); ++j)
      if(!j->second && i->first > j->first) {
        j->second=true;

        perc=sum*100.0/totpackets;
        if(j->first < 1024)
          cout<< perc <<"% of questions answered within " << j->first << " usec (";
        else
          cout<< perc <<"% of questions answered within " << j->first/1000.0 << " msec (";
        
        cout<<perc-lastperc<<"%)\n";
        lastperc=sum*100.0/totpackets;
      }
  }

  //  ofstream load("load");
  //  BOOST_FOREACH(pcounts_t::value_type& val, pcounts) {
  //    load<<val.first<<'\t'<<val.second.questions<<'\t'<<val.second.answers<<'\t'<<val.second.outstanding<<'\n';
  //    
  //  }
  
  if(totpackets)
    cout<<"Average response time: "<<tottime/totpackets<<" usec"<<endl;
}
catch(std::exception& e)
{
  cerr<<"Fatal: "<<e.what()<<endl;
}
