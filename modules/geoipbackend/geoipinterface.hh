/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef PDNS_GEOIPINTERFACE_HH
#define PDNS_GEOIPINTERFACE_HH

class GeoIPInterface {
public:
  enum GeoIPQueryAttribute {
    ASn,
    City,
    Continent,
    Country,
    Country2,
    Name,
    Region
  };

  virtual bool queryCountry(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryCountryV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryCountry2(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryCountry2V6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryContinent(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryContinentV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryName(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryNameV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryASnum(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryASnumV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryRegion(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryRegionV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryCity(string &ret, GeoIPLookup* gl, const string &ip) = 0;
  virtual bool queryCityV6(string &ret, GeoIPLookup* gl, const string &ip) = 0;

  virtual ~GeoIPInterface() { }

  static unique_ptr<GeoIPInterface> makeInterface(const string& dbStr);
private:
  static unique_ptr<GeoIPInterface> makeDATInterface(const string& fname, const map<string, string>& opts);
};

#endif
