// -*- c-basic-offset: 2 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004 Apple Computer, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "kjs_binding.h"

#include "kjs_dom.h"
#include "kjs_window.h"
#include <kjs/internal.h> // for InterpreterImp
#include <kjs/collector.h>

#include "dom/dom_exception.h"
#include "dom/dom2_events.h"
#include "dom/dom2_range.h"
#include "misc/hashmap.h"
#include "xml/dom_nodeimpl.h"
#include "xml/dom2_eventsimpl.h"
#include "dom/css_stylesheet.h"

#include <kdebug.h>

using DOM::CSSException;
using DOM::DOMString;
using DOM::DocumentImpl;
using DOM::NodeImpl;
using DOM::RangeException;
using khtml::HashMap;
using khtml::PointerHash;

namespace KJS {

/* TODO:
 * The catch all (...) clauses below shouldn't be necessary.
 * But they helped to view for example www.faz.net in an stable manner.
 * Those unknown exceptions should be treated as severe bugs and be fixed.
 *
 * these may be CSS exceptions - need to check - pmk
 */

Value DOMObject::get(ExecState *exec, const Identifier &p) const
{
  return tryGet(exec,p);
}

void DOMObject::put(ExecState *exec, const Identifier &propertyName,
                    const Value &value, int attr)
{
  tryPut(exec, propertyName, value, attr);
}

UString DOMObject::toString(ExecState *) const
{
  return "[object " + className() + "]";
}

Value DOMFunction::get(ExecState *exec, const Identifier &propertyName) const
{
  return tryGet(exec, propertyName);
}

Value DOMFunction::call(ExecState *exec, Object &thisObj, const List &args)
{
  return tryCall(exec, thisObj, args);
}

typedef HashMap<void *, DOMObject *> DOMObjectMap;
typedef HashMap<NodeImpl *, DOMNode *, PointerHash<NodeImpl *> > NodeMap;
typedef HashMap<DocumentImpl *, NodeMap *, PointerHash<DocumentImpl *> > NodePerDocMap;

static DOMObjectMap *domObjects()
{ 
  static DOMObjectMap* staticDomObjects = new DOMObjectMap();
  return staticDomObjects;
}

static NodePerDocMap *domNodesPerDocument()
{
  static NodePerDocMap *staticDOMNodesPerDocument = new NodePerDocMap();
  return staticDOMNodesPerDocument;
}


ScriptInterpreter::ScriptInterpreter( const Object &global, KHTMLPart* part )
  : Interpreter( global ), m_part( part ),
    m_evt( 0L ), m_inlineCode(false), m_timerCallback(false)
{
#ifdef KJS_VERBOSE
  kdDebug(6070) << "ScriptInterpreter::ScriptInterpreter " << this << " for part=" << m_part << endl;
#endif
}

ScriptInterpreter::~ScriptInterpreter()
{
#ifdef KJS_VERBOSE
  kdDebug(6070) << "ScriptInterpreter::~ScriptInterpreter " << this << " for part=" << m_part << endl;
#endif
}

DOMObject* ScriptInterpreter::getDOMObject(void* objectHandle) 
{
    return domObjects()->get(objectHandle);
}

void ScriptInterpreter::putDOMObject(void* objectHandle, DOMObject* obj) 
{
    domObjects()->insert(objectHandle, obj);
}

void ScriptInterpreter::deleteDOMObject(void* objectHandle) 
{
    domObjects()->remove(objectHandle);
}

void ScriptInterpreter::forgetDOMObject(void* objectHandle)
{
    deleteDOMObject(objectHandle);
}

DOMNode *ScriptInterpreter::getDOMNodeForDocument(DOM::DocumentImpl *document, DOM::NodeImpl *node)
{
    NodeMap *documentDict = domNodesPerDocument()->get(document);
    if (documentDict)
        return documentDict->get(node);

    return NULL;
}

void ScriptInterpreter::forgetDOMNodeForDocument(DOM::DocumentImpl *document, NodeImpl *node)
{
    NodeMap *documentDict = domNodesPerDocument()->get(document);
    if (documentDict)
        documentDict->remove(node);
}

void ScriptInterpreter::putDOMNodeForDocument(DOM::DocumentImpl *document, NodeImpl *nodeHandle, DOMNode *nodeWrapper)
{
    NodeMap *documentDict = domNodesPerDocument()->get(document);
    if (!documentDict) {
        documentDict = new NodeMap();
        domNodesPerDocument()->insert(document, documentDict);
    }
    documentDict->insert(nodeHandle, nodeWrapper);
}

void ScriptInterpreter::forgetAllDOMNodesForDocument(DOM::DocumentImpl *document)
{
    NodePerDocMap::iterator it = domNodesPerDocument()->find(document);
    if (it != domNodesPerDocument()->end()) {
        delete it->second;
        domNodesPerDocument()->remove(it);
    }
}

void ScriptInterpreter::mark()
{
  NodePerDocMap::iterator dictEnd = domNodesPerDocument()->end();
  for (NodePerDocMap::iterator dictIt = domNodesPerDocument()->begin();
       dictIt != dictEnd;
       ++dictIt) {
    
      NodeMap *nodeDict = dictIt->second;
      NodeMap::iterator nodeEnd = nodeDict->end();
      for (NodeMap::iterator nodeIt = nodeDict->begin();
           nodeIt != nodeEnd;
           ++nodeIt) {

        DOMNode *node = nodeIt->second;
        // don't mark wrappers for nodes that are no longer in the
        // document - they should not be saved if the node is not
        // otherwise reachable from JS.
        if (node->impl()->inDocument() && !node->marked())
            node->mark();
      }
  }
}

void ScriptInterpreter::updateDOMNodeDocument(NodeImpl *node, DOM::DocumentImpl *oldDoc, DOM::DocumentImpl *newDoc)
{
  DOMNode *cachedObject = getDOMNodeForDocument(oldDoc, node);
  if (cachedObject)
      putDOMNodeForDocument(newDoc, node, cachedObject);
}

bool ScriptInterpreter::wasRunByUserGesture() const
{
  if ( m_evt )
  {
    int id = m_evt->id();
    bool eventOk = ( // mouse events
      id == DOM::EventImpl::CLICK_EVENT || id == DOM::EventImpl::MOUSEDOWN_EVENT ||
      id == DOM::EventImpl::MOUSEUP_EVENT || id == DOM::EventImpl::KHTML_DBLCLICK_EVENT ||
      id == DOM::EventImpl::KHTML_CLICK_EVENT ||
      // keyboard events
      id == DOM::EventImpl::KEYDOWN_EVENT || id == DOM::EventImpl::KEYPRESS_EVENT ||
      id == DOM::EventImpl::KEYUP_EVENT ||
      // other accepted events
      id == DOM::EventImpl::SELECT_EVENT || id == DOM::EventImpl::CHANGE_EVENT ||
      id == DOM::EventImpl::FOCUS_EVENT || id == DOM::EventImpl::BLUR_EVENT ||
      id == DOM::EventImpl::SUBMIT_EVENT );
    kdDebug(6070) << "Window.open, smart policy: id=" << id << " eventOk=" << eventOk << endl;
    if (eventOk)
      return true;
  } else // no event
  {
    if ( m_inlineCode  && !m_timerCallback )
    {
      // This is the <a href="javascript:window.open('...')> case -> we let it through
      return true;
      kdDebug(6070) << "Window.open, smart policy, no event, inline code -> ok" << endl;
    }
    else // This is the <script>window.open(...)</script> case or a timer callback -> block it
      kdDebug(6070) << "Window.open, smart policy, no event, <script> tag -> refused" << endl;
  }
  return false;
}

#if APPLE_CHANGES
bool ScriptInterpreter::isGlobalObject(const Value &v)
{
    if (v.type() == ObjectType) {
	Object o = v.toObject (globalExec());
	if (o.classInfo() == &Window::info)
	    return true;
    }
    return false;
}

bool ScriptInterpreter::isSafeScript (const Interpreter *_target)
{
    const KJS::ScriptInterpreter *target = static_cast<const ScriptInterpreter *>(_target);

    return KJS::Window::isSafeScript (this, target);
}

Interpreter *ScriptInterpreter::interpreterForGlobalObject (const ValueImp *imp)
{
    const KJS::Window *win = static_cast<const KJS::Window *>(imp);
    return win->interpreter();
}

void *ScriptInterpreter::createLanguageInstanceForValue (ExecState *exec, Bindings::Instance::BindingLanguage language, const Object &value, const Bindings::RootObject *origin, const Bindings::RootObject *current)
{
    void *result = 0;
    
    if (language == Bindings::Instance::ObjectiveCLanguage)
	result = createObjcInstanceForValue (exec, value, origin, current);
    
    if (!result)
	result = Interpreter::createLanguageInstanceForValue (exec, language, value, origin, current);
	
    return result;
}

#endif

//////

UString::UString(const QString &d)
{
  // reinterpret_cast is ugly but in this case safe, since QChar and UChar have the same
  // memory layout
  rep = UString::Rep::createCopying(reinterpret_cast<const UChar *>(d.unicode()), d.length());
}

UString::UString(const DOMString &d)
{
  if (d.isNull()) {
    attach(&Rep::null);
    return;
  }
  // reinterpret_cast is ugly but in this case safe, since QChar and UChar have the same
  // memory layout
  rep = UString::Rep::createCopying(reinterpret_cast<const UChar *>(d.unicode()), d.length());
}

DOMString UString::string() const
{
  if (isNull())
    return DOMString();
  if (isEmpty())
    return DOMString("");
  return DOMString((QChar*) data(), size());
}

QString UString::qstring() const
{
  if (isNull())
    return QString();
  if (isEmpty())
    return QString("");
  return QString((QChar*) data(), size());
}

QConstString UString::qconststring() const
{
  return QConstString((QChar*) data(), size());
}

DOMString Identifier::string() const
{
  if (isNull())
    return DOMString();
  if (isEmpty())
    return DOMString("");
  return DOMString((QChar*) data(), size());
}

QString Identifier::qstring() const
{
  if (isNull())
    return QString();
  if (isEmpty())
    return QString("");
  return QString((QChar*) data(), size());
}

Value getStringOrNull(DOMString s)
{
  if (s.isNull())
    return Null();
  else
    return String(s);
}

QVariant ValueToVariant(ExecState* exec, const Value &val) {
  QVariant res;
  switch (val.type()) {
  case BooleanType:
    res = QVariant(val.toBoolean(exec), 0);
    break;
  case NumberType:
    res = QVariant(val.toNumber(exec));
    break;
  case StringType:
    res = QVariant(val.toString(exec).qstring());
    break;
  default:
    // everything else will be 'invalid'
    break;
  }
  return res;
}

void setDOMException(ExecState *exec, int DOMExceptionCode)
{
  if (DOMExceptionCode == 0 || exec->hadException())
    return;

  const char *type = "DOM";
  int code = DOMExceptionCode;

  if (code >= RangeException::_EXCEPTION_OFFSET && code <= RangeException::_EXCEPTION_MAX) {
    type = "DOM Range";
    code -= RangeException::_EXCEPTION_OFFSET;
  } else if (code >= CSSException::_EXCEPTION_OFFSET && code <= CSSException::_EXCEPTION_MAX) {
    type = "CSS";
    code -= CSSException::_EXCEPTION_OFFSET;
  }

  char buffer[100]; // needs to fit 20 characters, plus an integer in ASCII, plus a null character
  sprintf(buffer, "%s exception %d", type, code);

  Object errorObject = Error::create(exec, GeneralError, buffer);
  errorObject.put(exec, "code", Number(code));
  exec->setException(errorObject);
}

}
