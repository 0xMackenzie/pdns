#include "dnsdist.hh"
#include <thread>
#include "dolog.hh"
#include "sodcrypto.hh"
#include "base64.hh"
#include <fstream>

using std::thread;

static vector<std::function<void(void)>>* g_launchWork;

vector<std::function<void(void)>> setupLua(bool client)
{
  g_launchWork= new vector<std::function<void(void)>>();
  g_lua.writeFunction("newServer", 
		      [client](boost::variant<string,std::unordered_map<std::string, std::string>> pvars, boost::optional<int> qps)
		      { 
			if(client) {
			  return shared_ptr<DownstreamState>();
			}
			if(auto address = boost::get<string>(&pvars)) {
			  auto ret=std::make_shared<DownstreamState>(ComboAddress(*address, 53));

			  if(qps) {
			    ret->qps=QPSLimiter(*qps, *qps);
			  }
			  g_dstates.modify([ret](servers_t& servers) { 
			      servers.push_back(ret); 
			      std::stable_sort(servers.begin(), servers.end(), [](const decltype(ret)& a, const decltype(ret)& b) {
				  return a->order < b->order;
				});

			    });

			  if(g_launchWork) {
			    g_launchWork->push_back([ret]() {
				ret->tid = move(thread(responderThread, ret));
			      });
			  }
			  else {
			    ret->tid = move(thread(responderThread, ret));
			  }

			  return ret;
			}
			auto vars=boost::get<std::unordered_map<std::string, std::string>>(pvars);
			auto ret=std::make_shared<DownstreamState>(ComboAddress(vars["address"], 53));
			
			if(vars.count("qps")) {
			  ret->qps=QPSLimiter(boost::lexical_cast<int>(vars["qps"]),boost::lexical_cast<int>(vars["qps"]));
			}

			if(vars.count("pool")) {
			  ret->pools.insert(vars["pool"]);
			}

			if(vars.count("order")) {
			  ret->order=boost::lexical_cast<int>(vars["order"]);
			}

			if(vars.count("weight")) {
			  ret->weight=boost::lexical_cast<int>(vars["weight"]);
			}

			if(g_launchWork) {
			  g_launchWork->push_back([ret]() {
			      ret->tid = move(thread(responderThread, ret));
			    });
			}
			else {
			  ret->tid = move(thread(responderThread, ret));
			}

			auto states = g_dstates.getCopy();
			states->push_back(ret);
			std::stable_sort(states->begin(), states->end(), [](const decltype(ret)& a, const decltype(ret)& b) {
			    return a->order < b->order;
			  });
			g_dstates.setState(states);
			return ret;
		      } );




  g_lua.writeFunction("rmServer", 
		      [](boost::variant<std::shared_ptr<DownstreamState>, int> var)
		      { 
			auto states = g_dstates.getCopy();
			if(auto* rem = boost::get<shared_ptr<DownstreamState>>(&var))
			  states->erase(remove(states->begin(), states->end(), *rem), states->end());
			else
			  states->erase(states->begin() + boost::get<int>(var));
			g_dstates.setState(states);
		      } );


  g_lua.writeFunction("setServerPolicy", [](ServerPolicy policy)  {
      g_policy.setState(std::make_shared<ServerPolicy>(policy));
    });

  g_lua.writeFunction("setServerPolicyLua", [](string name, policy_t policy)  {
      g_policy.setState(std::make_shared<ServerPolicy>(ServerPolicy{name, policy}));
    });

  g_lua.writeFunction("showServerPolicy", []() {
      g_outputBuffer=g_policy.getLocal()->name+"\n";
    });


  g_lua.registerMember("name", &ServerPolicy::name);
  g_lua.registerMember("policy", &ServerPolicy::policy);
  g_lua.writeFunction("newServerPolicy", [](string name, policy_t policy) { return ServerPolicy{name, policy};});
  g_lua.writeVariable("firstAvailable", ServerPolicy{"firstAvailable", firstAvailable});
  g_lua.writeVariable("roundrobin", ServerPolicy{"roundrobin", roundrobin});
  g_lua.writeVariable("wrandom", ServerPolicy{"wrandom", wrandom});
  g_lua.writeVariable("leastOutstanding", ServerPolicy{"leastOutstanding", leastOutstanding});
  g_lua.writeFunction("addACL", [](const std::string& domain) {
      auto state=g_ACL.getCopy();
      state->addMask(domain);
      g_ACL.setState(state);
    });

  g_lua.writeFunction("addLocal", [client](const std::string& addr) {
      if(client)
	return;
      try {
	ComboAddress loc(addr, 53);
	g_locals.push_back(loc); /// only works pre-startup, so no sync necessary
      }
      catch(std::exception& e) {
	g_outputBuffer="Error: "+string(e.what())+"\n";
      }
    });
  g_lua.writeFunction("setACL", [](const vector<pair<int, string>>& parts) {
      auto nmg = std::make_shared<NetmaskGroup>();
      for(const auto& p : parts) {
	nmg->addMask(p.second);
      }
      g_ACL.setState(nmg);
  });
  g_lua.writeFunction("showACL", []() {
      vector<string> vec;

      g_ACL.getCopy()->toStringVector(&vec);

      for(const auto& s : vec)
        g_outputBuffer+=s+"\n";

    });
  g_lua.writeFunction("shutdown", []() { _exit(0);} );


  g_lua.writeFunction("addDomainBlock", [](const std::string& domain) { 
      g_suffixMatchNodeFilter.modify([domain](SuffixMatchNode& smn) {
	  smn.add(DNSName(domain)); 
	});
    });
  g_lua.writeFunction("showServers", []() {  
      try {
      ostringstream ret;
      
      boost::format fmt("%1$-3d %2% %|30t|%3$5s %|36t|%4$7.1f %|41t|%5$7d %|44t|%6$3d %|53t|%7$2d %|55t|%8$10d %|61t|%9$7d %|76t|%10$5.1f %|84t|%11$5.1f %12%" );
      //             1        2          3       4        5       6       7       8           9        10        11
      ret << (fmt % "#" % "Address" % "State" % "Qps" % "Qlim" % "Ord" % "Wt" % "Queries" % "Drops" % "Drate" % "Lat" % "Pools") << endl;

      uint64_t totQPS{0}, totQueries{0}, totDrops{0};
      int counter=0;
      auto states = g_dstates.getCopy();
      for(const auto& s : *states) {
	string status;
	if(s->availability == DownstreamState::Availability::Up) 
	  status = "UP";
	else if(s->availability == DownstreamState::Availability::Down) 
	  status = "DOWN";
	else 
	  status = (s->upStatus ? "up" : "down");

	string pools;
	for(auto& p : s->pools) {
	  if(!pools.empty())
	    pools+=" ";
	  pools+=p;
	}

	ret << (fmt % counter % s->remote.toStringWithPort() % 
		status % 
		s->queryLoad % s->qps.getRate() % s->order % s->weight % s->queries.load() % s->reuseds.load() % (s->dropRate) % (s->latencyUsec/1000.0) % pools) << endl;

	totQPS += s->queryLoad;
	totQueries += s->queries.load();
	totDrops += s->reuseds.load();
	++counter;
      }
      ret<< (fmt % "All" % "" % "" 
		% 
	     (double)totQPS % "" % "" % "" % totQueries % totDrops % "" % "" % "" ) << endl;

      g_outputBuffer=ret.str();
      }catch(std::exception& e) { g_outputBuffer=e.what(); throw; }
    });

  g_lua.writeFunction("addPoolRule", [](boost::variant<string,vector<pair<int, string>> > var, string pool) {
      SuffixMatchNode smn;
      NetmaskGroup nmg;

      auto add=[&](string src) {
	try {
	  smn.add(DNSName(src));
	} catch(...) {
	  nmg.addMask(src);
	}
      };
      if(auto src = boost::get<string>(&var))
	add(*src);
      else {
	for(auto& a : boost::get<vector<pair<int, string>>>(var)) {
	  add(a.second);
	}
      }
      if(nmg.empty())
	g_poolrules.modify([smn, pool](decltype(g_poolrules)::value_type& poolrules) {
	    poolrules.push_back({smn, pool});
	  });
      else
	g_poolrules.modify([nmg,pool](decltype(g_poolrules)::value_type& poolrules) {
	  poolrules.push_back({nmg, pool}); 
	  });

    });

  g_lua.writeFunction("showPoolRules", []() {
      boost::format fmt("%-3d %-50s %s\n");
      g_outputBuffer += (fmt % "#" % "Object" % "Pool").str();
      int num=0;
      for(const auto& lim : *g_poolrules.getCopy()) {  
	string name;
	if(auto nmg=boost::get<NetmaskGroup>(&lim.first)) {
	  name=nmg->toString();
	}
	else if(auto smn=boost::get<SuffixMatchNode>(&lim.first)) {
	  name=smn->toString(); 
	}
	g_outputBuffer += (fmt % num % name % lim.second).str();
	++num;
      }
    });


  g_lua.writeFunction("addQPSLimit", [](boost::variant<string,vector<pair<int, string>> > var, int lim) {
      SuffixMatchNode smn;
      NetmaskGroup nmg;

      auto add=[&](string src) {
	try {
	  smn.add(DNSName(src));
	} catch(...) {
	  nmg.addMask(src);
	}
      };
      if(auto src = boost::get<string>(&var))
	add(*src);
      else {
	for(auto& a : boost::get<vector<pair<int, string>>>(var)) {
	  add(a.second);
	}
      }
      if(nmg.empty())
	g_limiters.modify([smn, lim](decltype(g_limiters)::value_type& limiters) {
	    limiters.push_back({smn, QPSLimiter(lim, lim)});
	  });
      else
	g_limiters.modify([nmg, lim](decltype(g_limiters)::value_type& limiters) {
	    limiters.push_back({nmg, QPSLimiter(lim, lim)}); 
	  });
    });

  g_lua.writeFunction("rmQPSLimit", [](int i) {
      g_limiters.modify([i](decltype(g_limiters)::value_type& limiters) { 
	  limiters.erase(limiters.begin() + i);
	});
    });

  g_lua.writeFunction("showQPSLimits", []() {
      boost::format fmt("%-3d %-50s %7d %8d %8d\n");
      g_outputBuffer += (fmt % "#" % "Object" % "Lim" % "Passed" % "Blocked").str();
      int num=0;
      for(const auto& lim : *g_limiters.getCopy()) {  
	string name;
	if(auto nmg=boost::get<NetmaskGroup>(&lim.first)) {
	  name=nmg->toString();
	}
	else if(auto smn=boost::get<SuffixMatchNode>(&lim.first)) {
	  name=smn->toString(); 
	}
	g_outputBuffer += (fmt % num % name % lim.second.getRate() % lim.second.getPassed() % lim.second.getBlocked()).str();
	++num;
      }
    });


  g_lua.writeFunction("getServers", []() {
      vector<pair<int, std::shared_ptr<DownstreamState> > > ret;
      int count=1;
      for(const auto& s : *g_dstates.getCopy()) {
	ret.push_back(make_pair(count++, s));
      }
      return ret;
    });

  g_lua.writeFunction("getServer", [](int i) { return (*g_dstates.getCopy())[i]; });

  g_lua.registerFunction<void(DownstreamState::*)(int)>("setQPS", [](DownstreamState& s, int lim) { s.qps = lim ? QPSLimiter(lim, lim) : QPSLimiter(); });
  g_lua.registerFunction<void(DownstreamState::*)(string)>("addPool", [](DownstreamState& s, string pool) { s.pools.insert(pool);});
  g_lua.registerFunction<void(DownstreamState::*)(string)>("rmPool", [](DownstreamState& s, string pool) { s.pools.erase(pool);});

  g_lua.registerFunction<void(DownstreamState::*)()>("getOutstanding", [](const DownstreamState& s) { g_outputBuffer=std::to_string(s.outstanding.load()); });


  g_lua.registerFunction("isUp", &DownstreamState::isUp);
  g_lua.registerFunction("setDown", &DownstreamState::setDown);
  g_lua.registerFunction("setUp", &DownstreamState::setUp);
  g_lua.registerFunction("setAuto", &DownstreamState::setAuto);
  g_lua.registerMember("upStatus", &DownstreamState::upStatus);
  g_lua.registerMember("weight", &DownstreamState::weight);
  g_lua.registerMember("order", &DownstreamState::order);
  
  g_lua.writeFunction("infolog", [](const string& arg) {
      infolog("%s", arg);
    });
  g_lua.writeFunction("errlog", [](const string& arg) {
      errlog("%s", arg);
    });
  g_lua.writeFunction("warnlog", [](const string& arg) {
      warnlog("%s", arg);
    });


  g_lua.writeFunction("show", [](const string& arg) {
      g_outputBuffer+=arg;
      g_outputBuffer+="\n";
    });

  g_lua.registerFunction<void(dnsheader::*)(bool)>("setRD", [](dnsheader& dh, bool v) {
      dh.rd=v;
    });

  g_lua.registerFunction<bool(dnsheader::*)()>("getRD", [](dnsheader& dh) {
      return (bool)dh.rd;
    });


  g_lua.registerFunction<void(dnsheader::*)(bool)>("setTC", [](dnsheader& dh, bool v) {
      dh.tc=v;
    });

  g_lua.registerFunction<void(dnsheader::*)(bool)>("setQR", [](dnsheader& dh, bool v) {
      dh.qr=v;
    });


  g_lua.registerFunction("tostring", &ComboAddress::toString);

  g_lua.registerFunction("isPartOf", &DNSName::isPartOf);
  g_lua.registerFunction("tostring", &DNSName::toString);
  g_lua.writeFunction("newDNSName", [](const std::string& name) { return DNSName(name); });
  g_lua.writeFunction("newSuffixMatchNode", []() { return SuffixMatchNode(); });

  g_lua.registerFunction("add",(void (SuffixMatchNode::*)(const DNSName&)) &SuffixMatchNode::add);
  g_lua.registerFunction("check",(bool (SuffixMatchNode::*)(const DNSName&) const) &SuffixMatchNode::check);

  g_lua.writeFunction("controlSocket", [client](const std::string& str) {
      ComboAddress local(str, 5199);

      if(client) {
	g_serverControl = local;
	return;
      }
      
      try {
	int sock = socket(local.sin4.sin_family, SOCK_STREAM, 0);
	SSetsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
	SBind(sock, local);
	SListen(sock, 5);
	auto launch=[sock, local]() {
	    thread t(controlThread, sock, local);
	    t.detach();
	};
	if(g_launchWork) 
	  g_launchWork->push_back(launch);
	else
	  launch();
	    
      }
      catch(std::exception& e) {
	errlog("Unable to bind to control socket on %s: %s", local.toStringWithPort(), e.what());
      }
    });


  // something needs to be done about this, unlocked will 'mostly' work
  g_lua.writeFunction("getTopQueries", [](unsigned int top, boost::optional<int> labels) {
      map<DNSName, int> counts;
      unsigned int total=0;
      if(!labels) {
	for(const auto& a : g_rings.queryRing) {
	  counts[a]++;
	  total++;
	}
      }
      else {
	unsigned int lab = *labels;
	for(auto a : g_rings.queryRing) {
	  a.trimToLabels(lab);
	  counts[a]++;
	  total++;
	}

      }
      // cout<<"Looked at "<<total<<" queries, "<<counts.size()<<" different ones"<<endl;
      vector<pair<int, DNSName>> rcounts;
      for(const auto& c : counts) 
	rcounts.push_back(make_pair(c.second, c.first));

      sort(rcounts.begin(), rcounts.end(), [](const decltype(rcounts)::value_type& a, 
					      const decltype(rcounts)::value_type& b) {
	     return b.first < a.first;
	   });

      std::unordered_map<int, vector<boost::variant<string,double>>> ret;
      unsigned int count=1, rest=0;
      for(const auto& rc : rcounts) {
	if(count==top+1)
	  rest+=rc.first;
	else
	  ret.insert({count++, {rc.second.toString(), rc.first, 100.0*rc.first/total}});
      }
      ret.insert({count, {"Rest", rest, 100.0*rest/total}});
      return ret;

    });
  
  g_lua.executeCode(R"(function topQueries(top, labels) for k,v in ipairs(getTopQueries(top,labels)) do show(string.format("%4d  %-40s %4d %4.1f%%",k,v[1],v[2], v[3])) end end)");

  g_lua.writeFunction("getTopResponses", [](unsigned int top, unsigned int kind, boost::optional<int> labels) {
      map<DNSName, int> counts;
      unsigned int total=0;
      {
	std::lock_guard<std::mutex> lock(g_rings.respMutex);
	if(!labels) {
	  for(const auto& a : g_rings.respRing) {
	    if(a.rcode!=kind)
	      continue;
	    counts[a.name]++;
	    total++;
	  }
	}
	else {
	  unsigned int lab = *labels;
	  for(auto a : g_rings.respRing) {
	    if(a.rcode!=kind)
	      continue;

	    a.name.trimToLabels(lab);
	    counts[a.name]++;
	    total++;
	  }
	  
	}
      }
      //      cout<<"Looked at "<<total<<" responses, "<<counts.size()<<" different ones"<<endl;
      vector<pair<int, DNSName>> rcounts;
      for(const auto& c : counts) 
	rcounts.push_back(make_pair(c.second, c.first));

      sort(rcounts.begin(), rcounts.end(), [](const decltype(rcounts)::value_type& a, 
					      const decltype(rcounts)::value_type& b) {
	     return b.first < a.first;
	   });

      std::unordered_map<int, vector<boost::variant<string,double>>> ret;
      unsigned int count=1, rest=0;
      for(const auto& rc : rcounts) {
	if(count==top+1)
	  rest+=rc.first;
	else
	  ret.insert({count++, {rc.second.toString(), rc.first, 100.0*rc.first/total}});
      }
      ret.insert({count, {"Rest", rest, 100.0*rest/total}});
      return ret;

    });
  
  g_lua.executeCode(R"(function topResponses(top, kind, labels) for k,v in ipairs(getTopResponses(top, kind, labels)) do show(string.format("%4d  %-40s %4d %4.1f%%",k,v[1],v[2], v[3])) end end)");


  g_lua.writeFunction("showResponseLatency", []() {

      map<double, unsigned int> histo;
      double bin=100;
      for(int i=0; i < 15; ++i) {
	histo[bin];
	bin*=2;
      }

      double totlat=0;
      int size=0;
      {
	std::lock_guard<std::mutex> lock(g_rings.respMutex);
	for(const auto& r : g_rings.respRing) {
	  ++size;
	  auto iter = histo.lower_bound(r.usec);
	  if(iter != histo.end())
	    iter->second++;
	  else
	    histo.rbegin()++;
	  totlat+=r.usec;
	}
      }

      g_outputBuffer = (boost::format("Average response latency: %.02f msec\n") % (0.001*totlat/size)).str();
      double highest=0;
      
      for(auto iter = histo.cbegin(); iter != histo.cend(); ++iter) {
	highest=std::max(highest, iter->second*1.0);
      }
      boost::format fmt("%7.2f\t%s\n");
      g_outputBuffer += (fmt % "msec" % "").str();

      for(auto iter = histo.cbegin(); iter != histo.cend(); ++iter) {
	int stars = (70.0 * iter->second/highest);
	char c='*';
	if(!stars && iter->second) {
	  stars=1; // you get 1 . to show something is there..
	  if(70.0*iter->second/highest > 0.5)
	    c=':';
	  else
	    c='.';
	}
	g_outputBuffer += (fmt % (iter->first/1000.0) % string(stars, c)).str();
      }
    });

  g_lua.writeFunction("newQPSLimiter", [](int rate, int burst) { return QPSLimiter(rate, burst); });
  g_lua.registerFunction("check", &QPSLimiter::check);


  g_lua.writeFunction("makeKey", []() {
      g_outputBuffer="setKey("+newKey()+")\n";
    });
  
  g_lua.writeFunction("setKey", [](const std::string& key) {
      if(B64Decode(key, g_key) < 0) {
	  g_outputBuffer=string("Unable to decode ")+key+" as Base64";
	  errlog("%s", g_outputBuffer);
	}
    });

  
  g_lua.writeFunction("testCrypto", [](string testmsg)
   {
     try {
       SodiumNonce sn, sn2;
       sn.init();
       sn2=sn;
       string encrypted = sodEncryptSym(testmsg, g_key, sn);
       string decrypted = sodDecryptSym(encrypted, g_key, sn2);
       
       sn.increment();
       sn2.increment();

       encrypted = sodEncryptSym(testmsg, g_key, sn);
       decrypted = sodDecryptSym(encrypted, g_key, sn2);

       if(testmsg == decrypted)
	 g_outputBuffer="Everything is ok!\n";
       else
	 g_outputBuffer="Crypto failed..\n";
       
     }
     catch(...) {
       g_outputBuffer="Crypto failed..\n";
     }});

  
  std::ifstream ifs(g_vm["config"].as<string>());
  if(!ifs) 
    warnlog("Unable to read configuration from '%s'", g_vm["config"].as<string>());
  else
    infolog("Read configuration from '%s'", g_vm["config"].as<string>());

  g_lua.executeCode(ifs);
  auto ret=*g_launchWork;
  delete g_launchWork;
  g_launchWork=0;
  return ret;
}
