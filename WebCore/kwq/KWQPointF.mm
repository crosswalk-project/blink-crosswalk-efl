/*
 * Copyright (C) 2004, 2006 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) 2005 Nokia.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#import "KWQPointF.h"
#import "IntPointArray.h"

QPointF::QPointF() : xCoord(0), yCoord(0)
{
}

QPointF::QPointF(float xIn, float yIn) : xCoord(xIn), yCoord(yIn)
{
}

QPointF::QPointF(const IntPoint& p) :xCoord(p.x()), yCoord(p.y())
{
}

#ifndef NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES
QPointF::QPointF(const NSPoint& p) : xCoord(p.x), yCoord(p.y)
{
}
#endif

QPointF::QPointF(const CGPoint& p) : xCoord(p.x), yCoord(p.y)
{
}

#ifndef NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES
QPointF::operator NSPoint() const
{
    return NSMakePoint(xCoord, yCoord);
}
#endif

QPointF::operator CGPoint() const
{
    return CGPointMake(xCoord, yCoord);
}

QPointF operator+(const QPointF& a, const QPointF& b)
{
    return QPointF(a.xCoord + b.xCoord, a.yCoord + b.yCoord);
}

QPointF operator-(const QPointF& a, const QPointF& b)
{
    return QPointF(a.xCoord - b.xCoord, a.yCoord - b.yCoord);
}

const QPointF operator*(const QPointF& p, double s)
{
    return QPointF(p.xCoord * s, p.yCoord * s);
}
