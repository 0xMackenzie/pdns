#include "dnsname.hh"
#include <boost/format.hpp>
#include <string>
#include "dnswriter.hh"
#include "misc.hh"

DNSName::DNSName(const char* p)
{
  d_labels = segmentDNSName(p);
  for(const auto& e : d_labels)
    if(e.size() > 63)
      throw std::range_error("label too long");
}

// this should be the __only__ dns name parser in PowerDNS. 
DNSName::DNSName(const char* pos, int len, int offset, bool uncompress, uint16_t* qtype, uint16_t* qclass)
{
  unsigned char labellen;
  const char *opos = pos;
  pos += offset;
  const char* end = pos + len;
  while((labellen=*pos++) && pos < end) { // "scan and copy"
    if(labellen & 0xc0) {
      if(!uncompress)
	throw std::range_error("Found compressed label, instructed not to follow");

      labellen &= (~0xc0);
      int newpos = (labellen << 8) + *(const unsigned char*)pos;

      (*this) += DNSName(opos, len, newpos, false);
      pos++;
      break;
    }
    d_labels.push_back(string(pos, labellen));
    pos+=labellen;
  }
  if(qtype && pos + labellen + 2 <= end)  
    *qtype=(*(const unsigned char*)pos)*256 + *((const unsigned char*)pos+1);

  pos+=2;
  if(qclass && pos + labellen + 2 <= end)  
    *qclass=(*(const unsigned char*)pos)*256 + *((const unsigned char*)pos+1);

}

std::string DNSName::toString() const
{
  if(d_labels.empty())  // I keep wondering if there is some deeper meaning to the need to do this
    return ".";
  std::string ret;
  for(const auto& s : d_labels) {
    ret+= escapeLabel(s) + ".";
  }
  return ret;
}

std::string DNSName::toDNSString() const
{
  std::string ret;
  for(const auto& s : d_labels) {
    ret.append(1, (char) s.length());
    ret.append(s);
  }
  ret.append(1, (char)0);
  return ret;
}


bool DNSName::isPartOf(const DNSName& parent) const
{
  auto us = d_labels.crbegin();
  auto p = parent.d_labels.crbegin();
  for(; us != d_labels.crend() && p != parent.d_labels.crend(); ++us, ++p) {
    if(!pdns_iequals(*p, *us))
      break;
  }
  return (p==parent.d_labels.crend());
}

void DNSName::appendRawLabel(const std::string& label)
{
  if(label.size() > 63)
    throw std::range_error("label too long");
  d_labels.push_back(label);
}

void DNSName::prependRawLabel(const std::string& label)
{
  if(label.size() > 63)
    throw std::range_error("label too long");

  d_labels.push_front(label);
}

deque<string> DNSName::getRawLabels() const
{
  return d_labels;
}

bool DNSName::chopOff() 
{
  if(d_labels.empty())
    return false;
  d_labels.pop_front();
  return true;
}

bool DNSName::operator==(const DNSName& rhs) const
{
  if(rhs.d_labels.size() != d_labels.size())
    return false;

  auto us = d_labels.crbegin();
  auto p = rhs.d_labels.crbegin();
  for(; us != d_labels.crend() && p != rhs.d_labels.crend(); ++us, ++p) {
    if(!pdns_iequals(*p, *us))
      return false;
  }
  return true;
}

string DNSName::escapeLabel(const std::string& label)
{
  string ret;
  for(uint8_t p : label) {
    if(p=='.') 
      ret+="\\.";
    else if(p=='\\')
      ret+="\\\\";
    else if(p > 0x21 && p < 0x7e)
      ret.append(1, (char)p);
    else {
      ret+="\\" + (boost::format("%03o") % (unsigned int)p).str();
    }
  }
  return ret;
}
