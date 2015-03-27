// Sandstorm - Personal Cloud Sandbox
// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <kj/debug.h>
#include <limits.h>
#include <sys/types.h>
#include <nss.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <capnp/ez-rpc.h>
#include <sandstorm/hack-session.capnp.h>
#include <sandstorm/sandstorm-http-bridge.capnp.h>

#include <stdio.h>
#include <vector>
#include <iterator>

#define ALIGN(x) ((x+sizeof(void*)-1) & ~(sizeof(void*) - 1))

static inline size_t PROTO_ADDRESS_SIZE(int proto) {
  return proto == AF_INET6 ? 16 : 4;
}

extern "C" {
enum nss_status _nss_sandstormdns_gethostbyname4_r(
    const char *hostname,
    struct gaih_addrtuple **pat,
    char *buffer, size_t bufLength,
    int *errnop, int *h_errnop,
    int32_t *ttlp) {
    // KJ_SYSCALL(write(STDERR_FILENO, "Failure\n",
    //            sizeof("Failure\n")));


  try {
    // TODO(someday): consider moving this global and/or lazy loading this (there are some serious concerns though)
    // TODO(soon): make sure this works even if the thread has another event loop setup
    capnp::EzRpcClient client("unix:/tmp/sandstorm-api");
    sandstorm::SandstormHttpBridge::Client restorer = client.getMain<sandstorm::SandstormHttpBridge>();

    auto request = restorer.getFirstSessionContextRequest();
    auto session = request.send().getContext().castAs<sandstorm::HackSessionContext>();
    auto ipNetwork = session.getIpNetworkRequest().send().getNetwork();
    auto getAddressRequest = ipNetwork.getIpAddressForHostnameRequest();
    getAddressRequest.setHostname(hostname);

    auto address = getAddressRequest.send().wait(client.getWaitScope()).getAddress();

    auto hostnameLength = strlen(hostname);
    auto expectedLength = ALIGN(hostnameLength + 1) + ALIGN(sizeof(struct gaih_addrtuple));
    if (bufLength < expectedLength) {
      *errnop = ENOMEM;
      *h_errnop = NO_RECOVERY;
      return NSS_STATUS_TRYAGAIN;
    }

    // copy hostname
    auto resHostname = buffer;
    memcpy(buffer, hostname, hostnameLength + 1);
    auto idx = ALIGN(hostnameLength + 1);

    gaih_addrtuple * r_tuple;
    // TODO(someday): when there's more than 1 address, fill in addresses in backwards order
    if (!(address.getUpper64() == 0 && (address.getLower64() >> 32) == 0xFFFF)) {
      // TODO(someday): handle ipv6
      *errnop = EAFNOSUPPORT;
      *h_errnop = NO_DATA;
      return NSS_STATUS_UNAVAIL;
    }
    r_tuple = (struct gaih_addrtuple*) (buffer + idx);
    r_tuple->next = NULL;
    r_tuple->name = resHostname;
    r_tuple->family = AF_INET;
    r_tuple->scopeid = 0;

    r_tuple->addr[0] = htonl(address.getLower64() & 0xFFFFFFFF);

    idx += ALIGN(sizeof(struct gaih_addrtuple));

    *pat = r_tuple;

    if (ttlp)
      *ttlp = 0;

    return NSS_STATUS_SUCCESS;
  } catch (kj::Exception& e) {
    *errnop = ENOENT;
    *h_errnop = HOST_NOT_FOUND;
    KJ_LOG(ERROR, e.getDescription());
    return (NSS_STATUS_NOTFOUND);
  } catch (...) {
    *errnop = EINVAL;
    *h_errnop = NO_DATA;
    return NSS_STATUS_UNAVAIL;
  }
}

enum nss_status _nss_sandstormdns_gethostbyname3_r(
    const char *hostname,
    int af,
    struct hostent *result,
    char *buffer, size_t bufLength,
    int *errnop, int *h_errnop,
    int32_t *ttlp,
    char **canonp) {
  if (af != AF_INET) {
    *errnop = EAFNOSUPPORT;
    *h_errnop = NO_DATA;
    return NSS_STATUS_UNAVAIL;
  }

  try {
    auto addressLength = PROTO_ADDRESS_SIZE(af); // This will be 4 since we only support ip4

    capnp::EzRpcClient client("unix:/tmp/sandstorm-api");
    sandstorm::SandstormHttpBridge::Client restorer = client.getMain<sandstorm::SandstormHttpBridge>();

    auto request = restorer.getFirstSessionContextRequest();
    auto session = request.send().getContext().castAs<sandstorm::HackSessionContext>();
    auto ipNetwork = session.getIpNetworkRequest().send().getNetwork();
    auto getAddressRequest = ipNetwork.getIpAddressForHostnameRequest();
    getAddressRequest.setHostname(hostname);

    auto address = getAddressRequest.send().wait(client.getWaitScope()).getAddress();

    auto hostnameLength = strlen(hostname);
    auto expectedLength = ALIGN(hostnameLength + 1) +
                          sizeof(char *) + // empty aliases array
                          ALIGN(addressLength) +
                          2 * sizeof(char *); //
    if (bufLength < expectedLength) {
      *errnop = ENOMEM;
      *h_errnop = NO_RECOVERY;
      return NSS_STATUS_TRYAGAIN;
    }

    // copy hostname
    auto resHostname = buffer;
    memcpy(buffer, hostname, hostnameLength + 1);
    auto idx = ALIGN(hostnameLength + 1);

    // create empty aliases array
    char** resAliases = (char**)buffer + idx;
    *resAliases = NULL;
    idx += sizeof(char*);

    // add address
    uint32_t* resAddr = (uint32_t*)buffer + idx;
    resAddr[0] = htonl(address.getLower64() & 0xFFFFFFFF);

    idx += ALIGN(addressLength);

    /* Fourth, add address pointer array */
    char** resAddrList = (char**)buffer + idx;
    resAddrList[0] = (char*)resAddr;
    resAddrList[1] = NULL;

    idx += 2 * sizeof(char *);

    result->h_name = resHostname;
    result->h_aliases = resAliases;
    result->h_addrtype = af;
    result->h_length = addressLength;
    result->h_addr_list = resAddrList;

    if (ttlp)
      *ttlp = 0;

    if (canonp)
      *canonp = resHostname;

    return NSS_STATUS_SUCCESS;
  } catch (kj::Exception& e) {
    *errnop = ENOENT;
    *h_errnop = HOST_NOT_FOUND;
    KJ_LOG(ERROR, e.getDescription());
    return (NSS_STATUS_NOTFOUND);
  } catch (...) {
    *errnop = EINVAL;
    *h_errnop = NO_DATA;
    return NSS_STATUS_UNAVAIL;
  }
}


enum nss_status _nss_sandstormdns_gethostbyname2_r(
    const char *name,
    int af,
    struct hostent *host,
    char *buffer, size_t buflen,
    int *errnop, int *h_errnop)
{
  return _nss_sandstormdns_gethostbyname3_r(
      name,
      af,
      host,
      buffer, buflen,
      errnop, h_errnop,
      NULL,
      NULL);
}

enum nss_status _nss_sandstormdns_gethostbyname_r(
    const char *name,
    struct hostent *host,
    char *buffer, size_t buflen,
    int *errnop, int *h_errnop)
{
  return _nss_sandstormdns_gethostbyname3_r(
      name,
      AF_INET,
      host,
      buffer, buflen,
      errnop, h_errnop,
      NULL,
      NULL);
}

enum nss_status _nss_sandstormdns_gethostbyaddr2_r(
    const void* addr, socklen_t len,
    int af,
    struct hostent *result,
    char *buffer, size_t buflen,
    int *errnop, int *h_errnop,
    int32_t *ttlp)
{
  *errnop = EAFNOSUPPORT;
  *h_errnop = NO_DATA;
  return NSS_STATUS_UNAVAIL;
}

enum nss_status _nss_sandstormdns_gethostbyaddr_r(
    const void* addr, socklen_t len,
    int af,
    struct hostent *host,
    char *buffer, size_t buflen,
    int *errnop, int *h_errnop)
{
  return _nss_sandstormdns_gethostbyaddr2_r(
      addr, len,
      af,
      host,
      buffer, buflen,
      errnop, h_errnop,
      NULL);
}

} // extern "C"
