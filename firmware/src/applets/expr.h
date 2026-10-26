#pragma once
// Tiny expression language for applet templates: `${count + 1}`, `${wifi.up ? "online" : "offline"}`.
// Side-effect free, no allocation beyond Strings, never throws: errors come back through `err`.
#include <Arduino.h>

namespace expr {
struct Value {
  enum T : uint8_t { Null, Num, Bool, Str } t = Null;
  double n = 0; String s;
  Value() {}
  static Value num(double v) { Value x; x.t = Num; x.n = v; return x; }
  static Value boolean(bool v) { Value x; x.t = Bool; x.n = v ? 1 : 0; return x; }
  static Value str(const String& v) { Value x; x.t = Str; x.s = v; return x; }
  static Value null() { return Value(); }
  bool isNull() const { return t == Null; }
  bool truthy() const;
  double toNum() const;
  String toStr() const;
};

// Resolves a bare or dotted name (`count`, `device.id`, `resp.json.total`). Return false when unknown.
typedef bool (*Resolver)(const String& name, Value& out, void* ctx);

Value eval(const char* src, Resolver r, void* ctx, String* err = nullptr);
// Substitute every ${...} in a template. A template that is exactly one ${...} keeps the value's type.
Value renderTyped(const char* tpl, Resolver r, void* ctx, String* err = nullptr);
String render(const char* tpl, Resolver r, void* ctx, String* err = nullptr);
bool hasTemplate(const char* tpl);
String fmtNum(double v, int decimals = -1);   // -1: shortest (integers without ".0")
}
