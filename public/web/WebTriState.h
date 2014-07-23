// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WebTriState_h
#define WebTriState_h

namespace blink {

// The following enum should be consistent with the TriState enum
// defined in WTF.
enum WebTriState {
    WebFalseTriState,
    WebTrueTriState,
    WebMixedTriState
};

} // namespace blink

#endif
