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
#pragma once

extern size_t g_EdnsUDPPayloadSize;
extern uint16_t g_PayloadSizeSelfGenAnswers;

int rewriteResponseWithoutEDNS(const std::string& initialPacket, vector<uint8_t>& newContent);
int locateEDNSOptRR(const std::string& packet, uint16_t * optStart, size_t * optLen, bool * last);
bool handleEDNSClientSubnet(char * packet, size_t packetSize, unsigned int consumed, uint16_t * len, bool* ednsAdded, bool* ecsAdded, const ComboAddress& remote, bool overrideExisting, uint16_t ecsPrefixLength);
void generateOptRR(const std::string& optRData, string& res, uint16_t udpPayloadSize, bool dnssecOK);
int removeEDNSOptionFromOPT(char* optStart, size_t* optLen, const uint16_t optionCodeToRemove);
int rewriteResponseWithoutEDNSOption(const std::string& initialPacket, const uint16_t optionCodeToSkip, vector<uint8_t>& newContent);
int getEDNSOptionsStart(char* packet, const size_t offset, const size_t len, char ** optRDLen, size_t * remaining);
bool isEDNSOptionInOpt(const std::string& packet, const size_t optStart, const size_t optLen, const uint16_t optionCodeToFind);
bool addEDNS(DNSQuestion& dq, bool dnssecOK);
bool addEDNSToQueryTurnedResponse(DNSQuestion& dq);

int getEDNSZ(const DNSQuestion& dq);
bool queryHasEDNS(const DNSQuestion& dq);

