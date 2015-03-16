#include "dnsdist.hh"
#include "dnsname.hh"

class NetmaskGroupRule : public DNSRule
{
public:
  NetmaskGroupRule(const NetmaskGroup& nmg) : d_nmg(nmg)
  {

  }
  bool matches(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh) const override
  {
    return d_nmg.match(remote);
  }

  string toString() const override
  {
    return d_nmg.toString();
  }
private:
  NetmaskGroup d_nmg;
};

class SuffixMatchNodeRule : public DNSRule
{
public:
  SuffixMatchNodeRule(const SuffixMatchNode& smn) : d_smn(smn)
  {
  }
  bool matches(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh) const override
  {
    return d_smn.check(qname);
  }
  string toString() const override
  {
    return d_smn.toString();
  }
private:
  SuffixMatchNode d_smn;
};

class QTypeRule : public DNSRule
{
public:
  QTypeRule(uint16_t qtype) : d_qtype(qtype)
  {
  }
  bool matches(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh) const override
  {
    return d_qtype == qtype;
  }
  string toString() const override
  {
    QType qt(d_qtype);
    return "qtype=="+qt.getName();
  }
private:
  uint16_t d_qtype;
};

class DropAction : public DNSAction
{
public:
  DNSAction::Action operator()(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh, string* ruleresult) const override
  {
    return Action::Drop;
  }
  string toString() const override
  {
    return "drop";
  }
};

class QPSAction : public DNSAction
{
public:
  QPSAction(int limit) : d_qps(limit, limit) 
  {}
  DNSAction::Action operator()(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh, string* ruleresult) const override
  {
    if(d_qps.check())
      return Action::Allow;
    else
      return Action::Drop;
  }
  string toString() const override
  {
    return "qps limit to "+std::to_string(d_qps.getRate()); 
  }
private:
  QPSLimiter d_qps;
};

class PoolAction : public DNSAction
{
public:
  PoolAction(const std::string& pool) : d_pool(pool) {}
  DNSAction::Action operator()(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh, string* ruleresult) const override
  {
    *ruleresult=d_pool;
    return Action::Pool;
  }
  string toString() const override
  {
    return "to pool "+d_pool;
  }

private:
  string d_pool;
};

class RCodeAction : public DNSAction
{
public:
  RCodeAction(int rcode) : d_rcode(rcode) {}
  DNSAction::Action operator()(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh, string* ruleresult) const override
  {
    dh->rcode = d_rcode;
    dh->qr = true; // for good measure
    return Action::HeaderModify;
  }
  string toString() const override
  {
    return "set rcode "+std::to_string(d_rcode);
  }

private:
  int d_rcode;
};

class TCAction : public DNSAction
{
public:
  DNSAction::Action operator()(const ComboAddress& remote, const DNSName& qname, uint16_t qtype, dnsheader* dh, string* ruleresult) const override
  {
    dh->tc = true;
    dh->qr = true; // for good measure
    return Action::HeaderModify;
  }
  string toString() const override
  {
    return "tc=1 answer";
  }
};
