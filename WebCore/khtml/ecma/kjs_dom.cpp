/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.
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

#include "config.h"
#include "kjs_dom.h"

#include "CDATASection.h"
#include "Comment.h"
#include "DOMImplementation.h"
#include "DocumentFragment.h"
#include "DocumentType.h"
#include "ExceptionCode.h"
#include "EventNames.h"
#include "Frame.h"
#include "JSAttr.h"
#include "JSCharacterData.h"
#include "JSDOMImplementation.h"
#include "JSDocumentType.h"
#include "JSEntity.h"
#include "JSNotation.h"
#include "JSProcessingInstruction.h"
#include "JSRange.h"
#include "JSText.h"
#include "css_ruleimpl.h"
#include "css_stylesheetimpl.h"
#include "dom2_eventsimpl.h"
#include "Range.h"
#include "AbstractView.h"
#include "dom_xmlimpl.h"
#include "HTMLDocument.h"
#include "html_objectimpl.h"
#include "HTMLNames.h"
#include "khtml_settings.h"
#include "kjs_css.h"
#include "kjs_events.h"
#include "kjs_html.h"
#include "kjs_traversal.h"
#include "kjs_views.h"
#include "kjs_window.h"
#include "RenderCanvas.h"

#if __APPLE__
#include <JavaScriptCore/runtime_object.h>
#endif

using namespace WebCore;
using namespace HTMLNames;
using namespace EventNames;

#include "kjs_dom.lut.h"

namespace KJS {

// -------------------------------------------------------------------------
/* Source for DOMNodeProtoTable. Use "make hashtables" to regenerate.
@begin DOMNodeProtoTable 25
  insertBefore  DOMNode::InsertBefore   DontDelete|Function 2
  replaceChild  DOMNode::ReplaceChild   DontDelete|Function 2
  removeChild   DOMNode::RemoveChild    DontDelete|Function 1
  appendChild   DOMNode::AppendChild    DontDelete|Function 1
  hasAttributes DOMNode::HasAttributes  DontDelete|Function 0
  hasChildNodes DOMNode::HasChildNodes  DontDelete|Function 0
  cloneNode     DOMNode::CloneNode      DontDelete|Function 1
# DOM2
  normalize     DOMNode::Normalize      DontDelete|Function 0
  isSupported   DOMNode::IsSupported    DontDelete|Function 2
# DOM3
  isSameNode    DOMNode::IsSameNode     DontDelete|Function 1
  isEqualNode   DOMNode::IsEqualNode    DontDelete|Function 1
  isDefaultNamespace    DOMNode::IsDefaultNamespace DontDelete|Function 1
  lookupNamespaceURI    DOMNode::LookupNamespaceURI DontDelete|Function 1
  lookupPrefix  DOMNode::LookupPrefix   DontDelete|Function 1
# "DOM level 0" (from Gecko DOM reference; also in WinIE)
  item          DOMNode::Item           DontDelete|Function 1
@end
*/
KJS_IMPLEMENT_PROTOFUNC(DOMNodeProtoFunc)
KJS_IMPLEMENT_PROTOTYPE("DOMNode", DOMNodeProto, DOMNodeProtoFunc)

const ClassInfo DOMNode::info = { "Node", 0, &DOMNodeTable, 0 };

DOMNode::DOMNode(ExecState *exec, Node *n)
  : m_impl(n)
{
  setPrototype(DOMNodeProto::self(exec));
}

DOMNode::DOMNode(Node *n)
  : m_impl(n)
{
}

DOMNode::~DOMNode()
{
  ScriptInterpreter::forgetDOMNodeForDocument(m_impl->document(), m_impl.get());
}

void DOMNode::mark()
{
  assert(!marked());

  Node *node = m_impl.get();

  // Nodes in the document are kept alive by ScriptInterpreter::mark,
  // so we have no special responsibilities and can just call the base class here.
  if (node->inDocument()) {
    DOMObject::mark();
    return;
  }

  // This is a node outside the document, so find the root of the tree it is in,
  // and start marking from there.
  Node *root = node;
  for (Node *current = m_impl.get(); current; current = current->parentNode()) {
    root = current;
  }

  static HashSet<Node*> markingRoots;

  // If we're already marking this tree, then we can simply mark this wrapper
  // by calling the base class; our caller is iterating the tree.
  if (markingRoots.contains(root)) {
    DOMObject::mark();
    return;
  }

  // Mark the whole tree; use the global set of roots to avoid reentering.
  markingRoots.add(root);
  for (Node *nodeToMark = root; nodeToMark; nodeToMark = nodeToMark->traverseNextNode()) {
    DOMNode *wrapper = ScriptInterpreter::getDOMNodeForDocument(m_impl->document(), nodeToMark);
    if (wrapper) {
      if (!wrapper->marked())
        wrapper->mark();
    } else if (nodeToMark == node) {
      // This is the case where the map from the document to wrappers has
      // been cleared out, but a wrapper is being marked. For now, we'll
      // let the rest of the tree of wrappers get collected, because we have
      // no good way of finding them. Later we should test behavior of other
      // browsers and see if we need to preserve other wrappers in this case.
      if (!marked())
        mark();
    }
  }
  markingRoots.remove(root);

  // Double check that we actually ended up marked. This assert caught problems in the past.
  assert(marked());
}

bool DOMNode::toBoolean(ExecState *) const
{
    return m_impl;
}

/* Source for DOMNodeTable. Use "make hashtables" to regenerate.
@begin DOMNodeTable 25
  nodeName      DOMNode::NodeName       DontDelete|ReadOnly
  nodeValue     DOMNode::NodeValue      DontDelete
  nodeType      DOMNode::NodeType       DontDelete|ReadOnly
  parentNode    DOMNode::ParentNode     DontDelete|ReadOnly
  parentElement DOMNode::ParentElement  DontDelete|ReadOnly
  childNodes    DOMNode::ChildNodes     DontDelete|ReadOnly
  firstChild    DOMNode::FirstChild     DontDelete|ReadOnly
  lastChild     DOMNode::LastChild      DontDelete|ReadOnly
  previousSibling  DOMNode::PreviousSibling DontDelete|ReadOnly
  nextSibling   DOMNode::NextSibling    DontDelete|ReadOnly
  attributes    DOMNode::Attributes     DontDelete|ReadOnly
  namespaceURI  DOMNode::NamespaceURI   DontDelete|ReadOnly
# DOM2
  prefix        DOMNode::Prefix         DontDelete
  localName     DOMNode::LocalName      DontDelete|ReadOnly
  ownerDocument DOMNode::OwnerDocument  DontDelete|ReadOnly
# DOM3
  textContent   DOMNode::TextContent    DontDelete
@end
*/
bool DOMNode::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  return getStaticValueSlot<DOMNode, DOMObject>(exec, &DOMNodeTable, this, propertyName, slot);
}

JSValue *DOMNode::getValueProperty(ExecState *exec, int token) const
{
  Node &node = *m_impl;
  switch (token) {
  case NodeName:
    return jsStringOrNull(node.nodeName());
  case NodeValue:
    return jsStringOrNull(node.nodeValue());
  case NodeType:
    return jsNumber(node.nodeType());
  case ParentNode:
  case ParentElement: // IE only apparently
    return toJS(exec,node.parentNode());
  case ChildNodes:
    return toJS(exec,node.childNodes().get());
  case FirstChild:
    return toJS(exec,node.firstChild());
  case LastChild:
    return toJS(exec,node.lastChild());
  case PreviousSibling:
    return toJS(exec,node.previousSibling());
  case NextSibling:
    return toJS(exec,node.nextSibling());
  case Attributes:
    return toJS(exec,node.attributes());
  case NamespaceURI:
    return jsStringOrNull(node.namespaceURI());
  case Prefix:
    return jsStringOrNull(node.prefix());
  case LocalName:
    return jsStringOrNull(node.localName());
  case OwnerDocument:
    return toJS(exec,node.ownerDocument());
  case TextContent:
    return jsStringOrNull(node.textContent());
  }

  return jsUndefined();
}

void DOMNode::put(ExecState *exec, const Identifier& propertyName, JSValue *value, int attr)
{
    lookupPut<DOMNode,DOMObject>(exec, propertyName, value, attr, &DOMNodeTable, this);
}

void DOMNode::putValueProperty(ExecState *exec, int token, JSValue *value, int /*attr*/)
{
  DOMExceptionTranslator exception(exec);
  Node &node = *m_impl;
  switch (token) {
  case NodeValue:
    node.setNodeValue(value->toString(exec), exception);
    break;
  case Prefix:
    node.setPrefix(value->toString(exec), exception);
    break;
  case TextContent:
    node.setTextContent(valueToStringWithNullCheck(exec, value), exception);
    break;
  }
}

JSValue *DOMNode::toPrimitive(ExecState *exec, JSType) const
{
  if (!m_impl)
    return jsNull();

  return jsString(toString(exec));
}

UString DOMNode::toString(ExecState *) const
{
  if (!m_impl)
    return "null";
  return "[object " + (m_impl->isElementNode() ? UString(m_impl->nodeName()) : className()) + "]";
}

JSValue *DOMNodeProtoFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
  if (!thisObj->inherits(&DOMNode::info))
    return throwError(exec, TypeError);
  DOMExceptionTranslator exception(exec);
  Node &node = *static_cast<DOMNode *>(thisObj)->impl();
  switch (id) {
    case DOMNode::HasAttributes:
      return jsBoolean(node.hasAttributes());
    case DOMNode::HasChildNodes:
      return jsBoolean(node.hasChildNodes());
    case DOMNode::CloneNode:
      return toJS(exec,node.cloneNode(args[0]->toBoolean(exec)));
    case DOMNode::Normalize:
      node.normalize();
      return jsUndefined();
    case DOMNode::IsSupported:
        return jsBoolean(node.isSupported(args[0]->toString(exec),
                                          valueToStringWithNullCheck(exec, args[1])));
    case DOMNode::IsSameNode:
        return jsBoolean(node.isSameNode(toNode(args[0])));
    case DOMNode::IsEqualNode:
        return jsBoolean(node.isEqualNode(toNode(args[0])));
    case DOMNode::IsDefaultNamespace:
        return jsBoolean(node.isDefaultNamespace(valueToStringWithNullCheck(exec, args[0])));
    case DOMNode::LookupNamespaceURI:
        return jsStringOrNull(node.lookupNamespaceURI(valueToStringWithNullCheck(exec, args[0])));
    case DOMNode::LookupPrefix:
        return jsStringOrNull(node.lookupPrefix(valueToStringWithNullCheck(exec, args[0])));
    case DOMNode::AppendChild:
      if (node.appendChild(toNode(args[0]), exception))
        return args[0];
      return jsNull();
    case DOMNode::RemoveChild:
      if (node.removeChild(toNode(args[0]), exception))
        return args[0];
      return jsNull();
    case DOMNode::InsertBefore:
      if (node.insertBefore(toNode(args[0]), toNode(args[1]), exception))
        return args[0];
      return jsNull();
    case DOMNode::ReplaceChild:
     if (node.replaceChild(toNode(args[0]), toNode(args[1]), exception))
        return args[1];
      return jsNull();
    case DOMNode::Item:
      return thisObj->get(exec, args[0]->toInt32(exec));
  }

  return jsUndefined();
}

EventTargetNode *toEventTargetNode(JSValue *val)
{
    if (!val || !val->isObject(&DOMEventTargetNode::info))
        return 0;
    return static_cast<EventTargetNode*>(static_cast<DOMEventTargetNode *>(val)->impl());
}

Node *toNode(JSValue *val)
{
    if (!val || !val->isObject(&DOMNode::info))
        return 0;
    return static_cast<DOMNode *>(val)->impl();
}

// -------------------------------------------------------------------------

/* Source for DOMEventTargetNodeTable
@begin DOMEventTargetNodeTable 50
onabort       DOMEventTargetNode::OnAbort                DontDelete
onblur        DOMEventTargetNode::OnBlur                 DontDelete
onchange      DOMEventTargetNode::OnChange               DontDelete
onclick       DOMEventTargetNode::OnClick                DontDelete
oncontextmenu DOMEventTargetNode::OnContextMenu          DontDelete
ondblclick    DOMEventTargetNode::OnDblClick             DontDelete
onbeforecut   DOMEventTargetNode::OnBeforeCut            DontDelete
oncut         DOMEventTargetNode::OnCut                  DontDelete
onbeforecopy  DOMEventTargetNode::OnBeforeCopy           DontDelete
oncopy        DOMEventTargetNode::OnCopy                 DontDelete
onbeforepaste DOMEventTargetNode::OnBeforePaste          DontDelete
onpaste       DOMEventTargetNode::OnPaste                DontDelete
ondrag        DOMEventTargetNode::OnDrag                 DontDelete
ondragdrop    DOMEventTargetNode::OnDragDrop             DontDelete
ondragend     DOMEventTargetNode::OnDragEnd              DontDelete
ondragenter   DOMEventTargetNode::OnDragEnter            DontDelete
ondragleave   DOMEventTargetNode::OnDragLeave            DontDelete
ondragover    DOMEventTargetNode::OnDragOver             DontDelete
ondragstart   DOMEventTargetNode::OnDragStart            DontDelete
ondrop        DOMEventTargetNode::OnDrop                 DontDelete
onerror       DOMEventTargetNode::OnError                DontDelete
onfocus       DOMEventTargetNode::OnFocus                DontDelete
oninput       DOMEventTargetNode::OnInput                DontDelete
onkeydown     DOMEventTargetNode::OnKeyDown              DontDelete
onkeypress    DOMEventTargetNode::OnKeyPress             DontDelete
onkeyup       DOMEventTargetNode::OnKeyUp                DontDelete
onload        DOMEventTargetNode::OnLoad                 DontDelete
onmousedown   DOMEventTargetNode::OnMouseDown            DontDelete
onmousemove   DOMEventTargetNode::OnMouseMove            DontDelete
onmouseout    DOMEventTargetNode::OnMouseOut             DontDelete
onmouseover   DOMEventTargetNode::OnMouseOver            DontDelete
onmouseup     DOMEventTargetNode::OnMouseUp              DontDelete
onmousewheel  DOMEventTargetNode::OnMouseWheel           DontDelete
onmove        DOMEventTargetNode::OnMove                 DontDelete
onreset       DOMEventTargetNode::OnReset                DontDelete
onresize      DOMEventTargetNode::OnResize               DontDelete
onscroll      DOMEventTargetNode::OnScroll               DontDelete
onsearch      DOMEventTargetNode::OnSearch               DontDelete
onselect      DOMEventTargetNode::OnSelect               DontDelete
onselectstart DOMEventTargetNode::OnSelectStart          DontDelete
onsubmit      DOMEventTargetNode::OnSubmit               DontDelete
onunload      DOMEventTargetNode::OnUnload               DontDelete
@end
*/

DOMEventTargetNode::DOMEventTargetNode(Node* n)
    : DOMNode(n)
{
}

DOMEventTargetNode::DOMEventTargetNode(ExecState *exec, Node *n)
    : DOMNode(n)
{
    setPrototype(DOMEventTargetNodeProto::self(exec));
}

bool DOMEventTargetNode::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<DOMEventTargetNode, DOMNode>(exec, &DOMEventTargetNodeTable, this, propertyName, slot);
}

JSValue *DOMEventTargetNode::getValueProperty(ExecState *exec, int token) const
{
    switch (token) {
        case OnAbort:
            return getListener(abortEvent);
        case OnBlur:
            return getListener(blurEvent);
        case OnChange:
            return getListener(changeEvent);
        case OnClick:
            return getListener(clickEvent);
        case OnContextMenu:
            return getListener(contextmenuEvent);
        case OnDblClick:
            return getListener(dblclickEvent);
        case OnDragDrop:
            return getListener(khtmlDragdropEvent);
        case OnError:
            return getListener(khtmlErrorEvent);
        case OnFocus:
            return getListener(focusEvent);
        case OnInput:
            return getListener(inputEvent);
        case OnKeyDown:
            return getListener(keydownEvent);
        case OnKeyPress:
            return getListener(keypressEvent);
        case OnKeyUp:
            return getListener(keyupEvent);
        case OnLoad:
            return getListener(loadEvent);
        case OnMouseDown:
            return getListener(mousedownEvent);
        case OnMouseMove:
            return getListener(mousemoveEvent);
        case OnMouseOut:
            return getListener(mouseoutEvent);
        case OnMouseOver:
            return getListener(mouseoverEvent);
        case OnMouseUp:
            return getListener(mouseupEvent);      
        case OnMouseWheel:
            return getListener(mousewheelEvent);      
        case OnBeforeCut:
            return getListener(beforecutEvent);
        case OnCut:
            return getListener(cutEvent);
        case OnBeforeCopy:
            return getListener(beforecopyEvent);
        case OnCopy:
            return getListener(copyEvent);
        case OnBeforePaste:
            return getListener(beforepasteEvent);
        case OnPaste:
            return getListener(pasteEvent);
        case OnDragEnter:
            return getListener(dragenterEvent);
        case OnDragOver:
            return getListener(dragoverEvent);
        case OnDragLeave:
            return getListener(dragleaveEvent);
        case OnDrop:
            return getListener(dropEvent);
        case OnDragStart:
            return getListener(dragstartEvent);
        case OnDrag:
            return getListener(dragEvent);
        case OnDragEnd:
            return getListener(dragendEvent);
        case OnMove:
            return getListener(khtmlMoveEvent);
        case OnReset:
            return getListener(resetEvent);
        case OnResize:
            return getListener(resizeEvent);
        case OnScroll:
            return getListener(scrollEvent);
        case OnSearch:
            return getListener(searchEvent);
        case OnSelect:
            return getListener(selectEvent);
        case OnSelectStart:
            return getListener(selectstartEvent);
        case OnSubmit:
            return getListener(submitEvent);
        case OnUnload:
            return getListener(unloadEvent);
    }
    
    return jsUndefined();
}

void DOMEventTargetNode::put(ExecState *exec, const Identifier& propertyName, JSValue *value, int attr)
{
    lookupPut<DOMEventTargetNode, DOMNode>(exec, propertyName, value, attr, &DOMEventTargetNodeTable, this);
}

void DOMEventTargetNode::putValueProperty(ExecState *exec, int token, JSValue *value, int /*attr*/)
{
    switch (token) {
        case OnAbort:
            setListener(exec, abortEvent, value);
            break;
        case OnBlur:
            setListener(exec, blurEvent, value);
            break;
        case OnChange:
            setListener(exec, changeEvent, value);
            break;
        case OnClick:
            setListener(exec, clickEvent, value);
            break;
        case OnContextMenu:
            setListener(exec, contextmenuEvent, value);
            break;
        case OnDblClick:
            setListener(exec, dblclickEvent, value);
            break;
        case OnDragDrop:
            setListener(exec, khtmlDragdropEvent, value);
            break;
        case OnError:
            setListener(exec, khtmlErrorEvent, value);
            break;
        case OnFocus:
            setListener(exec, focusEvent, value);
            break;
        case OnInput:
            setListener(exec, inputEvent, value);
            break;
        case OnKeyDown:
            setListener(exec, keydownEvent, value);
            break;
        case OnKeyPress:
            setListener(exec, keypressEvent, value);
            break;
        case OnKeyUp:
            setListener(exec, keyupEvent, value);
            break;
        case OnLoad:
            setListener(exec, loadEvent, value);
            break;
        case OnMouseDown:
            setListener(exec, mousedownEvent, value);
            break;
        case OnMouseMove:
            setListener(exec, mousemoveEvent, value);
            break;
        case OnMouseOut:
            setListener(exec, mouseoutEvent, value);
            break;
        case OnMouseOver:
            setListener(exec, mouseoverEvent, value);
            break;
        case OnMouseUp:
            setListener(exec, mouseupEvent, value);
            break;
        case OnMouseWheel:
            setListener(exec, mousewheelEvent, value);
            break;
        case OnBeforeCut:
            setListener(exec, beforecutEvent, value);
            break;
        case OnCut:
            setListener(exec, cutEvent, value);
            break;
        case OnBeforeCopy:
            setListener(exec, beforecopyEvent, value);
            break;
        case OnCopy:
            setListener(exec, copyEvent, value);
            break;
        case OnBeforePaste:
            setListener(exec, beforepasteEvent, value);
            break;
        case OnPaste:
            setListener(exec, pasteEvent, value);
            break;
        case OnDragEnter:
            setListener(exec, dragenterEvent, value);
            break;
        case OnDragOver:
            setListener(exec, dragoverEvent, value);
            break;
        case OnDragLeave:
            setListener(exec, dragleaveEvent, value);
            break;
        case OnDrop:
            setListener(exec, dropEvent, value);
            break;
        case OnDragStart:
            setListener(exec, dragstartEvent, value);
            break;
        case OnDrag:
            setListener(exec, dragEvent, value);
            break;
        case OnDragEnd:
            setListener(exec, dragendEvent, value);
            break;
        case OnMove:
            setListener(exec, khtmlMoveEvent, value);
            break;
        case OnReset:
            setListener(exec, resetEvent, value);
            break;
        case OnResize:
            setListener(exec, resizeEvent, value);
            break;
        case OnScroll:
            setListener(exec, scrollEvent, value);
            break;
        case OnSearch:
            setListener(exec, searchEvent, value);
            break;
        case OnSelect:
            setListener(exec, selectEvent, value);
            break;
        case OnSelectStart:
            setListener(exec, selectstartEvent, value);
            break;
        case OnSubmit:
            setListener(exec, submitEvent, value);
            break;
        case OnUnload:
            setListener(exec, unloadEvent, value);
            break;
    }
}

void DOMEventTargetNode::setListener(ExecState *exec, const AtomicString &eventType, JSValue *func) const
{
    EventTargetNodeCast(impl())->setHTMLEventListener(eventType, Window::retrieveActive(exec)->getJSEventListener(func, true));
}

JSValue *DOMEventTargetNode::getListener(const AtomicString &eventType) const
{
    WebCore::EventListener *listener = EventTargetNodeCast(impl())->getHTMLEventListener(eventType);
    JSEventListener *jsListener = static_cast<JSEventListener*>(listener);
    if (jsListener && jsListener->listenerObj())
        return jsListener->listenerObj();
    else
        return jsNull();
}

void DOMEventTargetNode::pushEventHandlerScope(ExecState *, ScopeChain &) const
{
}

/*
@begin DOMEventTargetNodeProtoTable 5
# from the EventTarget interface
addEventListener        DOMEventTargetNode::AddEventListener   DontDelete|Function 3
removeEventListener     DOMEventTargetNode::RemoveEventListener    DontDelete|Function 3
dispatchEvent           DOMEventTargetNode::DispatchEvent  DontDelete|Function 1
@end
*/

KJS_IMPLEMENT_PROTOFUNC(DOMEventTargetNodeProtoFunc)
KJS_IMPLEMENT_PROTOTYPE("DOMEventTargetNode", DOMEventTargetNodeProto, DOMEventTargetNodeProtoFunc)

JSValue *DOMEventTargetNodeProtoFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
    if (!thisObj->inherits(&DOMEventTargetNode::info))
        return throwError(exec, TypeError);
    DOMExceptionTranslator exception(exec);
    DOMEventTargetNode* DOMNode = static_cast<DOMEventTargetNode*>(thisObj);
    EventTargetNode* node = static_cast<EventTargetNode*>(DOMNode->impl());
    switch (id) {
        case DOMEventTargetNode::AddEventListener: {
            JSEventListener *listener = Window::retrieveActive(exec)->getJSEventListener(args[1]);
            if (listener)
                node->addEventListener(args[0]->toString(exec), listener,args[2]->toBoolean(exec));
            return jsUndefined();
        }
        case DOMEventTargetNode::RemoveEventListener: {
            JSEventListener *listener = Window::retrieveActive(exec)->getJSEventListener(args[1]);
            if (listener)
                node->removeEventListener(args[0]->toString(exec), listener,args[2]->toBoolean(exec));
            return jsUndefined();
        }
        case DOMEventTargetNode::DispatchEvent:
            return jsBoolean(node->dispatchEvent(toEvent(args[0]), exception));
    }
    
    return jsUndefined();
}

// -------------------------------------------------------------------------

/*
@begin DOMNodeListTable 2
  length        DOMNodeList::Length     DontDelete|ReadOnly
  item          DOMNodeList::Item               DontDelete|Function 1
@end
*/

KJS_IMPLEMENT_PROTOFUNC(DOMNodeListFunc)

const ClassInfo DOMNodeList::info = { "NodeList", 0, &DOMNodeListTable, 0 };

DOMNodeList::~DOMNodeList()
{
  ScriptInterpreter::forgetDOMObject(m_impl.get());
}

JSValue *DOMNodeList::toPrimitive(ExecState *exec, JSType) const
{
  if (!m_impl)
    return jsNull();

  return jsString(toString(exec));
}

JSValue *DOMNodeList::getValueProperty(ExecState *exec, int token) const
{
  assert(token == Length);
  return jsNumber(m_impl->length());
}

JSValue *DOMNodeList::indexGetter(ExecState *exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNodeList *thisObj = static_cast<DOMNodeList *>(slot.slotBase());
  return toJS(exec, thisObj->m_impl->item(slot.index()));
}

JSValue *DOMNodeList::nameGetter(ExecState *exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNodeList *thisObj = static_cast<DOMNodeList *>(slot.slotBase());
  return toJS(exec, thisObj->m_impl->itemById(propertyName));
}

bool DOMNodeList::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  const HashEntry* entry = Lookup::findEntry(&DOMNodeListTable, propertyName);

  if (entry) {
    if (entry->attr & Function)
      slot.setStaticEntry(this, entry, staticFunctionGetter<DOMNodeListFunc>);
    else
      slot.setStaticEntry(this, entry, staticValueGetter<DOMNodeList>);
    return true;
  }

  NodeList &list = *m_impl;

  // array index ?
  bool ok;
  unsigned idx = propertyName.toUInt32(&ok);
  if (ok && idx < list.length()) {
    slot.setCustomIndex(this, idx, indexGetter);
    return true;
  } else if (list.itemById(String(propertyName).impl())) {
    slot.setCustom(this, nameGetter);
    return true;
  }

  return DOMObject::getOwnPropertySlot(exec, propertyName, slot);
}

// Need to support both get and call, so that list[0] and list(0) work.
JSValue *DOMNodeList::callAsFunction(ExecState *exec, JSObject *, const List &args)
{
  // Do not use thisObj here. See JSHTMLCollection.
  UString s = args[0]->toString(exec);
  bool ok;
  unsigned int u = s.toUInt32(&ok);
  if (ok)
    return toJS(exec, m_impl->item(u));

  return jsUndefined();
}

// Not a prototype class currently, but should probably be converted to one
JSValue *DOMNodeListFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
  if (!thisObj->inherits(&KJS::DOMNodeList::info))
    return throwError(exec, TypeError);
  WebCore::NodeList &list = *static_cast<DOMNodeList *>(thisObj)->impl();

  if (id == DOMNodeList::Item)
    return toJS(exec, list.item(args[0]->toInt32(exec)));

  return jsUndefined();
}

Attr* toAttr(JSValue* val, bool& ok)
{
    if (!val || !val->isObject(&JSAttr::info)) {
        ok = false;
        return 0;
    }

    ok = true;
    return static_cast<Attr *>(static_cast<DOMNode *>(val)->impl());
}

// -------------------------------------------------------------------------

/* Source for DOMDocumentProtoTable. Use "make hashtables" to regenerate.
@begin DOMDocumentProtoTable 29
  adoptNode       DOMDocument::AdoptNode                       DontDelete|Function 1
  createElement   DOMDocument::CreateElement                   DontDelete|Function 1
  createDocumentFragment DOMDocument::CreateDocumentFragment   DontDelete|Function 1
  createTextNode  DOMDocument::CreateTextNode                  DontDelete|Function 1
  createComment   DOMDocument::CreateComment                   DontDelete|Function 1
  createCDATASection DOMDocument::CreateCDATASection           DontDelete|Function 1
  createProcessingInstruction DOMDocument::CreateProcessingInstruction DontDelete|Function 1
  createAttribute DOMDocument::CreateAttribute                 DontDelete|Function 1
  createEntityReference DOMDocument::CreateEntityReference     DontDelete|Function 1
  elementFromPoint     DOMDocument::ElementFromPoint           DontDelete|Function 1
  getElementsByTagName  DOMDocument::GetElementsByTagName      DontDelete|Function 1
  importNode           DOMDocument::ImportNode                 DontDelete|Function 2
  createElementNS      DOMDocument::CreateElementNS            DontDelete|Function 2
  createAttributeNS    DOMDocument::CreateAttributeNS          DontDelete|Function 2
  getElementsByTagNameNS  DOMDocument::GetElementsByTagNameNS  DontDelete|Function 2
  getElementById     DOMDocument::GetElementById               DontDelete|Function 1
  createRange        DOMDocument::CreateRange                  DontDelete|Function 0
  createNodeIterator DOMDocument::CreateNodeIterator           DontDelete|Function 3
  createTreeWalker   DOMDocument::CreateTreeWalker             DontDelete|Function 4
  createEvent        DOMDocument::CreateEvent                  DontDelete|Function 1
  getOverrideStyle   DOMDocument::GetOverrideStyle             DontDelete|Function 2
  execCommand        DOMDocument::ExecCommand                  DontDelete|Function 3
  queryCommandEnabled DOMDocument::QueryCommandEnabled         DontDelete|Function 1
  queryCommandIndeterm DOMDocument::QueryCommandIndeterm       DontDelete|Function 1
  queryCommandState DOMDocument::QueryCommandState             DontDelete|Function 1
  queryCommandSupported DOMDocument::QueryCommandSupported     DontDelete|Function 1
  queryCommandValue DOMDocument::QueryCommandValue             DontDelete|Function 1
@end
*/
KJS_DEFINE_PROTOTYPE_WITH_PROTOTYPE(DOMDocumentProto, DOMEventTargetNodeProto)
KJS_IMPLEMENT_PROTOFUNC(DOMDocumentProtoFunc)
KJS_IMPLEMENT_PROTOTYPE("DOMDocument", DOMDocumentProto, DOMDocumentProtoFunc)

const ClassInfo DOMDocument::info = { "Document", &DOMEventTargetNode::info, &DOMDocumentTable, 0 };

/* Source for DOMDocumentTable. Use "make hashtables" to regenerate.
@begin DOMDocumentTable 17
  doctype         DOMDocument::DocType                         DontDelete|ReadOnly
  implementation  DOMDocument::Implementation                  DontDelete|ReadOnly
  documentElement DOMDocument::DocumentElement                 DontDelete|ReadOnly
  charset         DOMDocument::Charset                         DontDelete
  defaultCharset  DOMDocument::DefaultCharset                  DontDelete|ReadOnly
  characterSet    DOMDocument::CharacterSet                    DontDelete|ReadOnly
  actualEncoding  DOMDocument::ActualEncoding                  DontDelete|ReadOnly
  inputEncoding   DOMDocument::InputEncoding                   DontDelete|ReadOnly
  styleSheets     DOMDocument::StyleSheets                     DontDelete|ReadOnly
  preferredStylesheetSet  DOMDocument::PreferredStylesheetSet  DontDelete|ReadOnly
  selectedStylesheetSet  DOMDocument::SelectedStylesheetSet    DontDelete
  readyState      DOMDocument::ReadyState                      DontDelete|ReadOnly
  defaultView        DOMDocument::DefaultView                  DontDelete|ReadOnly
@end
*/

DOMDocument::DOMDocument(ExecState *exec, Document *d)
  : DOMEventTargetNode(d) 
{ 
  setPrototype(DOMDocumentProto::self(exec));
}

DOMDocument::DOMDocument(Document *d)
  : DOMEventTargetNode(d) 
{ 
}

DOMDocument::~DOMDocument()
{
  ScriptInterpreter::forgetDOMObject(static_cast<Document *>(m_impl.get()));
}

bool DOMDocument::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  return getStaticValueSlot<DOMDocument, DOMEventTargetNode>(exec, &DOMDocumentTable, this, propertyName, slot);
}

JSValue *DOMDocument::getValueProperty(ExecState *exec, int token) const
{
  Document &doc = *static_cast<Document *>(impl());

  switch(token) {
  case DocType:
    return toJS(exec,doc.doctype());
  case Implementation:
    return toJS(exec, doc.implementation());
  case DocumentElement:
    return toJS(exec,doc.documentElement());
  case Charset:
  case CharacterSet:
  case ActualEncoding:
  case InputEncoding:
    if (Decoder* decoder = doc.decoder())
      return jsString(decoder->encodingName());
    return jsNull();
  case DefaultCharset:
    if (Frame *frame = doc.frame())
        return jsString(frame->settings()->encoding());
    return jsUndefined();
  case StyleSheets:
    return getDOMStyleSheetList(exec, doc.styleSheets(), &doc);
  case PreferredStylesheetSet:
    return jsStringOrNull(doc.preferredStylesheetSet());
  case SelectedStylesheetSet:
    return jsStringOrNull(doc.selectedStylesheetSet());
  case ReadyState:
    if (Frame *frame = doc.frame()) {
      if (frame->isComplete()) return jsString("complete");
      if (doc.parsing()) return jsString("loading");
      return jsString("loaded");
      // What does the interactive value mean ?
      // Missing support for "uninitialized"
    }
    return jsUndefined();
  case DOMDocument::DefaultView: // DOM2
    return toJS(exec,doc.defaultView());
  default:
    return NULL;
  }
}

void DOMDocument::put(ExecState *exec, const Identifier& propertyName, JSValue *value, int attr)
{
    lookupPut<DOMDocument, DOMEventTargetNode>(exec, propertyName, value, attr, &DOMDocumentTable, this);
}

void DOMDocument::putValueProperty(ExecState *exec, int token, JSValue *value, int)
{
  Document &doc = *static_cast<Document *>(impl());
  switch (token) {
    case SelectedStylesheetSet:
      doc.setSelectedStylesheetSet(value->toString(exec));
      break;
    case Charset:
      doc.decoder()->setEncodingName(value->toString(exec).cstring().c_str(), Decoder::UserChosenEncoding);
      break;
  }
}

JSValue *DOMDocumentProtoFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
  if (!thisObj->inherits(&KJS::DOMDocument::info))
    return throwError(exec, TypeError);
  DOMExceptionTranslator exception(exec);
  Node &node = *static_cast<DOMNode *>(thisObj)->impl();
  Document &doc = static_cast<Document &>(node);
  UString str = args[0]->toString(exec);
  WebCore::String s = str;

  switch(id) {
  case DOMDocument::AdoptNode:
    return toJS(exec,doc.adoptNode(toNode(args[0]),exception));
  case DOMDocument::CreateElement:
    return toJS(exec,doc.createElement(s, exception));
  case DOMDocument::CreateDocumentFragment:
    return toJS(exec,doc.createDocumentFragment());
  case DOMDocument::CreateTextNode:
    return toJS(exec,doc.createTextNode(s));
  case DOMDocument::CreateComment:
    return toJS(exec,doc.createComment(s));
  case DOMDocument::CreateCDATASection:
    return toJS(exec, doc.createCDATASection(s, exception));
  case DOMDocument::CreateProcessingInstruction:
    return toJS(exec, doc.createProcessingInstruction(args[0]->toString(exec), args[1]->toString(exec), exception));
  case DOMDocument::CreateAttribute:
    return toJS(exec,doc.createAttribute(s, exception));
  case DOMDocument::CreateEntityReference:
    return toJS(exec, doc.createEntityReference(s, exception));
  case DOMDocument::ElementFromPoint:
    return toJS(exec,doc.elementFromPoint((int)args[0]->toNumber(exec), (int)args[1]->toNumber(exec)));
  case DOMDocument::GetElementsByTagName:
    return toJS(exec,doc.getElementsByTagName(s).get());
  case DOMDocument::ImportNode: // DOM2
    return toJS(exec,doc.importNode(toNode(args[0]), args[1]->toBoolean(exec), exception));
  case DOMDocument::CreateElementNS: // DOM2
    return toJS(exec,doc.createElementNS(s, args[1]->toString(exec), exception));
  case DOMDocument::CreateAttributeNS: // DOM2
    return toJS(exec,doc.createAttributeNS(s, args[1]->toString(exec), exception));
  case DOMDocument::GetElementsByTagNameNS: // DOM2
    return toJS(exec,doc.getElementsByTagNameNS(s, args[1]->toString(exec)).get());
  case DOMDocument::GetElementById:
    return toJS(exec,doc.getElementById(args[0]->toString(exec)));
  case DOMDocument::CreateRange:
    return toJS(exec, doc.createRange().get());
  case DOMDocument::CreateNodeIterator: {
    RefPtr<NodeFilter> filter;
    JSValue* arg2 = args[2];
    if (arg2->isObject()) {
      JSObject* o(static_cast<JSObject*>(arg2));
      filter = new NodeFilter(new JSNodeFilterCondition(o));
    }
    return toJS(exec, doc.createNodeIterator(toNode(args[0]), args[1]->toUInt32(exec),
        filter.release(), args[3]->toBoolean(exec), exception).get());
  }
  case DOMDocument::CreateTreeWalker: {
    RefPtr<NodeFilter> filter;
    JSValue* arg2 = args[2];
    if (arg2->isObject()) {
      JSObject* o(static_cast<JSObject *>(arg2));
      filter = new NodeFilter(new JSNodeFilterCondition(o));
    }
    return toJS(exec, doc.createTreeWalker(toNode(args[0]), args[1]->toUInt32(exec),
        filter.release(), args[3]->toBoolean(exec), exception).get());
  }
  case DOMDocument::CreateEvent:
    return toJS(exec, doc.createEvent(s, exception).get());
  case DOMDocument::GetOverrideStyle:
    if (Element *element0 = toElement(args[0]))
        return toJS(exec,doc.getOverrideStyle(element0, args[1]->toString(exec)));
    // FIXME: Is undefined right here, or should we raise an exception?
    return jsUndefined();
  case DOMDocument::ExecCommand: {
    return jsBoolean(doc.execCommand(args[0]->toString(exec), args[1]->toBoolean(exec), args[2]->toString(exec)));
  }
  case DOMDocument::QueryCommandEnabled: {
    return jsBoolean(doc.queryCommandEnabled(args[0]->toString(exec)));
  }
  case DOMDocument::QueryCommandIndeterm: {
    return jsBoolean(doc.queryCommandIndeterm(args[0]->toString(exec)));
  }
  case DOMDocument::QueryCommandState: {
    return jsBoolean(doc.queryCommandState(args[0]->toString(exec)));
  }
  case DOMDocument::QueryCommandSupported: {
    return jsBoolean(doc.queryCommandSupported(args[0]->toString(exec)));
  }
  case DOMDocument::QueryCommandValue: {
    WebCore::String commandValue(doc.queryCommandValue(args[0]->toString(exec)));
    // Method returns null String to signal command is unsupported.
    // Microsoft documentation for this method says:
    // "If not supported [for a command identifier], this method returns a Boolean set to false."
    if (commandValue.isNull())
        return jsBoolean(false);
    else 
        return jsString(commandValue);
  }
  default:
    break;
  }

  return jsUndefined();
}

// -------------------------------------------------------------------------

/* Source for DOMElementProtoTable. Use "make hashtables" to regenerate.
@begin DOMElementProtoTable 8
  scrollIntoView                DOMElement::ScrollIntoView      DontDelete|Function 1
  scrollIntoViewIfNeeded        DOMElement::ScrollIntoViewIfNeeded      DontDelete|Function 1

# extension for Safari RSS
  scrollByLines         DOMElement::ScrollByLines       DontDelete|Function 1
  scrollByPages         DOMElement::ScrollByPages       DontDelete|Function 1
@end
*/
KJS_IMPLEMENT_PROTOFUNC(DOMElementProtoFunc)
KJS_IMPLEMENT_PROTOTYPE("DOMElement", DOMElementProto, DOMElementProtoFunc)

const ClassInfo DOMElement::info = { "Element", &DOMEventTargetNode::info, &DOMElementTable, 0 };
/* Source for DOMElementTable. Use "make hashtables" to regenerate.
@begin DOMElementTable 17
    tagName       DOMElement::TagName                         DontDelete|ReadOnly

# IE extensions
    offsetLeft    DOMElement::OffsetLeft             DontDelete|ReadOnly
    offsetTop     DOMElement::OffsetTop              DontDelete|ReadOnly
    offsetWidth   DOMElement::OffsetWidth            DontDelete|ReadOnly
    offsetHeight  DOMElement::OffsetHeight           DontDelete|ReadOnly
    offsetParent  DOMElement::OffsetParent           DontDelete|ReadOnly
    clientWidth   DOMElement::ClientWidth            DontDelete|ReadOnly
    clientHeight  DOMElement::ClientHeight           DontDelete|ReadOnly
    scrollLeft    DOMElement::ScrollLeft             DontDelete
    scrollTop     DOMElement::ScrollTop              DontDelete
    scrollWidth   DOMElement::ScrollWidth            DontDelete|ReadOnly
    scrollHeight  DOMElement::ScrollHeight           DontDelete|ReadOnly
@end
*/
DOMElement::DOMElement(ExecState *exec, Element *e)
  : DOMEventTargetNode(e) 
{
  setPrototype(DOMElementProto::self(exec));
}

DOMElement::DOMElement(Element *e)
  : DOMEventTargetNode(e) 
{ 
}

JSValue *DOMElement::getValueProperty(ExecState *exec, int token) const
{
    Element *element = static_cast<Element *>(impl());
    switch (token) {
    case TagName:
        return jsStringOrNull(element->nodeName());

    default:
        // no DOM standard -- IE extension
        
        // Make sure our layout is up to date before we allow a query on these attributes.
        element->document()->updateLayoutIgnorePendingStylesheets();
        
        RenderObject *rend = element->renderer();
        
        switch (token) {
            case OffsetLeft:
                return rend ? jsNumber(rend->offsetLeft()) : static_cast<JSValue *>(jsUndefined());
            case OffsetTop:
                return rend ? jsNumber(rend->offsetTop()) : static_cast<JSValue *>(jsUndefined());
            case OffsetWidth:
                return rend ? jsNumber(rend->offsetWidth()) : static_cast<JSValue *>(jsUndefined());
            case OffsetHeight:
                return rend ? jsNumber(rend->offsetHeight()) : static_cast<JSValue *>(jsUndefined());
            case OffsetParent: {
                RenderObject* par = rend ? rend->offsetParent() : 0;
                return toJS(exec, par ? par->element() : 0);
            }
            case ClientWidth:
                return rend ? jsNumber(rend->clientWidth()) : static_cast<JSValue *>(jsUndefined());
            case ClientHeight:
                return rend ? jsNumber(rend->clientHeight()) : static_cast<JSValue *>(jsUndefined());
            case ScrollWidth:
                return rend ? jsNumber(rend->scrollWidth()) : static_cast<JSValue *>(jsUndefined());
            case ScrollHeight:
                return rend ? jsNumber(rend->scrollHeight()) : static_cast<JSValue *>(jsUndefined());
            case ScrollLeft:
                return jsNumber(rend && rend->layer() ? rend->layer()->scrollXOffset() : 0);
            case ScrollTop:
                return jsNumber(rend && rend->layer() ? rend->layer()->scrollYOffset() : 0);
        }
    }

    return jsUndefined();
}

void DOMElement::put(ExecState *exec, const Identifier& propertyName, JSValue *value, int attr)
{
    lookupPut<DOMElement, DOMEventTargetNode>(exec, propertyName, value, attr, &DOMElementTable, this);
}

void DOMElement::putValueProperty(ExecState *exec, int token, JSValue *value, int /*attr*/)
{
    Node &node = *m_impl;
    switch (token) {
        case ScrollTop: {
            RenderObject *rend = node.renderer();
            if (rend && rend->hasOverflowClip())
                rend->layer()->scrollToYOffset(value->toInt32(exec));
            break;
        }
        case ScrollLeft: {
            RenderObject *rend = node.renderer();
            if (rend && rend->hasOverflowClip())
                rend->layer()->scrollToXOffset(value->toInt32(exec));
            break;
        }
    }
}

JSValue* DOMElement::attributeGetter(ExecState* exec, JSObject* originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMElement* thisObj = static_cast<DOMElement*>(slot.slotBase());
  return jsStringOrNull(static_cast<Element*>(thisObj->impl())->getAttributeNS(nullAtom, propertyName));
}

bool DOMElement::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
  const HashEntry* entry = Lookup::findEntry(&DOMElementTable, propertyName);
  if (entry) {
    slot.setStaticEntry(this, entry, staticValueGetter<DOMElement>);
    return true;
  }

  // We have to check in DOMEventTargetNode before giving access to attributes, otherwise
  // onload="..." would make onload return the string (attribute value) instead of
  // the listener object (function).
  if (DOMEventTargetNode::getOwnPropertySlot(exec, propertyName, slot))
    return true;

  JSValue* proto = prototype();
  if (proto->isObject() && static_cast<JSObject *>(proto)->hasProperty(exec, propertyName))
    return false;

  // FIXME: do we really want to do this attribute lookup thing? Mozilla doesn't do it,
  // and it seems like it could interfere with XBL.
  WebCore::String attr = static_cast<Element*>(impl())->getAttributeNS(nullAtom, propertyName);
  if (!attr.isNull()) {
    slot.setCustom(this, attributeGetter);
    return true;
  }

  return false;
}

JSValue *DOMElementProtoFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
  if (!thisObj->inherits(&KJS::DOMElement::info))
    return throwError(exec, TypeError);
  DOMExceptionTranslator exception(exec);
  Node &node = *static_cast<DOMNode *>(thisObj)->impl();
  Element &element = static_cast<Element &>(node);

  switch(id) {
      case DOMElement::ScrollIntoView: 
        element.scrollIntoView(args[0]->isUndefinedOrNull() || args[0]->toBoolean(exec));
        return jsUndefined();
      case DOMElement::ScrollIntoViewIfNeeded: 
        element.scrollIntoViewIfNeeded(args[0]->isUndefinedOrNull() || args[0]->toBoolean(exec));
        return jsUndefined();
      case DOMElement::ScrollByLines:
      case DOMElement::ScrollByPages:
        element.document()->updateLayoutIgnorePendingStylesheets();
        if (RenderObject *rend = element.renderer())
          if (rend->hasOverflowClip()) {
            KWQScrollDirection direction = KWQScrollDown;
            int multiplier = args[0]->toInt32(exec);
            if (multiplier < 0) {
                direction = KWQScrollUp;
                multiplier = -multiplier;
            }
            KWQScrollGranularity granularity = id == DOMElement::ScrollByLines ? KWQScrollLine : KWQScrollPage;
            rend->layer()->scroll(direction, granularity, multiplier);
          }
        return jsUndefined();
    }

    return jsUndefined();
}

Element *toElement(JSValue *val)
{
    if (!val || !val->isObject(&DOMElement::info))
        return 0;
    return static_cast<Element *>(static_cast<DOMElement *>(val)->impl());
}

DocumentType *toDocumentType(JSValue *val)
{
    if (!val || !val->isObject(&JSDocumentType::info))
        return 0;
    return static_cast<DocumentType *>(static_cast<DOMNode *>(val)->impl());
}

// -------------------------------------------------------------------------

/* Source for DOMNamedNodeMapProtoTable. Use "make hashtables" to regenerate.
@begin DOMNamedNodeMapProtoTable 10
  getNamedItem          DOMNamedNodeMap::GetNamedItem           DontDelete|Function 1
  setNamedItem          DOMNamedNodeMap::SetNamedItem           DontDelete|Function 1
  removeNamedItem       DOMNamedNodeMap::RemoveNamedItem        DontDelete|Function 1
  item                  DOMNamedNodeMap::Item                   DontDelete|Function 1
# DOM2
  getNamedItemNS        DOMNamedNodeMap::GetNamedItemNS         DontDelete|Function 2
  setNamedItemNS        DOMNamedNodeMap::SetNamedItemNS         DontDelete|Function 1
  removeNamedItemNS     DOMNamedNodeMap::RemoveNamedItemNS      DontDelete|Function 2
@end
*/
KJS_DEFINE_PROTOTYPE(DOMNamedNodeMapProto)
KJS_IMPLEMENT_PROTOFUNC(DOMNamedNodeMapProtoFunc)
KJS_IMPLEMENT_PROTOTYPE("NamedNodeMap",DOMNamedNodeMapProto,DOMNamedNodeMapProtoFunc)

const ClassInfo DOMNamedNodeMap::info = { "NamedNodeMap", 0, 0, 0 };

DOMNamedNodeMap::DOMNamedNodeMap(ExecState *exec, NamedNodeMap *m)
  : m_impl(m) 
{ 
  setPrototype(DOMNamedNodeMapProto::self(exec));
}

DOMNamedNodeMap::~DOMNamedNodeMap()
{
  ScriptInterpreter::forgetDOMObject(m_impl.get());
}

JSValue *DOMNamedNodeMap::lengthGetter(ExecState* exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNamedNodeMap *thisObj = static_cast<DOMNamedNodeMap *>(slot.slotBase());
  return jsNumber(thisObj->m_impl->length());
}

JSValue *DOMNamedNodeMap::indexGetter(ExecState* exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNamedNodeMap *thisObj = static_cast<DOMNamedNodeMap *>(slot.slotBase());
  return toJS(exec, thisObj->m_impl->item(slot.index()));
}

JSValue *DOMNamedNodeMap::nameGetter(ExecState *exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNamedNodeMap *thisObj = static_cast<DOMNamedNodeMap *>(slot.slotBase());
  return toJS(exec, thisObj->m_impl->getNamedItem(propertyName));
}

bool DOMNamedNodeMap::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
  if (propertyName == lengthPropertyName) {
      slot.setCustom(this, lengthGetter);
      return true;
  } else {
    // Look in the prototype (for functions) before assuming it's an item's name
    JSValue *proto = prototype();
    if (proto->isObject() && static_cast<JSObject *>(proto)->hasProperty(exec, propertyName))
      return false;

    // name or index ?
    bool ok;
    unsigned idx = propertyName.toUInt32(&ok);
    if (ok && idx < m_impl->length()) {
      slot.setCustomIndex(this, idx, indexGetter);
      return true;
    }

    if (m_impl->getNamedItem(propertyName)) {
      slot.setCustom(this, nameGetter);
      return true;
    }
  }

  return DOMObject::getOwnPropertySlot(exec, propertyName, slot);
}

JSValue *DOMNamedNodeMapProtoFunc::callAsFunction(ExecState *exec, JSObject *thisObj, const List &args)
{
  if (!thisObj->inherits(&KJS::DOMNamedNodeMap::info))
    return throwError(exec, TypeError);
  DOMExceptionTranslator exception(exec);
  NamedNodeMap &map = *static_cast<DOMNamedNodeMap *>(thisObj)->impl();
  switch (id) {
    case DOMNamedNodeMap::GetNamedItem:
      return toJS(exec, map.getNamedItem(args[0]->toString(exec)));
    case DOMNamedNodeMap::SetNamedItem:
      return toJS(exec, map.setNamedItem(toNode(args[0]), exception).get());
    case DOMNamedNodeMap::RemoveNamedItem:
      return toJS(exec, map.removeNamedItem(args[0]->toString(exec), exception).get());
    case DOMNamedNodeMap::Item:
      return toJS(exec, map.item(args[0]->toInt32(exec)));
    case DOMNamedNodeMap::GetNamedItemNS: // DOM2
      return toJS(exec, map.getNamedItemNS(valueToStringWithNullCheck(exec, args[0]), args[1]->toString(exec)));
    case DOMNamedNodeMap::SetNamedItemNS: // DOM2
      return toJS(exec, map.setNamedItemNS(toNode(args[0]), exception).get());
    case DOMNamedNodeMap::RemoveNamedItemNS: // DOM2
      return toJS(exec, map.removeNamedItemNS(valueToStringWithNullCheck(exec, args[0]), args[1]->toString(exec), exception).get());
  }
  return jsUndefined();
}

// -------------------------------------------------------------------------

JSValue* toJS(ExecState *exec, Document *n)
{
  if (!n)
    return jsNull();

  DOMDocument *ret = 0;
  ScriptInterpreter* interp = static_cast<ScriptInterpreter *>(exec->dynamicInterpreter());

  if ((ret = static_cast<DOMDocument *>(interp->getDOMObject(n))))
    return ret;

  if (n->isHTMLDocument())
    ret = new JSHTMLDocument(exec, static_cast<HTMLDocument *>(n));
  else
    ret = new DOMDocument(exec, n);

  // Make sure the document is kept around by the window object, and works right with the
  // back/forward cache.
  if (n->frame())
    Window::retrieveWindow(n->frame())->putDirect("document", ret, DontDelete|ReadOnly);

  interp->putDOMObject(n, ret);

  return ret;
}

bool checkNodeSecurity(ExecState *exec, Node *n)
{
  if (!n) 
    return false;

  // Check to see if the currently executing interpreter is allowed to access the specified node
  Window *win = Window::retrieveWindow(n->document()->frame());
  return win && win->isSafeScript(exec);
}

JSValue *toJS(ExecState *exec, PassRefPtr<Node> node)
{
  Node* n = node.get();
  DOMNode *ret = 0;
  if (!n)
    return jsNull();
  ScriptInterpreter* interp = static_cast<ScriptInterpreter *>(exec->dynamicInterpreter());
  Document *doc = n->document();

  if ((ret = interp->getDOMNodeForDocument(doc, n)))
    return ret;

  switch (n->nodeType()) {
    case WebCore::Node::ELEMENT_NODE:
      if (n->isHTMLElement())
        ret = new JSHTMLElement(exec, static_cast<HTMLElement *>(n));
      else
        ret = new JSElement(exec, static_cast<Element *>(n));
      break;
    case WebCore::Node::ATTRIBUTE_NODE:
      ret = new JSAttr(exec, static_cast<Attr *>(n));
      break;
    case WebCore::Node::TEXT_NODE:
    case WebCore::Node::CDATA_SECTION_NODE:
      ret = new JSText(exec, static_cast<Text *>(n));
      break;
    case WebCore::Node::ENTITY_NODE:
      ret = new JSEntity(exec, static_cast<Entity *>(n));
      break;
    case WebCore::Node::PROCESSING_INSTRUCTION_NODE:
      ret = new JSProcessingInstruction(exec, static_cast<ProcessingInstruction *>(n));
      break;
    case WebCore::Node::COMMENT_NODE:
      ret = new JSCharacterData(exec, static_cast<CharacterData *>(n));
      break;
    case WebCore::Node::DOCUMENT_NODE:
      // we don't want to cache the document itself in the per-document dictionary
      return toJS(exec, static_cast<Document *>(n));
    case WebCore::Node::DOCUMENT_TYPE_NODE:
      ret = new JSDocumentType(exec, static_cast<DocumentType *>(n));
      break;
    case WebCore::Node::NOTATION_NODE:
      ret = new JSNotation(exec, static_cast<Notation *>(n));
      break;
    case WebCore::Node::DOCUMENT_FRAGMENT_NODE:
    case WebCore::Node::ENTITY_REFERENCE_NODE:
    default:
      ret = new DOMNode(exec, n);
  }

  interp->putDOMNodeForDocument(doc, n, ret);

  return ret;
}

JSValue *toJS(ExecState *exec, NamedNodeMap *m)
{
  return cacheDOMObject<NamedNodeMap, DOMNamedNodeMap>(exec, m);
}

JSValue *getRuntimeObject(ExecState *exec, Node *n)
{
    if (!n)
        return 0;

#if __APPLE__
    if (n->hasTagName(appletTag)) {
        HTMLAppletElement *appletElement = static_cast<HTMLAppletElement *>(n);
        if (appletElement->getAppletInstance())
            // The instance is owned by the applet element.
            return new RuntimeObjectImp(appletElement->getAppletInstance());
    }
    else if (n->hasTagName(embedTag)) {
        HTMLEmbedElement *embedElement = static_cast<HTMLEmbedElement *>(n);
        if (embedElement->getEmbedInstance())
            return new RuntimeObjectImp(embedElement->getEmbedInstance());
    }
    else if (n->hasTagName(objectTag)) {
        HTMLObjectElement *objectElement = static_cast<HTMLObjectElement *>(n);
        if (objectElement->getObjectInstance())
            return new RuntimeObjectImp(objectElement->getObjectInstance());
    }
#endif

    // If we don't have a runtime object return 0.
    return 0;
}

JSValue *toJS(ExecState *exec, PassRefPtr<NodeList> l)
{
  return cacheDOMObject<NodeList, DOMNodeList>(exec, l.get());
}

// -------------------------------------------------------------------------

const ClassInfo NodeConstructor::info = { "NodeConstructor", 0, &NodeConstructorTable, 0 };
/* Source for NodeConstructorTable. Use "make hashtables" to regenerate.
@begin NodeConstructorTable 11
  ELEMENT_NODE          WebCore::Node::ELEMENT_NODE         DontDelete|ReadOnly
  ATTRIBUTE_NODE        WebCore::Node::ATTRIBUTE_NODE               DontDelete|ReadOnly
  TEXT_NODE             WebCore::Node::TEXT_NODE            DontDelete|ReadOnly
  CDATA_SECTION_NODE    WebCore::Node::CDATA_SECTION_NODE   DontDelete|ReadOnly
  ENTITY_REFERENCE_NODE WebCore::Node::ENTITY_REFERENCE_NODE        DontDelete|ReadOnly
  ENTITY_NODE           WebCore::Node::ENTITY_NODE          DontDelete|ReadOnly
  PROCESSING_INSTRUCTION_NODE WebCore::Node::PROCESSING_INSTRUCTION_NODE DontDelete|ReadOnly
  COMMENT_NODE          WebCore::Node::COMMENT_NODE         DontDelete|ReadOnly
  DOCUMENT_NODE         WebCore::Node::DOCUMENT_NODE                DontDelete|ReadOnly
  DOCUMENT_TYPE_NODE    WebCore::Node::DOCUMENT_TYPE_NODE   DontDelete|ReadOnly
  DOCUMENT_FRAGMENT_NODE WebCore::Node::DOCUMENT_FRAGMENT_NODE      DontDelete|ReadOnly
  NOTATION_NODE         WebCore::Node::NOTATION_NODE                DontDelete|ReadOnly
@end
*/
bool NodeConstructor::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  return getStaticValueSlot<NodeConstructor, DOMObject>(exec, &NodeConstructorTable, this, propertyName, slot);
}

JSValue *NodeConstructor::getValueProperty(ExecState *, int token) const
{
  // We use the token as the value to return directly
  return jsNumber(token);
}

JSObject *getNodeConstructor(ExecState *exec)
{
  return cacheGlobalObject<NodeConstructor>(exec, "[[node.constructor]]");
}

// -------------------------------------------------------------------------

const ClassInfo DOMExceptionConstructor::info = { "DOMExceptionConstructor", 0, 0, 0 };

/* Source for DOMExceptionConstructorTable. Use "make hashtables" to regenerate.
@begin DOMExceptionConstructorTable 15
  INDEX_SIZE_ERR                WebCore::INDEX_SIZE_ERR               DontDelete|ReadOnly
  DOMSTRING_SIZE_ERR            WebCore::DOMSTRING_SIZE_ERR   DontDelete|ReadOnly
  HIERARCHY_REQUEST_ERR         WebCore::HIERARCHY_REQUEST_ERR        DontDelete|ReadOnly
  WRONG_DOCUMENT_ERR            WebCore::WRONG_DOCUMENT_ERR   DontDelete|ReadOnly
  INVALID_CHARACTER_ERR         WebCore::INVALID_CHARACTER_ERR        DontDelete|ReadOnly
  NO_DATA_ALLOWED_ERR           WebCore::NO_DATA_ALLOWED_ERR  DontDelete|ReadOnly
  NO_MODIFICATION_ALLOWED_ERR   WebCore::NO_MODIFICATION_ALLOWED_ERR  DontDelete|ReadOnly
  NOT_FOUND_ERR                 WebCore::NOT_FOUND_ERR                DontDelete|ReadOnly
  NOT_SUPPORTED_ERR             WebCore::NOT_SUPPORTED_ERR    DontDelete|ReadOnly
  INUSE_ATTRIBUTE_ERR           WebCore::INUSE_ATTRIBUTE_ERR  DontDelete|ReadOnly
  INVALID_STATE_ERR             WebCore::INVALID_STATE_ERR    DontDelete|ReadOnly
  SYNTAX_ERR                    WebCore::SYNTAX_ERR           DontDelete|ReadOnly
  INVALID_MODIFICATION_ERR      WebCore::INVALID_MODIFICATION_ERR     DontDelete|ReadOnly
  NAMESPACE_ERR                 WebCore::NAMESPACE_ERR                DontDelete|ReadOnly
  INVALID_ACCESS_ERR            WebCore::INVALID_ACCESS_ERR   DontDelete|ReadOnly
@end
*/

bool DOMExceptionConstructor::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  return getStaticValueSlot<DOMExceptionConstructor, DOMObject>(exec, &DOMExceptionConstructorTable, this, propertyName, slot);
}

JSValue *DOMExceptionConstructor::getValueProperty(ExecState *, int token) const
{
  // We use the token as the value to return directly
  return jsNumber(token);
}

JSObject *getDOMExceptionConstructor(ExecState *exec)
{
  return cacheGlobalObject<DOMExceptionConstructor>(exec, "[[DOMException.constructor]]");
}

// -------------------------------------------------------------------------

// Such a collection is usually very short-lived, it only exists
// for constructs like document.forms.<name>[1],
// so it shouldn't be a problem that it's storing all the nodes (with the same name). (David)
DOMNamedNodesCollection::DOMNamedNodesCollection(ExecState *, const DeprecatedValueList< RefPtr<Node> >& nodes )
  : m_nodes(nodes)
{
}

JSValue *DOMNamedNodesCollection::lengthGetter(ExecState* exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNamedNodesCollection *thisObj = static_cast<DOMNamedNodesCollection *>(slot.slotBase());
  return jsNumber(thisObj->m_nodes.count());
}

JSValue *DOMNamedNodesCollection::indexGetter(ExecState* exec, JSObject *originalObject, const Identifier& propertyName, const PropertySlot& slot)
{
  DOMNamedNodesCollection *thisObj = static_cast<DOMNamedNodesCollection *>(slot.slotBase());
  return toJS(exec, thisObj->m_nodes[slot.index()].get());
}

bool DOMNamedNodesCollection::getOwnPropertySlot(ExecState *exec, const Identifier& propertyName, PropertySlot& slot)
{
  if (propertyName == lengthPropertyName) {
    slot.setCustom(this, lengthGetter);
    return true;
  }

  // array index ?
  bool ok;
  unsigned idx = propertyName.toUInt32(&ok);
  if (ok && idx < m_nodes.count()) {
    slot.setCustomIndex(this, idx, indexGetter);
    return true;
  }

  // For IE compatibility, we need to be able to look up elements in a
  // document.formName.name result by id as well as be index.

  AtomicString atomicPropertyName = propertyName;
  DeprecatedValueListConstIterator< RefPtr<Node> > end = m_nodes.end();
  int i = 0;
  for (DeprecatedValueListConstIterator< RefPtr<Node> > it = m_nodes.begin(); it != end; ++it, ++i) {
    Node *node = (*it).get();
    if (node->hasAttributes() && node->attributes()->id() == atomicPropertyName) {
      slot.setCustomIndex(this, i, indexGetter);
      return true;
    }
  }

  return DOMObject::getOwnPropertySlot(exec, propertyName, slot);
}

// -------------------------------------------------------------------------


} // namespace
