/*
 * Copyright (C) 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2009 Google Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SocketStreamHandle.h"

#include "Logging.h"
#include "SocketStreamError.h"
#include "SocketStreamHandleClient.h"

#if defined(BUILDING_ON_TIGER) || defined(BUILDING_ON_LEOPARD)
#include <SystemConfiguration/SystemConfiguration.h>
#endif

extern "C" {
extern const CFStringRef kCFStreamPropertyCONNECTProxy;
extern const CFStringRef kCFStreamPropertyCONNECTProxyHost;
extern const CFStringRef kCFStreamPropertyCONNECTProxyPort;
}

namespace WebCore {

SocketStreamHandle::SocketStreamHandle(const KURL& url, SocketStreamHandleClient* client)
    : SocketStreamHandleBase(url, client)
    , m_connectingSubstate(New)
    , m_connectionType(Unknown)
{
    LOG(Network, "SocketStreamHandle %p new client %p", this, m_client);

    ASSERT(url.protocolIs("ws") || url.protocolIs("wss"));

    if (!m_url.port())
        m_url.setPort(shouldUseSSL() ? 443 : 80);

    KURL httpURL(KURL(), (shouldUseSSL() ? "https://" : "http://") + m_url.host());
    m_httpURL.adoptCF(httpURL.createCFURL());

    createStreams();
    ASSERT(!m_readStream == !m_writeStream);
    if (!m_readStream) // Doing asynchronous PAC file processing, streams will be created later.
        return;

    CFStreamClientContext clientContext = { 0, this, 0, 0, copyCFStreamDescription };
    // FIXME: Pass specific events we're interested in instead of -1.
    CFReadStreamSetClient(m_readStream.get(), static_cast<CFOptionFlags>(-1), readStreamCallback, &clientContext);
    CFWriteStreamSetClient(m_writeStream.get(), static_cast<CFOptionFlags>(-1), writeStreamCallback, &clientContext);

    CFReadStreamScheduleWithRunLoop(m_readStream.get(), CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    CFWriteStreamScheduleWithRunLoop(m_writeStream.get(), CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    
    CFReadStreamOpen(m_readStream.get());
    CFWriteStreamOpen(m_writeStream.get());

    m_connectingSubstate = WaitingForConnect;
}

void SocketStreamHandle::chooseProxy()
{
#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)
    RetainPtr<CFDictionaryRef> proxyDictionary(AdoptCF, CFNetworkCopySystemProxySettings());
#else
    // We don't need proxy information often, so there is no need to set up a permanent dynamic store session.
    RetainPtr<CFDictionaryRef> proxyDictionary(AdoptCF, SCDynamicStoreCopyProxies(0));
#endif

    // SOCKS or HTTPS (AKA CONNECT) proxies are supported.
    // WebSocket protocol relies on handshake being transferred unchanged, so we need a proxy that will not modify headers.
    // Since HTTP proxies must add Via headers, they are highly unlikely to work.
    // Many CONNECT proxies limit connectivity to port 443, so we prefer SOCKS, if configured.

    if (!proxyDictionary) {
        m_connectionType = Direct;
        return;
    }

#ifndef BUILDING_ON_TIGER
    // CFNetworkCopyProxiesForURL doesn't know about WebSocket schemes, so pretend to use http.
    // Always use "https" to get HTTPS proxies in result - we'll try to use those for ws:. even though many are configured to reject connections to ports other than 443.
    KURL httpsURL(KURL(), "https://" + m_url.host());
    RetainPtr<CFURLRef> httpsURLCF(AdoptCF, httpsURL.createCFURL());

    RetainPtr<CFArrayRef> proxyArray(AdoptCF, CFNetworkCopyProxiesForURL(httpsURLCF.get(), proxyDictionary.get()));
    CFIndex proxyArrayCount = CFArrayGetCount(proxyArray.get());

    // FIXME: Support PAC files (always the preferred entry).

    CFDictionaryRef chosenProxy = 0;
    for (CFIndex i = 0; i < proxyArrayCount; ++i) {
        CFDictionaryRef proxyInfo = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(proxyArray.get(), i));
        CFTypeRef proxyType = CFDictionaryGetValue(proxyInfo, kCFProxyTypeKey);
        if (proxyType && CFGetTypeID(proxyType) == CFStringGetTypeID()) {
            if (CFEqual(proxyType, kCFProxyTypeSOCKS)) {
                m_connectionType = SOCKSProxy;
                chosenProxy = proxyInfo;
                break;
            }
            if (CFEqual(proxyType, kCFProxyTypeHTTPS)) {
                m_connectionType = CONNECTProxy;
                chosenProxy = proxyInfo;
                // Keep looking for proxies, as a SOCKS one is preferable.
            }
        }
    }

    if (chosenProxy) {
        ASSERT(m_connectionType != Unknown);
        ASSERT(m_connectionType != Direct);

        CFTypeRef proxyHost = CFDictionaryGetValue(chosenProxy, kCFProxyHostNameKey);
        CFTypeRef proxyPort = CFDictionaryGetValue(chosenProxy, kCFProxyPortNumberKey);

        if (proxyHost && CFGetTypeID(proxyHost) == CFStringGetTypeID() && proxyPort && CFGetTypeID(proxyPort) == CFNumberGetTypeID()) {
            m_proxyHost = static_cast<CFStringRef>(proxyHost);
            m_proxyPort = static_cast<CFNumberRef>(proxyPort);
            return;
        }
    }
#else // BUILDING_ON_TIGER
    // FIXME: check proxy bypass list and ExcludeSimpleHostnames.
    // FIXME: Support PAC files.

    CFTypeRef socksEnableCF = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesSOCKSEnable);
    int socksEnable;
    if (socksEnableCF && CFGetTypeID(socksEnableCF) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(socksEnableCF), kCFNumberIntType, &socksEnable) && socksEnable) {
        CFTypeRef proxyHost = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesSOCKSProxy);
        CFTypeRef proxyPort = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesSOCKSPort);
        if (proxyHost && CFGetTypeID(proxyHost) == CFStringGetTypeID() && proxyPort && CFGetTypeID(proxyPort) == CFNumberGetTypeID()) {
            m_proxyHost = static_cast<CFStringRef>(proxyHost);
            m_proxyPort = static_cast<CFNumberRef>(proxyPort);
            m_connectionType = SOCKSProxy;
            return;
        }
    }

    CFTypeRef httpsEnableCF = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesHTTPSEnable);
    int httpsEnable;
    if (httpsEnableCF && CFGetTypeID(httpsEnableCF) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(httpsEnableCF), kCFNumberIntType, &httpsEnable) && httpsEnable) {
        CFTypeRef proxyHost = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesHTTPSProxy);
        CFTypeRef proxyPort = CFDictionaryGetValue(proxyDictionary.get(), kSCPropNetProxiesHTTPSPort);

        if (proxyHost && CFGetTypeID(proxyHost) == CFStringGetTypeID() && proxyPort && CFGetTypeID(proxyPort) == CFNumberGetTypeID()) {
            m_proxyHost = static_cast<CFStringRef>(proxyHost);
            m_proxyPort = static_cast<CFNumberRef>(proxyPort);
            m_connectionType = CONNECTProxy;
            return;
        }
    }
#endif

    m_connectionType = Direct;
}

void SocketStreamHandle::createStreams()
{
    if (m_connectionType == Unknown)
        chooseProxy();

    // If it's still unknown, then we're resolving a PAC file asynchronously.
    if (m_connectionType == Unknown)
        return;

    RetainPtr<CFStringRef> host(AdoptCF, m_url.host().createCFString());

    // Creating streams to final destination, not to proxy.
    CFReadStreamRef readStream = 0;
    CFWriteStreamRef writeStream = 0;
    CFStreamCreatePairWithSocketToHost(0, host.get(), m_url.port(), &readStream, &writeStream);

    m_readStream.adoptCF(readStream);
    m_writeStream.adoptCF(writeStream);

    switch (m_connectionType) {
    case Unknown:
        ASSERT_NOT_REACHED();
        break;
    case Direct:
        break;
    case SOCKSProxy: {
        // FIXME: SOCKS5 doesn't do challenge-response, should we try to apply credentials from Keychain right away?
        // But SOCKS5 credentials don't work at the time of this writing anyway, see <rdar://6776698>.
        const void* proxyKeys[] = { kCFStreamPropertySOCKSProxyHost, kCFStreamPropertySOCKSProxyPort };
        const void* proxyValues[] = { m_proxyHost.get(), m_proxyPort.get() };
        RetainPtr<CFDictionaryRef> connectDictionary(AdoptCF, CFDictionaryCreate(0, proxyKeys, proxyValues, sizeof(proxyKeys) / sizeof(*proxyKeys), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFReadStreamSetProperty(m_readStream.get(), kCFStreamPropertySOCKSProxy, connectDictionary.get());
        break;
        }
    case CONNECTProxy: {
        const void* proxyKeys[] = { kCFStreamPropertyCONNECTProxyHost, kCFStreamPropertyCONNECTProxyPort };
        const void* proxyValues[] = { m_proxyHost.get(), m_proxyPort.get() };
        RetainPtr<CFDictionaryRef> connectDictionary(AdoptCF, CFDictionaryCreate(0, proxyKeys, proxyValues, sizeof(proxyKeys) / sizeof(*proxyKeys), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFReadStreamSetProperty(m_readStream.get(), kCFStreamPropertyCONNECTProxy, connectDictionary.get());
        break;
        }
    }

    if (shouldUseSSL()) {
        const void* keys[] = { kCFStreamSSLPeerName, kCFStreamSSLLevel };
        const void* values[] = { host.get(), kCFStreamSocketSecurityLevelNegotiatedSSL };
        RetainPtr<CFDictionaryRef> settings(AdoptCF, CFDictionaryCreate(0, keys, values, sizeof(keys) / sizeof(*keys), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFReadStreamSetProperty(m_readStream.get(), kCFStreamPropertySSLSettings, settings.get());
        CFWriteStreamSetProperty(m_writeStream.get(), kCFStreamPropertySSLSettings, settings.get());
    }
}

CFStringRef SocketStreamHandle::copyCFStreamDescription(void* info)
{
    SocketStreamHandle* handle = static_cast<SocketStreamHandle*>(info);
    return ("WebKit socket stream, " + handle->m_url.string()).createCFString();
}

void SocketStreamHandle::readStreamCallback(CFReadStreamRef stream, CFStreamEventType type, void* clientCallBackInfo)
{
    SocketStreamHandle* handle = static_cast<SocketStreamHandle*>(clientCallBackInfo);
    ASSERT_UNUSED(stream, stream == handle->m_readStream.get());
    handle->readStreamCallback(type);
}

void SocketStreamHandle::writeStreamCallback(CFWriteStreamRef stream, CFStreamEventType type, void* clientCallBackInfo)
{
    SocketStreamHandle* handle = static_cast<SocketStreamHandle*>(clientCallBackInfo);
    ASSERT_UNUSED(stream, stream == handle->m_writeStream.get());
    handle->writeStreamCallback(type);
}

void SocketStreamHandle::readStreamCallback(CFStreamEventType type)
{
    switch(type) {
    case kCFStreamEventNone:
        break;
    case kCFStreamEventOpenCompleted:
        break;
    case kCFStreamEventHasBytesAvailable: {
        if (m_connectingSubstate == WaitingForConnect) {
            // FIXME: Handle CONNECT proxy credentials here.
        } else if (m_connectingSubstate == WaitingForCredentials)
            break;

        if (m_connectingSubstate == WaitingForConnect) {
            m_connectingSubstate = Connected;
            m_state = Open;
            m_client->didOpen(this);
            // Fall through.
        } else if (m_state == Closed)
            break;

        ASSERT(m_state == Open);
        ASSERT(m_connectingSubstate == Connected);

        CFIndex length;
        UInt8 localBuffer[1024]; // Used if CFReadStreamGetBuffer couldn't return anything.
        const UInt8* ptr = CFReadStreamGetBuffer(m_readStream.get(), 0, &length);
        if (!ptr) {
            length = CFReadStreamRead(m_readStream.get(), localBuffer, sizeof(localBuffer));
            ptr = localBuffer;
        }

        m_client->didReceiveData(this, reinterpret_cast<const char*>(ptr), length);

        break;
    }
    case kCFStreamEventCanAcceptBytes:
        ASSERT_NOT_REACHED();
        break;
    case kCFStreamEventErrorOccurred: {
        CFStreamError error = CFReadStreamGetError(m_readStream.get());
        m_client->didFail(this, SocketStreamError(error.error)); // FIXME: Provide a sensible error.
        break;
    }
    case kCFStreamEventEndEncountered:
        m_client->didClose(this);
        break;
    }
}

void SocketStreamHandle::writeStreamCallback(CFStreamEventType type)
{
    switch(type) {
    case kCFStreamEventNone:
        break;
    case kCFStreamEventOpenCompleted:
        break;
    case kCFStreamEventHasBytesAvailable:
        ASSERT_NOT_REACHED();
        break;
    case kCFStreamEventCanAcceptBytes: {
        // Possibly, a spurious event from CONNECT handshake.
        if (!CFWriteStreamCanAcceptBytes(m_writeStream.get()))
            return;

        if (m_connectingSubstate == WaitingForCredentials)
            break;

        if (m_connectingSubstate == WaitingForConnect) {
            m_connectingSubstate = Connected;
            m_state = Open;
            m_client->didOpen(this);
            break;
        }

        ASSERT(m_state = Open);
        ASSERT(m_connectingSubstate == Connected);

        sendPendingData();
        break;
    }
    case kCFStreamEventErrorOccurred: {
        CFStreamError error = CFWriteStreamGetError(m_writeStream.get());
        m_client->didFail(this, SocketStreamError(error.error)); // FIXME: Provide a sensible error.
        break;
    }
    case kCFStreamEventEndEncountered:
        // FIXME: Currently, we call didClose from read callback, but these can come independently (e.g. a server can stop listening, but keep sending data).
        break;
    }
}

SocketStreamHandle::~SocketStreamHandle()
{
    LOG(Network, "SocketStreamHandle %p dtor", this);
}

int SocketStreamHandle::platformSend(const char* data, int length)
{
    if (!CFWriteStreamCanAcceptBytes(m_writeStream.get()))
        return 0;

    return CFWriteStreamWrite(m_writeStream.get(), reinterpret_cast<const UInt8*>(data), length);
}

void SocketStreamHandle::platformClose()
{
    LOG(Network, "SocketStreamHandle %p platformClose", this);

    ASSERT(!m_readStream == !m_writeStream);
    if (!m_readStream)
        return;

    CFReadStreamUnscheduleFromRunLoop(m_readStream.get(), CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    CFWriteStreamUnscheduleFromRunLoop(m_writeStream.get(), CFRunLoopGetCurrent(), kCFRunLoopCommonModes);

    CFReadStreamClose(m_readStream.get());
    CFWriteStreamClose(m_writeStream.get());
    
    m_readStream = 0;
    m_writeStream = 0;
}

void SocketStreamHandle::receivedCredential(const AuthenticationChallenge&, const Credential&)
{
}

void SocketStreamHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&)
{
}

void SocketStreamHandle::receivedCancellation(const AuthenticationChallenge&)
{
}

}  // namespace WebCore
