// -*- c-basic-offset: 2 -*-
/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2003, 2006, 2007 Apple Inc. All rights reserved.
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef KJS_lookup_h
#define KJS_lookup_h

#include "ExecState.h"
#include "function.h"
#include "identifier.h"
#include "JSGlobalObject.h"
#include "object.h"
#include <stdio.h>
#include <wtf/Assertions.h>

namespace KJS {

  /**
   * An entry in a hash table.
   */
  struct HashEntry {
    typedef InternalFunctionImp* (*ConstructFunctionObject)(ExecState*, int, const Identifier&);

    /**
     * s is the key (e.g. a property name)
     */
    const char* s;

    /**
     * value is the result value (enum value for properties and a function pointer to a constructor factory for functions)
     */
    union {
      intptr_t intValue;
      ConstructFunctionObject functionValue;
    } value;

    /**
     * attr is a set for flags (e.g. the property flags, see object.h)
     */
    unsigned char attr;
    /**
     * params is another number. For property hashtables, it is used to
     * denote the number of argument of the function
     */
    short int params;
    /**
     * next is the pointer to the next entry for the same hash value
     */
    const HashEntry* next;
  };

  /**
   * A hash table
   * Usually the hashtable is generated by the create_hash_table script, from a .table file.
   *
   * The implementation uses an array of entries, "size" is the total size of that array.
   * The entries between 0 and hashSize-1 are the entry points
   * for each hash value, and the entries between hashSize and size-1
   * are the overflow entries for the hash values that need one.
   * The "next" pointer of the entry links entry points to overflow entries,
   * and links overflow entries between them.
   */
  struct HashTable {
    /**
     * type is a version number. Currently always 2
     */
    int type;
    /**
     * size is the total number of entries in the hashtable, including the null entries,
     * i.e. the size of the "entries" array.
     * Used to iterate over all entries in the table
     */
    int size;
    /**
     * pointer to the array of entries
     * Mind that some entries in the array are null (0,0,0,0).
     */
    const HashEntry* entries;
    /**
     * the maximum value for the hash minus 1. Always smaller than size.
     */
    int hashSizeMask;
  };

  /**
   * @short Fast keyword lookup.
   */
  class Lookup {
  public:
    /**
     * Find an entry in the table, and return its value (i.e. the value field of HashEntry)
     */
    static int find(const struct HashTable*, const Identifier&);
    static int find(const struct HashTable*, const UChar*, unsigned int len);

    /**
     * Find an entry in the table, and return the entry
     * This variant gives access to the other attributes of the entry,
     * especially the attr field.
     */
    static const HashEntry* findEntry(const struct HashTable*, const Identifier&);

  };

  class ExecState;
  class UString;
  /**
   * @internal
   * Helper for getStaticFunctionSlot and getStaticPropertySlot
   */
  inline JSValue* staticFunctionGetter(ExecState* exec, JSObject*, const Identifier& propertyName, const PropertySlot& slot)
  {
      // Look for cached value in dynamic map of properties (in JSObject)
      JSObject* thisObj = slot.slotBase();
      JSValue* cachedVal = thisObj->getDirect(propertyName);
      if (cachedVal)
        return cachedVal;

      const HashEntry* entry = slot.staticEntry();
      JSValue* val = entry->value.functionValue(exec, entry->params, propertyName); 
      thisObj->putDirect(propertyName, val, entry->attr);
      return val;
  }

  /**
   * @internal
   * Helper for getStaticValueSlot and getStaticPropertySlot
   */
  template <class ThisImp>
  inline JSValue* staticValueGetter(ExecState* exec, JSObject*, const Identifier&, const PropertySlot& slot)
  {
      ThisImp* thisObj = static_cast<ThisImp*>(slot.slotBase());
      const HashEntry* entry = slot.staticEntry();
      return thisObj->getValueProperty(exec, entry->value.intValue);
  }

  /**
   * Helper method for property lookups
   *
   * This method does it all (looking in the hashtable, checking for function
   * overrides, creating the function or retrieving from cache, calling
   * getValueProperty in case of a non-function property, forwarding to parent if
   * unknown property).
   *
   * Template arguments:
   * @param FuncImp the class which implements this object's functions
   * @param ThisImp the class of "this". It must implement the getValueProperty(exec,token) method,
   * for non-function properties.
   * @param ParentImp the class of the parent, to propagate the lookup.
   *
   * Method arguments:
   * @param exec execution state, as usual
   * @param propertyName the property we're looking for
   * @param table the static hashtable for this class
   * @param thisObj "this"
   */
  template <class ThisImp, class ParentImp>
  inline bool getStaticPropertySlot(ExecState* exec, const HashTable* table, 
                                    ThisImp* thisObj, const Identifier& propertyName, PropertySlot& slot)
  {
    const HashEntry* entry = Lookup::findEntry(table, propertyName);

    if (!entry) // not found, forward to parent
      return thisObj->ParentImp::getOwnPropertySlot(exec, propertyName, slot);

    if (entry->attr & Function)
      slot.setStaticEntry(thisObj, entry, staticFunctionGetter);
    else
      slot.setStaticEntry(thisObj, entry, staticValueGetter<ThisImp>);

    return true;
  }

  /**
   * Simplified version of getStaticPropertySlot in case there are only functions.
   * Using this instead of getStaticPropertySlot allows 'this' to avoid implementing
   * a dummy getValueProperty.
   */
  template <class ParentImp>
  inline bool getStaticFunctionSlot(ExecState* exec, const HashTable* table,
                                    JSObject* thisObj, const Identifier& propertyName, PropertySlot& slot)
  {
    const HashEntry* entry = Lookup::findEntry(table, propertyName);

    if (!entry) // not found, forward to parent
      return static_cast<ParentImp*>(thisObj)->ParentImp::getOwnPropertySlot(exec, propertyName, slot);

    ASSERT(entry->attr & Function);

    slot.setStaticEntry(thisObj, entry, staticFunctionGetter);
    return true;
  }

  /**
   * Simplified version of getStaticPropertySlot in case there are no functions, only "values".
   * Using this instead of getStaticPropertySlot removes the need for a FuncImp class.
   */
  template <class ThisImp, class ParentImp>
  inline bool getStaticValueSlot(ExecState* exec, const HashTable* table,
                                 ThisImp* thisObj, const Identifier& propertyName, PropertySlot& slot)
  {
    const HashEntry* entry = Lookup::findEntry(table, propertyName);

    if (!entry) // not found, forward to parent
      return thisObj->ParentImp::getOwnPropertySlot(exec, propertyName, slot);

    ASSERT(!(entry->attr & Function));

    slot.setStaticEntry(thisObj, entry, staticValueGetter<ThisImp>);
    return true;
  }

  /**
   * This one is for "put".
   * It looks up a hash entry for the property to be set.  If an entry
   * is found it sets the value and returns true, else it returns false.
   */
  template <class ThisImp>
  inline bool lookupPut(ExecState* exec, const Identifier& propertyName,
                        JSValue* value, int attr,
                        const HashTable* table, ThisImp* thisObj)
  {
    const HashEntry* entry = Lookup::findEntry(table, propertyName);

    if (!entry)
      return false;

    if (entry->attr & Function) // function: put as override property
      thisObj->JSObject::put(exec, propertyName, value, attr);
    else if (!(entry->attr & ReadOnly))
      thisObj->putValueProperty(exec, entry->value.intValue, value, attr);

    return true;
  }

  /**
   * This one is for "put".
   * It calls lookupPut<ThisImp>() to set the value.  If that call
   * returns false (meaning no entry in the hash table was found),
   * then it calls put() on the ParentImp class.
   */
  template <class ThisImp, class ParentImp>
  inline void lookupPut(ExecState* exec, const Identifier& propertyName,
                        JSValue* value, int attr,
                        const HashTable* table, ThisImp* thisObj)
  {
    if (!lookupPut<ThisImp>(exec, propertyName, value, attr, table, thisObj))
      thisObj->ParentImp::put(exec, propertyName, value, attr); // not found: forward to parent
  }

  /**
   * This template method retrieves or create an object that is unique
   * (for a given global object) The first time this is called (for a given
   * property name), the Object will be constructed, and set as a property
   * of the global object. Later calls will simply retrieve that cached object. 
   * Note that the object constructor must take 1 argument, exec.
   */
  template <class ClassCtor>
  inline JSObject* cacheGlobalObject(ExecState* exec, const Identifier& propertyName)
  {
    JSGlobalObject* globalObject = exec->lexicalGlobalObject();
    JSValue* obj = globalObject->getDirect(propertyName);
    if (obj) {
      ASSERT(obj->isObject());
      return static_cast<JSObject* >(obj);
    }
    JSObject* newObject = new ClassCtor(exec);
    globalObject->put(exec, propertyName, newObject, Internal | DontEnum);
    return newObject;
  }

} // namespace

/**
 * Helpers to define prototype objects (each of which simply implements
 * the functions for a type of objects).
 * Sorry for this not being very readable, but it actually saves much copy-n-paste.
 * ParentPrototype is not our base class, it's the object we use as fallback.
 * The reason for this is that there should only be ONE DOMNode.hasAttributes (e.g.),
 * not one in each derived class. So we link the (unique) prototypes between them.
 *
 * Using those macros is very simple: define the hashtable (e.g. "DOMNodePrototypeTable"), then
 * KJS_DEFINE_PROTOTYPE(DOMNodePrototype)
 * KJS_IMPLEMENT_PROTOFUNC(DOMNodePrototypeFunction)
 * KJS_IMPLEMENT_PROTOTYPE("DOMNode", DOMNodePrototype, DOMNodePrototypeFunction)
 * and use DOMNodePrototype::self(exec) as prototype in the DOMNode constructor.
 * If the prototype has a "parent prototype", e.g. DOMElementPrototype falls back on DOMNodePrototype,
 * then the first line will use KJS_DEFINE_PROTOTYPE_WITH_PROTOTYPE, with DOMNodePrototype as the second argument.
 */

// These macros assume that a prototype's only properties are functions
#define KJS_DEFINE_PROTOTYPE(ClassPrototype) \
  class ClassPrototype : public KJS::JSObject { \
  public: \
    static KJS::JSObject* self(KJS::ExecState* exec); \
    virtual const KJS::ClassInfo* classInfo() const { return &info; } \
    static const KJS::ClassInfo info; \
    bool getOwnPropertySlot(KJS::ExecState* , const KJS::Identifier&, KJS::PropertySlot&); \
    ClassPrototype(KJS::ExecState* exec) \
      : KJS::JSObject(exec->lexicalGlobalObject()->objectPrototype()) { } \
    \
  };

#define KJS_DEFINE_PROTOTYPE_WITH_PROTOTYPE(ClassPrototype, ClassPrototypePrototype) \
    class ClassPrototype : public KJS::JSObject { \
    public: \
        static KJS::JSObject* self(KJS::ExecState* exec); \
        virtual const KJS::ClassInfo* classInfo() const { return &info; } \
        static const KJS::ClassInfo info; \
        bool getOwnPropertySlot(KJS::ExecState*, const KJS::Identifier&, KJS::PropertySlot&); \
        ClassPrototype(KJS::ExecState* exec) \
            : KJS::JSObject(ClassPrototypePrototype::self(exec)) { } \
    \
    };

#define KJS_IMPLEMENT_PROTOTYPE(ClassName, ClassPrototype) \
    const ClassInfo ClassPrototype::info = { ClassName"Prototype", 0, &ClassPrototype##Table  }; \
    JSObject* ClassPrototype::self(ExecState* exec) \
    { \
        static Identifier* prototypeIdentifier = new Identifier("[[" ClassName ".prototype]]"); \
        return KJS::cacheGlobalObject<ClassPrototype>(exec, *prototypeIdentifier); \
    } \
    bool ClassPrototype::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot) \
    { \
      return getStaticFunctionSlot<JSObject>(exec, &ClassPrototype##Table, this, propertyName, slot); \
    }

#define KJS_IMPLEMENT_PROTOTYPE_FUNCTION_WITH_CREATE(ClassFunction) \
    class ClassFunction : public KJS::InternalFunctionImp { \
    public: \
        static KJS::InternalFunctionImp* create(KJS::ExecState* exec, int len, const KJS::Identifier& name) \
        { \
            return new ClassFunction(exec, len, name); \
        } \
        ClassFunction(KJS::ExecState* exec, int len, const KJS::Identifier& name) \
            : KJS::InternalFunctionImp(static_cast<KJS::FunctionPrototype*>(exec->lexicalGlobalObject()->functionPrototype()), name) \
        { \
            put(exec, exec->propertyNames().length, KJS::jsNumber(len), KJS::DontDelete | KJS::ReadOnly | KJS::DontEnum); \
        } \
        \
        /* Macro user needs to implement the callAsFunction function. */ \
        virtual KJS::JSValue* callAsFunction(KJS::ExecState*, KJS::JSObject*, const KJS::List&); \
    }; \

#endif // KJS_lookup_h
